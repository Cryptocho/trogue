#include "trogue/ipc.h"

#include "trogue/config.h"
#include "trogue/scene.h"

#include "raylib.h"

#include <jansson.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#if TROGUE_IPC_ENABLED

typedef struct TgClient {
    int  fd;                            // -1 = 空槽
    int  len;
    char buf[TROGUE_IPC_LINE_MAX];
} TgClient;

struct TgIpc {
    int      listen_fd;
    int      port;
    TgWorld *world;
    TgClient clients[TROGUE_IPC_MAX_CLIENTS];
    bool     quit_requested;
    bool     shot_requested;
    char     shot_path[TROGUE_PATH_MAX];
};

// ── 工具 ──

static bool write_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t n = send(fd, data, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        data += n;
        len -= (size_t)n;
    }
    return true;
}

static bool send_json(int fd, json_t *obj)
{
    char *s = json_dumps(obj, JSON_COMPACT);
    if (!s)
        return false;
    bool ok = write_all(fd, s, strlen(s)) && write_all(fd, "\n", 1);
    free(s);
    return ok;
}

static json_t *err_json(const char *msg)
{
    return json_pack("{s:s}", "error", msg);
}

static int count_active_entities(const TgWorld *w)
{
    int n = 0;
    for (int i = 0; i < w->entity_count; i++)
        n += w->entities[i].active ? 1 : 0;
    return n;
}

// 实体快照：v1 字段之外，z/solid/sprite/vx/vy 仅在有意义时出现（v1.1 只增不改，老客户端忽略未知字段）
static json_t *entity_to_json(const TgWorld *w, const TgEntity *e)
{
    char col[10];
    if (e->a == 255)
        snprintf(col, sizeof(col), "#%02x%02x%02x", e->r, e->g, e->b);
    else
        snprintf(col, sizeof(col), "#%02x%02x%02x%02x", e->r, e->g, e->b, e->a);
    json_t *obj = json_pack("{s:s,s:s,s:f,s:f,s:f,s:f,s:s}",
                            "id", e->id, "type", e->type,
                            "x", (double)e->x, "y", (double)e->y,
                            "w", (double)e->w, "h", (double)e->h,
                            "color", col);
    if (e->z != 0)
        json_object_set_new(obj, "z", json_integer(e->z));
    if (e->solid)
        json_object_set_new(obj, "solid", json_true());
    if (e->sprite.has) {
        if (e->sprite.tileset >= 0 && e->sprite.tileset < w->tileset_count) {
            json_object_set_new(obj, "sprite",
                                json_pack("{s:s,s:i}", "tileset",
                                          w->tileset_names[e->sprite.tileset], "tile", e->sprite.tile));
        } else {
            json_t *sp = json_pack("{s:s}", "texture", e->sprite.texture);
            if (e->sprite.rw > 0 && e->sprite.rh > 0)
                json_object_set_new(sp, "region",
                                    json_pack("[f,f,f,f]", (double)e->sprite.rx, (double)e->sprite.ry,
                                              (double)e->sprite.rw, (double)e->sprite.rh));
            if (e->sprite.ox != 0.0f || e->sprite.oy != 0.0f)
                json_object_set_new(sp, "offset",
                                    json_pack("[f,f]", (double)e->sprite.ox, (double)e->sprite.oy));
            json_object_set_new(obj, "sprite", sp);
        }
    }
    if (e->vx != 0.0f || e->vy != 0.0f)
        json_object_set_new(obj, "v", json_pack("[f,f]", (double)e->vx, (double)e->vy));
    return obj;
}

// ── 命令处理：返回 data 对象（成功）或带 "error" 键的对象（失败） ──

static json_t *handle_command(TgIpc *ipc, const char *cmd, json_t *root)
{
    TgWorld *w = ipc->world;

    if (strcmp(cmd, "ping") == 0)
        return json_pack("{s:b,s:s}", "pong", 1, "version", TROGUE_VERSION);

    if (strcmp(cmd, "help") == 0) {
        static const char *cmds[] = { "ping", "status", "list_entities", "get_entity",
                                      "query_entities", "set_entity", "spawn", "despawn",
                                      "layers", "solid_at", "get_tile", "reload",
                                      "screenshot", "log", "quit", "help" };
        json_t *arr = json_array();
        for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
            json_array_append_new(arr, json_string(cmds[i]));
        return json_pack("{s:o}", "commands", arr);
    }

    if (strcmp(cmd, "status") == 0)
        return json_pack("{s:s,s:i,s:i,s:i,s:f,s:i}",
                         "scene", w->scene_name,
                         "reloads", w->reload_count,
                         "entities", count_active_entities(w),
                         "fps", w->fps,
                         "uptime_s", (double)w->uptime_s,
                         "port", ipc->port);

    if (strcmp(cmd, "list_entities") == 0) {
        json_t *arr = json_array();
        for (int i = 0; i < w->entity_count; i++)
            if (w->entities[i].active)
                json_array_append_new(arr, entity_to_json(w, &w->entities[i]));
        return json_pack("{s:o,s:i}", "entities", arr, "count", count_active_entities(w));
    }

    if (strcmp(cmd, "get_entity") == 0) {
        json_t *jid = json_object_get(root, "id");
        if (!json_is_string(jid))
            return err_json("需要字符串参数 id");
        TgEntity *e = tg_world_find_entity(w, json_string_value(jid));
        if (!e)
            return err_json("实体不存在");
        return json_pack("{s:o}", "entity", entity_to_json(w, e));
    }

    if (strcmp(cmd, "set_entity") == 0) {
        json_t *jid = json_object_get(root, "id");
        if (!json_is_string(jid))
            return err_json("需要字符串参数 id");
        TgEntity *e = tg_world_find_entity(w, json_string_value(jid));
        if (!e)
            return err_json("实体不存在");
        json_t *jx = json_object_get(root, "x");
        json_t *jy = json_object_get(root, "y");
        json_t *jcolor = json_object_get(root, "color");
        if (jx != NULL) {
            if (!json_is_number(jx))
                return err_json("x 必须是数字");
            e->x = (float)json_number_value(jx);
        }
        if (jy != NULL) {
            if (!json_is_number(jy))
                return err_json("y 必须是数字");
            e->y = (float)json_number_value(jy);
        }
        if (jcolor != NULL) {
            if (!json_is_string(jcolor))
                return err_json("color 必须是字符串");
            unsigned char rgba[4];
            if (!tg_parse_hex_color(json_string_value(jcolor), rgba))
                return err_json("color 需要 #rrggbb 格式");
            e->r = rgba[0];
            e->g = rgba[1];
            e->b = rgba[2];
            e->a = rgba[3];
        }
        return json_pack("{s:o}", "entity", entity_to_json(w, e));
    }

    if (strcmp(cmd, "spawn") == 0) {
        json_t *jx = json_object_get(root, "x");
        json_t *jy = json_object_get(root, "y");
        if (!json_is_number(jx) || !json_is_number(jy))
            return err_json("需要数字参数 x 和 y");
        json_t *jid = json_object_get(root, "id");
        json_t *jtype = json_object_get(root, "type");
        json_t *jw = json_object_get(root, "w");
        json_t *jh = json_object_get(root, "h");
        json_t *jcolor = json_object_get(root, "color");
        TgEntity *e = tg_world_spawn(w,
                                     json_is_string(jid) ? json_string_value(jid) : NULL,
                                     json_is_string(jtype) ? json_string_value(jtype) : NULL,
                                     (float)json_number_value(jx), (float)json_number_value(jy),
                                     (float)(json_is_number(jw) ? json_number_value(jw) : 0.0),
                                     (float)(json_is_number(jh) ? json_number_value(jh) : 0.0),
                                     json_is_string(jcolor) ? json_string_value(jcolor) : NULL);
        if (!e)
            return err_json("spawn 失败（池满）");
        return json_pack("{s:o}", "entity", entity_to_json(w, e));
    }

    if (strcmp(cmd, "despawn") == 0) {
        json_t *jid = json_object_get(root, "id");
        if (!json_is_string(jid))
            return err_json("需要字符串参数 id");
        if (!tg_world_despawn(w, json_string_value(jid)))
            return err_json("实体不存在");
        return json_pack("{s:b}", "despawned", 1);
    }

    if (strcmp(cmd, "query_entities") == 0) {
        // 半径模式（x/y/radius）或矩形模式（rect）；type 过滤可选。
        // radius 用「实体中心距查询点」比 AABB 相交更符合「附近有什么」的直觉。
        json_t *jx = json_object_get(root, "x");
        json_t *jy = json_object_get(root, "y");
        json_t *jr = json_object_get(root, "radius");
        json_t *jrect = json_object_get(root, "rect");
        bool radius_mode = jx != NULL || jy != NULL || jr != NULL;
        if (radius_mode) {
            if (!json_is_number(jx) || !json_is_number(jy) || !json_is_number(jr))
                return err_json("半径模式需要数字参数 x/y/radius");
            if (json_number_value(jr) < 0)
                return err_json("radius 不能为负");
        } else if (json_is_array(jrect) && json_array_size(jrect) == 4) {
            for (int i = 0; i < 4; i++)
                if (!json_is_number(json_array_get(jrect, i)))
                    return err_json("rect 元素必须是数字");
            if (json_number_value(json_array_get(jrect, 2)) < 0 ||
                json_number_value(json_array_get(jrect, 3)) < 0)
                return err_json("rect 的 w/h 不能为负");
        } else {
            return err_json("需要 x/y/radius（半径模式）或 rect:[x,y,w,h]（矩形模式）");
        }
        json_t *jtype = json_object_get(root, "type");
        const char *type = json_is_string(jtype) ? json_string_value(jtype) : NULL;
        json_t *arr = json_array();
        int count = 0;
        for (int i = 0; i < w->entity_count; i++) {
            TgEntity *e = &w->entities[i];
            if (!e->active)
                continue;
            if (type && strcmp(e->type, type) != 0)
                continue;
            bool hit;
            if (radius_mode) {
                float dx = (e->x + e->w * 0.5f) - (float)json_number_value(jx);
                float dy = (e->y + e->h * 0.5f) - (float)json_number_value(jy);
                float r = (float)json_number_value(jr);
                hit = dx * dx + dy * dy <= r * r;
            } else {
                float rx = (float)json_number_value(json_array_get(jrect, 0));
                float ry = (float)json_number_value(json_array_get(jrect, 1));
                float rw = (float)json_number_value(json_array_get(jrect, 2));
                float rh = (float)json_number_value(json_array_get(jrect, 3));
                hit = e->x < rx + rw && e->x + e->w > rx && e->y < ry + rh && e->y + e->h > ry;
            }
            if (hit) {
                json_array_append_new(arr, entity_to_json(w, e));
                count++;
            }
        }
        return json_pack("{s:o,s:i}", "entities", arr, "count", count);
    }

    if (strcmp(cmd, "layers") == 0) {
        json_t *arr = json_array();
        for (int i = 0; i < w->layer_count; i++) {
            TgTileLayer *l = &w->layers[i];
            int used = 0;
            for (int k = 0; k < l->width * l->height; k++)
                if (l->tiles[k] >= 0)
                    used++;
            // 层只持指针，名字反查 world.tilesets（palette 模式为 null）
            const char *ts_name = NULL;
            for (int k = 0; k < w->tileset_count; k++)
                if (w->tilesets[k] == l->tileset)
                    ts_name = w->tileset_names[k];
            json_t *lo = json_pack("{s:s,s:i,s:i,s:b,s:[i,i],s:i}",
                                   "name", l->name, "width", l->width, "height", l->height,
                                   "solid", l->solid ? 1 : 0,
                                   "origin", l->origin_x, l->origin_y, "tiles", used);
            json_object_set_new(lo, "tileset", ts_name ? json_string(ts_name) : json_null());
            json_array_append_new(arr, lo);
        }
        return json_pack("{s:o,s:i}", "layers", arr, "count", w->layer_count);
    }

    if (strcmp(cmd, "solid_at") == 0) {
        json_t *jx = json_object_get(root, "x");
        json_t *jy = json_object_get(root, "y");
        if (!json_is_number(jx) || !json_is_number(jy))
            return err_json("需要数字参数 x 和 y");
        return json_pack("{s:b}", "solid",
                         tg_world_is_solid_at(w, (float)json_number_value(jx),
                                              (float)json_number_value(jy)) ? 1 : 0);
    }

    if (strcmp(cmd, "get_tile") == 0) {
        json_t *jx = json_object_get(root, "x");
        json_t *jy = json_object_get(root, "y");
        if (!json_is_number(jx) || !json_is_number(jy))
            return err_json("需要数字参数 x 和 y");
        float px = (float)json_number_value(jx);
        float py = (float)json_number_value(jy);
        json_t *arr = json_array();
        for (int i = 0; i < w->layer_count; i++) {
            TgTileLayer *l = &w->layers[i];
            int v = tg_world_tile_at(w, l, px, py);
            if (v >= 0)
                json_array_append_new(arr, json_pack("{s:s,s:i}", "layer", l->name, "value", v));
        }
        return json_pack("{s:o,s:b}", "tiles", arr, "solid",
                         tg_world_is_solid_at(w, px, py) ? 1 : 0);
    }

    if (strcmp(cmd, "reload") == 0) {
        if (!tg_scene_reload(w))
            return err_json("重载失败，详见引擎日志");
        return json_pack("{s:b,s:i}", "reloaded", 1, "reloads", w->reload_count);
    }

    if (strcmp(cmd, "screenshot") == 0) {
        json_t *jpath = json_object_get(root, "path");
        if (json_is_string(jpath))
            snprintf(ipc->shot_path, sizeof(ipc->shot_path), "%s", json_string_value(jpath));
        else
            snprintf(ipc->shot_path, sizeof(ipc->shot_path), "screenshot_%ld.png",
                     (long)time(NULL));
        ipc->shot_requested = true;
        return json_pack("{s:s}", "path", ipc->shot_path);
    }

    if (strcmp(cmd, "log") == 0) {
        json_t *jmsg = json_object_get(root, "msg");
        if (!json_is_string(jmsg))
            return err_json("需要字符串参数 msg");
        TraceLog(LOG_WARNING, "[ipc] %s", json_string_value(jmsg));
        return json_pack("{s:b}", "logged", 1);
    }

    if (strcmp(cmd, "quit") == 0) {
        ipc->quit_requested = true;
        return json_pack("{s:b}", "bye", 1);
    }

    return err_json("未知命令，发送 {\"cmd\":\"help\"} 查看列表");
}

static void handle_line(TgIpc *ipc, TgClient *c, const char *line, int len)
{
    json_error_t jerr;
    json_t *root = json_loadb(line, (size_t)len, 0, &jerr);
    json_t *resp;
    if (!root) {
        char msg[256];
        snprintf(msg, sizeof(msg), "JSON 解析失败: %s", jerr.text);
        resp = json_pack("{s:b,s:s}", "ok", 0, "error", msg);
        send_json(c->fd, resp);
        json_decref(resp);
        return;
    }

    json_t *data = NULL;
    json_t *jcmd = json_object_get(root, "cmd");
    if (json_is_string(jcmd)) {
        data = handle_command(ipc, json_string_value(jcmd), root);
    } else {
        data = err_json("缺少字符串字段 cmd");
    }

    bool ok = !json_is_string(json_object_get(data, "error"));
    resp = json_pack("{s:b,s:o}", "ok", ok ? 1 : 0, ok ? "data" : "error",
                     json_incref(data));
    send_json(c->fd, resp);
    json_decref(data);
    json_decref(resp);
}

// ── 公共 API ──

TgIpc *tg_ipc_start(int port, TgWorld *world)
{
    signal(SIGPIPE, SIG_IGN);

    TgIpc *ipc = calloc(1, sizeof(*ipc));
    if (!ipc)
        return NULL;
    ipc->world = world;
    ipc->port = port;
    for (int i = 0; i < TROGUE_IPC_MAX_CLIENTS; i++)
        ipc->clients[i].fd = -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        TraceLog(LOG_ERROR, "[ipc] socket: %s", strerror(errno));
        free(ipc);
        return NULL;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 仅本机可达
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        TraceLog(LOG_ERROR, "[ipc] bind 127.0.0.1:%d 失败: %s%s", port, strerror(errno),
                 errno == EADDRINUSE ? "（端口被占用？已有实例在运行？）" : "");
        close(fd);
        free(ipc);
        return NULL;
    }
    if (listen(fd, 4) < 0 || fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        TraceLog(LOG_ERROR, "[ipc] listen/fcntl: %s", strerror(errno));
        close(fd);
        free(ipc);
        return NULL;
    }
    ipc->listen_fd = fd;
    TraceLog(LOG_INFO, "[ipc] tro-ipc v1 监听 tcp://127.0.0.1:%d（仅 DEBUG 构建）", port);
    return ipc;
}

void tg_ipc_destroy(TgIpc *ipc)
{
    if (!ipc)
        return;
    for (int i = 0; i < TROGUE_IPC_MAX_CLIENTS; i++)
        if (ipc->clients[i].fd >= 0)
            close(ipc->clients[i].fd);
    if (ipc->listen_fd >= 0)
        close(ipc->listen_fd);
    free(ipc);
}

void tg_ipc_poll(TgIpc *ipc)
{
    if (!ipc)
        return;

    // 接受所有新连接
    for (;;) {
        int fd = accept(ipc->listen_fd, NULL, NULL);
        if (fd < 0)
            break; // EAGAIN：没有更多连接
        fcntl(fd, F_SETFL, O_NONBLOCK);
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &(int){ 1 }, sizeof(int));

        int slot = -1;
        for (int i = 0; i < TROGUE_IPC_MAX_CLIENTS; i++)
            if (ipc->clients[i].fd < 0) {
                slot = i;
                break;
            }
        if (slot < 0) {
            json_t *r = json_pack("{s:b,s:s}", "ok", 0, "error", "server busy");
            send_json(fd, r);
            json_decref(r);
            close(fd);
            continue;
        }
        TgClient *c = &ipc->clients[slot];
        c->fd = fd;
        c->len = 0;
        json_t *hello = json_pack("{s:b,s:s,s:{s:s,s:i}}", "ok", 1, "event", "hello",
                                  "data", "protocol", "tro-ipc", "version", 1);
        send_json(fd, hello);
        json_decref(hello);
        TraceLog(LOG_INFO, "[ipc] 客户端接入 (slot %d)", slot);
    }

    // 读取已有客户端数据
    for (int i = 0; i < TROGUE_IPC_MAX_CLIENTS; i++) {
        TgClient *c = &ipc->clients[i];
        if (c->fd < 0)
            continue;
        char tmp[4096];
        bool dead = false;
        for (;;) {
            ssize_t n = recv(c->fd, tmp, sizeof(tmp), 0);
            if (n == 0) {
                dead = true;
                break;
            }
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                if (errno == EINTR)
                    continue;
                dead = true;
                break;
            }
            for (ssize_t k = 0; k < n; k++) {
                char ch = tmp[k];
                if (ch == '\n') {
                    handle_line(ipc, c, c->buf, c->len);
                    c->len = 0;
                } else if (ch != '\r') {
                    if (c->len >= TROGUE_IPC_LINE_MAX - 1) {
                        TraceLog(LOG_WARNING, "[ipc] 行超长，断开 slot %d", i);
                        dead = true;
                        break;
                    }
                    c->buf[c->len++] = ch;
                }
            }
            if (dead)
                break;
        }
        if (dead) {
            TraceLog(LOG_INFO, "[ipc] 客户端断开 (slot %d)", i);
            close(c->fd);
            c->fd = -1;
        }
    }
}

bool tg_ipc_take_screenshot(TgIpc *ipc, char *path, int cap)
{
    if (!ipc || !ipc->shot_requested)
        return false;
    if (path)
        snprintf(path, (size_t)cap, "%s", ipc->shot_path);
    ipc->shot_requested = false;
    return true;
}

bool tg_ipc_quit_requested(TgIpc *ipc)
{
    return ipc ? ipc->quit_requested : false;
}

#else /* !TROGUE_IPC_ENABLED：no-op 桩 */

struct TgIpc { int unused; };

TgIpc *tg_ipc_start(int port, TgWorld *world)
{
    (void)port;
    (void)world;
    return NULL;
}

void tg_ipc_destroy(TgIpc *ipc)
{
    (void)ipc;
}

void tg_ipc_poll(TgIpc *ipc)
{
    (void)ipc;
}

bool tg_ipc_take_screenshot(TgIpc *ipc, char *path, int cap)
{
    (void)ipc;
    (void)path;
    (void)cap;
    return false;
}

bool tg_ipc_quit_requested(TgIpc *ipc)
{
    (void)ipc;
    return false;
}

#endif
