#include "trogue/hotreload.h"

#include "trogue/config.h"

#include "raylib.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if TROGUE_HOTRELOAD_ENABLED

#include <sys/inotify.h>

struct TgWatcher {
    int  fd;
    int  wd;
    long last_fire_ms;
    bool pending;                        // 防抖窗口内到达的事件，窗口结束后补触发
    char pending_name[TROGUE_NAME_MAX];
};

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static bool ends_with_json(const char *name)
{
    size_t n = strlen(name);
    return n > 5 && strcmp(name + n - 5, ".json") == 0;
}

TgWatcher *tg_watcher_start(const char *dir)
{
    TgWatcher *w = calloc(1, sizeof(*w));
    if (!w)
        return NULL;
    w->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (w->fd < 0) {
        TraceLog(LOG_ERROR, "[watch] inotify_init1: %s", strerror(errno));
        free(w);
        return NULL;
    }
    uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_MODIFY;
    w->wd = inotify_add_watch(w->fd, dir, mask);
    if (w->wd < 0) {
        TraceLog(LOG_ERROR, "[watch] 无法监听目录 '%s': %s", dir, strerror(errno));
        close(w->fd);
        free(w);
        return NULL;
    }
    TraceLog(LOG_INFO, "[watch] 监听 '%s'", dir);
    return w;
}

void tg_watcher_destroy(TgWatcher *w)
{
    if (!w)
        return;
    if (w->wd >= 0)
        inotify_rm_watch(w->fd, w->wd);
    if (w->fd >= 0)
        close(w->fd);
    free(w);
}

int tg_watcher_poll(TgWatcher *w, char *name, int name_cap)
{
    if (!w || w->fd < 0)
        return 0;

    // 排空事件队列，只报告第一个匹配的 *.json。
    // 尾沿去抖：窗口内的后续事件不丢弃，记为 pending，窗口结束后补触发，
    // 保证编辑器连发保存时最终状态一定会被重载。
    char buf[4096];
    bool found = false;
    char found_name[TROGUE_NAME_MAX];
    for (;;) {
        ssize_t n = read(w->fd, buf, sizeof(buf));
        if (n <= 0) {
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                TraceLog(LOG_WARNING, "[watch] read: %s", strerror(errno));
            break;
        }
        for (char *p = buf; p < buf + n;) {
            struct inotify_event *ev = (struct inotify_event *)p;
            p += sizeof(*ev) + (size_t)ev->len;
            if (ev->mask & IN_Q_OVERFLOW)
                return -1;
            if (!(ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_MODIFY)))
                continue;
            if (ev->len == 0 || !ends_with_json(ev->name))
                continue;
            if (!found) {
                snprintf(found_name, sizeof(found_name), "%s", ev->name);
                found = true;
            }
        }
    }

    long now = now_ms();
    bool in_window = now - w->last_fire_ms < TROGUE_WATCH_DEBOUNCE_MS;

    if (found) {
        if (in_window) {
            snprintf(w->pending_name, sizeof(w->pending_name), "%s", found_name);
            w->pending = true;
            return 0;
        }
        w->last_fire_ms = now;
        w->pending = false;
        snprintf(name, (size_t)name_cap, "%s", found_name);
        return 1;
    }
    if (w->pending && !in_window) {
        w->last_fire_ms = now;
        w->pending = false;
        snprintf(name, (size_t)name_cap, "%s", w->pending_name);
        return 1;
    }
    return 0;
}

#else /* !TROGUE_HOTRELOAD_ENABLED：no-op 桩 */

struct TgWatcher { int unused; };

TgWatcher *tg_watcher_start(const char *dir)
{
    (void)dir;
    return NULL;
}

void tg_watcher_destroy(TgWatcher *w)
{
    (void)w;
}

int tg_watcher_poll(TgWatcher *w, char *name, int name_cap)
{
    (void)w;
    (void)name;
    (void)name_cap;
    return 0;
}

#endif
