#ifndef TROGUE_HOTRELOAD_H
#define TROGUE_HOTRELOAD_H

typedef struct TgWatcher TgWatcher;

// 开始监听目录内 *.json 的写入/重命名事件。
// 当前实现为 Linux inotify；其他平台返回 NULL。
TgWatcher *tg_watcher_start(const char *dir);
void tg_watcher_destroy(TgWatcher *w);

// 每帧调用。返回 1 时 name 填入变更文件名（不含目录）；0 = 本帧无事件；<0 = 错误。
// 内置 TROGUE_WATCH_DEBOUNCE_MS 抑制编辑器原子保存产生的连发事件。
int tg_watcher_poll(TgWatcher *w, char *name, int name_cap);

#endif // TROGUE_HOTRELOAD_H
