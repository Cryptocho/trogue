#include "trogue/scene.h"

#include <jansson.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "trogue/tileset.h"

static char g_last_error[512] = "(none)";

static void set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, ap);
    va_end(ap);
}

const char *tg_scene_last_error(void)
{
    return g_last_error;
}

// 交换两个 world 的内容；src 只保留壳（tiles 指针与 tileset 的所有权转移给 dst）
static void world_swap(TgWorld *dst, TgWorld *src)
{
    tg_tileset_destroy(dst->tileset); // 旧场景的 tileset 及其 GPU 纹理必须释放，否则每次重载泄漏
    dst->tileset = NULL;
    for (int i = 0; i < dst->layer_count; i++)
        free(dst->layers[i].tiles);
    *dst = *src;
    src->layer_count = 0; // 防止 destroy(tmp) 释放已转移的资源
    src->entity_count = 0;
    src->tileset = NULL;
}

static bool parse_tiles_array(json_t *arr, TgTileLayer *l, int max_tile)
{
    if (!json_is_array(arr) || (int)json_array_size(arr) != l->width * l->height) {
        set_error("layer '%s': tiles 必须是 width*height=%d 个整数的数组",
                  l->name, l->width * l->height);
        return false;
    }
    l->tiles = malloc(sizeof(short) * (size_t)(l->width * l->height));
    if (!l->tiles) {
        set_error("layer '%s': 内存不足", l->name);
        return false;
    }
    for (int i = 0; i < l->width * l->height; i++) {
        json_t *v = json_array_get(arr, i);
        if (!json_is_integer(v)) {
            set_error("layer '%s': tiles[%d] 不是整数", l->name, i);
            return false;
        }
        long raw = (long)json_integer_value(v);
        if (raw < -1 || raw >= max_tile) {
            set_error("layer '%s': tiles[%d]=%ld 超出范围 [0,%d)",
                      l->name, i, raw, max_tile);
            return false;
        }
        l->tiles[i] = (short)raw;
    }
    return true;
}

static bool scene_parse(TgWorld *w, const char *path)
{
    json_error_t jerr;
    json_t *root = json_load_file(path, 0, &jerr);
    if (!root) {
        set_error("JSON 解析失败: %s (line %d)", jerr.text, jerr.line);
        return false;
    }
    bool ok = false;
    int loaded_layers = 0;

    if (!json_is_object(root)) {
        set_error("根节点必须是对象");
        goto out;
    }

    // ── 格式校验 ──
    json_t *fmt = json_object_get(root, "format");
    json_t *ver = json_object_get(root, "version");
    if (!json_is_string(fmt) || strcmp(json_string_value(fmt), "tro-scene") != 0) {
        set_error("format 必须是 \"tro-scene\"");
        goto out;
    }
    if (!json_is_integer(ver) || json_integer_value(ver) != 1) {
        set_error("不支持的 schema version（当前仅支持 1）");
        goto out;
    }

    snprintf(w->scene_path, sizeof(w->scene_path), "%s", path);

    // ── meta ──
    json_t *meta = json_object_get(root, "meta");
    if (json_is_object(meta)) {
        json_t *name = json_object_get(meta, "name");
        if (json_is_string(name))
            snprintf(w->scene_name, sizeof(w->scene_name), "%s", json_string_value(name));
        json_t *bg = json_object_get(meta, "background");
        if (json_is_string(bg)) {
            unsigned char rgba[4] = { 16, 16, 24, 255 };
            tg_parse_hex_color(json_string_value(bg), rgba);
            memcpy(w->bg, rgba, 4);
        }
    }

    // ── tilemap ──
    json_t *tm = json_object_get(root, "tilemap");
    if (!json_is_object(tm)) {
        set_error("缺少 tilemap 对象");
        goto out;
    }
    json_t *tw = json_object_get(tm, "tile_width");
    json_t *th = json_object_get(tm, "tile_height");
    if (json_is_integer(tw))
        w->tile_w = (int)json_integer_value(tw);
    if (json_is_integer(th))
        w->tile_h = (int)json_integer_value(th);
    if (w->tile_w < 1 || w->tile_w > 256 || w->tile_h < 1 || w->tile_h > 256) {
        set_error("tile_width/height 需在 [1,256]");
        goto out;
    }

    // ── 可选 tileset 引用（tro-scene v1.1）：存在则 tiles 值 = tile id ──
    json_t *jts = json_object_get(tm, "tileset");
    if (json_is_string(jts)) {
        w->tileset = tg_tileset_load(json_string_value(jts));
        if (!w->tileset) {
            set_error("tileset '%s' 载入失败，详见引擎日志", json_string_value(jts));
            goto out;
        }
        if (w->tileset->tile_w != w->tile_w || w->tileset->tile_h != w->tile_h) {
            set_error("tileset 尺寸 (%dx%d) 与场景 tile_width/height (%dx%d) 不一致",
                      w->tileset->tile_w, w->tileset->tile_h, w->tile_w, w->tile_h);
            goto out;
        }
    }

    json_t *palette = json_object_get(tm, "palette");
    if (json_is_array(palette)) {
        int n = (int)json_array_size(palette);
        if (n > TROGUE_MAX_PALETTE) {
            set_error("palette 最多 %d 色", TROGUE_MAX_PALETTE);
            goto out;
        }
        for (int i = 0; i < n; i++) {
            json_t *c = json_array_get(palette, i);
            if (!json_is_string(c) || !tg_parse_hex_color(json_string_value(c), w->palette[i])) {
                set_error("palette[%d] 不是合法的 #rrggbb", i);
                goto out;
            }
        }
        w->palette_count = n;
    }

    json_t *layers = json_object_get(tm, "layers");
    if (!json_is_array(layers)) {
        set_error("缺少 tilemap.layers 数组");
        goto out;
    }
    int layer_n = (int)json_array_size(layers);
    if (layer_n > TROGUE_MAX_LAYERS) {
        set_error("layers 最多 %d 个", TROGUE_MAX_LAYERS);
        goto out;
    }
    for (int i = 0; i < layer_n; i++) {
        json_t *lj = json_array_get(layers, i);
        if (!json_is_object(lj)) {
            set_error("layers[%d] 不是对象", i);
            goto out;
        }
        TgTileLayer *l = &w->layers[w->layer_count];
        memset(l, 0, sizeof(*l));

        json_t *lname = json_object_get(lj, "name");
        snprintf(l->name, sizeof(l->name), "%s",
                 json_is_string(lname) ? json_string_value(lname) : "layer");

        json_t *lw = json_object_get(lj, "width");
        json_t *lh = json_object_get(lj, "height");
        l->width = json_is_integer(lw) ? (int)json_integer_value(lw) : 0;
        l->height = json_is_integer(lh) ? (int)json_integer_value(lh) : 0;
        if (l->width < 1 || l->width > 4096 || l->height < 1 || l->height > 4096) {
            set_error("layer '%s': width/height 需在 [1,4096]", l->name);
            goto out;
        }
        json_t *solid = json_object_get(lj, "solid");
        l->solid = json_is_true(solid);

        // 层原点（像素，可负）：Godot 负坐标 cell 的偏移表达
        json_t *jorigin = json_object_get(lj, "origin");
        if (json_is_array(jorigin) && json_array_size(jorigin) == 2) {
            json_t *jox = json_array_get(jorigin, 0);
            json_t *joy = json_array_get(jorigin, 1);
            if (json_is_number(jox) && json_is_number(joy)) {
                l->origin_x = (int)json_number_value(jox);
                l->origin_y = (int)json_number_value(joy);
            }
        }

        int max_tile = w->tileset ? w->tileset->count : w->palette_count;
        if (!parse_tiles_array(json_object_get(lj, "tiles"), l, max_tile))
            goto out;
        w->layer_count++;
        loaded_layers++;
    }
    TraceLog(LOG_INFO, "[scene] loaded %d tile layer(s)", loaded_layers);

    // ── entities ──
    json_t *ents = json_object_get(root, "entities");
    if (json_is_array(ents)) {
        int n = (int)json_array_size(ents);
        for (int i = 0; i < n; i++) {
            json_t *ej = json_array_get(ents, i);
            if (!json_is_object(ej)) {
                set_error("entities[%d] 不是对象", i);
                goto out;
            }
            json_t *jid = json_object_get(ej, "id");
            json_t *jx = json_object_get(ej, "x");
            json_t *jy = json_object_get(ej, "y");
            if (!json_is_string(jid) || !json_is_number(jx) || !json_is_number(jy)) {
                set_error("entities[%d] 需要 id/x/y", i);
                goto out;
            }
            json_t *jtype = json_object_get(ej, "type");
            json_t *jw = json_object_get(ej, "w");
            json_t *jh = json_object_get(ej, "h");
            json_t *jcolor = json_object_get(ej, "color");
            if (!tg_world_spawn(w,
                                json_string_value(jid),
                                json_is_string(jtype) ? json_string_value(jtype) : "unknown",
                                (float)json_number_value(jx), (float)json_number_value(jy),
                                (float)(json_is_number(jw) ? json_number_value(jw) : 0),
                                (float)(json_is_number(jh) ? json_number_value(jh) : 0),
                                json_is_string(jcolor) ? json_string_value(jcolor) : NULL)) {
                set_error("entities[%d]: spawn 失败（池满或 id 重复）", i);
                goto out;
            }
        }
    }
    ok = true;

out:
    json_decref(root);
    return ok;
}

bool tg_scene_load(TgWorld *w, const char *path)
{
    TgWorld *tmp = tg_world_create();
    if (!tmp)
        return false;
    if (!scene_parse(tmp, path)) {
        TraceLog(LOG_ERROR, "[scene] 载入失败: %s", tg_scene_last_error());
        tg_world_destroy(tmp);
        return false;
    }
    world_swap(w, tmp);
    w->reload_count = 0;
    tg_world_destroy(tmp);
    TraceLog(LOG_INFO, "[scene] '%s' 载入成功 (%s)", w->scene_name, path);
    return true;
}

bool tg_scene_reload(TgWorld *w)
{
    if (!w->scene_path[0])
        return false;

    // 仅快照 "player" 的运行时位置：玩家状态属于运行时所有权，
    // 其余实体以资产为准（否则编辑器/AI 对场景的坐标改动永远不会生效）
    struct { char id[TROGUE_NAME_MAX]; float x, y; } snap[TROGUE_MAX_ENTITIES];
    int snap_n = 0;
    for (int i = 0; i < w->entity_count; i++) {
        if (!w->entities[i].active || strcmp(w->entities[i].type, "player") != 0)
            continue;
        snprintf(snap[snap_n].id, TROGUE_NAME_MAX, "%s", w->entities[i].id);
        snap[snap_n].x = w->entities[i].x;
        snap[snap_n].y = w->entities[i].y;
        snap_n++;
    }
    int prev_reloads = w->reload_count;

    if (!tg_scene_load(w, w->scene_path))
        return false; // 失败时旧场景原样保留

    for (int i = 0; i < snap_n; i++) {
        TgEntity *e = tg_world_find_entity(w, snap[i].id);
        if (e) {
            e->x = snap[i].x;
            e->y = snap[i].y;
        }
    }
    w->reload_count = prev_reloads + 1;
    TraceLog(LOG_INFO, "[scene] 热重载完成（第 %d 次，保留 %d 个 player 位置）",
             w->reload_count, snap_n);
    return true;
}
