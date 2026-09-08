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

// 交换两个 world 的内容；src 只保留壳（tiles 指针与 tilesets 的所有权转移给 dst）
static void world_swap(TgWorld *dst, TgWorld *src)
{
    // 旧场景的 tileset 及其 GPU 纹理必须释放，否则每次重载泄漏
    for (int i = 0; i < dst->tileset_count; i++)
        tg_tileset_destroy(dst->tilesets[i]);
    for (int i = 0; i < dst->layer_count; i++)
        free(dst->layers[i].tiles);
    *dst = *src;
    src->layer_count = 0;   // 防止 destroy(tmp) 释放已转移的资源
    src->tileset_count = 0; // 层的 tileset 指针指向 tilesets[] 元素，一并转移
    src->entity_count = 0;
}

// 在已载入的 tilesets 中按名字查找；失败返回 -1
static int world_find_tileset(const TgWorld *w, const char *name)
{
    for (int i = 0; i < w->tileset_count; i++)
        if (strcmp(w->tileset_names[i], name) == 0)
            return i;
    return -1;
}

// 解析 tilemap.tilesets：全部载入并登记名字。成功返回 true。
static bool parse_tilesets(json_t *tm, TgWorld *w)
{
    json_t *arr = json_object_get(tm, "tilesets");
    if (arr == NULL)
        return true; // palette 模式
    if (!json_is_array(arr) || json_array_size(arr) == 0) {
        set_error("tilesets 必须是非空数组（palette/bare 模式请直接省略该字段）");
        return false;
    }
    if ((int)json_array_size(arr) > TROGUE_MAX_TILESETS) {
        set_error("tilesets 最多 %d 个", TROGUE_MAX_TILESETS);
        return false;
    }
    for (int i = 0; i < (int)json_array_size(arr); i++) {
        json_t *e = json_array_get(arr, i);
        if (!json_is_object(e)) {
            set_error("tilesets[%d] 不是对象", i);
            return false;
        }
        json_t *jname = json_object_get(e, "name");
        json_t *jpath = json_object_get(e, "path");
        if (!json_is_string(jname) || !json_is_string(jpath)) {
            set_error("tilesets[%d] 需要 name 和 path 字符串", i);
            return false;
        }
        const char *name = json_string_value(jname);
        const char *path = json_string_value(jpath);
        if (world_find_tileset(w, name) >= 0) {
            set_error("tileset name '%s' 重复", name);
            return false;
        }
        TgTileset *ts = tg_tileset_load(path);
        if (!ts) {
            set_error("tileset '%s' (%s) 载入失败，详见引擎日志", name, path);
            return false;
        }
        if (ts->tile_w != w->tile_w || ts->tile_h != w->tile_h) {
            set_error("tileset '%s' 尺寸 (%dx%d) 与场景 tile_width/height (%dx%d) 不一致",
                      name, ts->tile_w, ts->tile_h, w->tile_w, w->tile_h);
            tg_tileset_destroy(ts);
            return false;
        }
        snprintf(w->tileset_names[w->tileset_count], TROGUE_NAME_MAX, "%s", name);
        w->tilesets[w->tileset_count] = ts;
        w->tileset_count++;
    }
    // 同贴图被多个 tileset 引用只是多占一份 GPU 纹理，合法但值得提示
    for (int i = 0; i < w->tileset_count; i++)
        for (int k = i + 1; k < w->tileset_count; k++)
            if (strcmp(w->tilesets[i]->texture_path, w->tilesets[k]->texture_path) == 0)
                TraceLog(LOG_WARNING, "[scene] tileset '%s' 与 '%s' 使用同一贴图 %s（GPU 纹理重复加载）",
                         w->tileset_names[i], w->tileset_names[k], w->tilesets[i]->texture_path);
    return true;
}

// tro-scene v2 sprite 字段：图集形态 {tileset, tile} 或独立贴图形态 {texture[, region, offset]}
static bool parse_sprite(json_t *obj, TgSprite *sp, TgWorld *w)
{
    memset(sp, 0, sizeof(*sp));
    sp->tileset = -1;
    if (!json_is_object(obj)) {
        set_error("sprite 必须是对象");
        return false;
    }
    json_t *jts = json_object_get(obj, "tileset");
    json_t *jtile = json_object_get(obj, "tile");
    json_t *jtex = json_object_get(obj, "texture");
    if (jts != NULL || jtile != NULL) {
        if (jtex != NULL) {
            set_error("sprite 的图集形态 (tileset/tile) 与独立贴图形态 (texture) 互斥");
            return false;
        }
        if (!json_is_string(jts) || !json_is_integer(jtile)) {
            set_error("sprite 图集形态需要 tileset(字符串) + tile(整数)");
            return false;
        }
        int idx = world_find_tileset(w, json_string_value(jts));
        if (idx < 0) {
            set_error("sprite 引用了不存在的 tileset '%s'", json_string_value(jts));
            return false;
        }
        long tile = (long)json_integer_value(jtile);
        if (tile < 0 || tile >= w->tilesets[idx]->count) {
            set_error("sprite tile=%ld 超出 tileset '%s' 范围 [0,%d)", tile,
                      json_string_value(jts), w->tilesets[idx]->count);
            return false;
        }
        sp->tileset = idx;
        sp->tile = (int)tile;
    } else if (jtex != NULL) {
        if (!json_is_string(jtex) || !json_string_value(jtex)[0]) {
            set_error("sprite.texture 必须是非空字符串（相对 assets/）");
            return false;
        }
        snprintf(sp->texture, sizeof(sp->texture), "%s", json_string_value(jtex));
        json_t *jregion = json_object_get(obj, "region");
        if (jregion != NULL) {
            if (!json_is_array(jregion) || json_array_size(jregion) != 4) {
                set_error("sprite.region 必须是 [x,y,w,h]");
                return false;
            }
            for (int i = 0; i < 4; i++) {
                json_t *v = json_array_get(jregion, i);
                if (!json_is_number(v) || (i >= 2 && json_number_value(v) <= 0)) {
                    set_error("sprite.region[%d] 非法（w/h 需 > 0）", i);
                    return false;
                }
            }
            sp->rx = (float)json_number_value(json_array_get(jregion, 0));
            sp->ry = (float)json_number_value(json_array_get(jregion, 1));
            sp->rw = (float)json_number_value(json_array_get(jregion, 2));
            sp->rh = (float)json_number_value(json_array_get(jregion, 3));
        } // region 缺省：rw/rh = 0 → 渲染时取整图
        json_t *joffset = json_object_get(obj, "offset");
        if (joffset != NULL) {
            if (!json_is_array(joffset) || json_array_size(joffset) != 2) {
                set_error("sprite.offset 必须是 [ox,oy]");
                return false;
            }
            for (int i = 0; i < 2; i++) {
                if (!json_is_number(json_array_get(joffset, i))) {
                    set_error("sprite.offset[%d] 不是数字", i);
                    return false;
                }
            }
            sp->ox = (float)json_number_value(json_array_get(joffset, 0));
            sp->oy = (float)json_number_value(json_array_get(joffset, 1));
        }
    } else {
        set_error("sprite 需要 tileset+tile 或 texture");
        return false;
    }
    sp->has = true;
    return true;
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

    // ── 格式校验（v2 破坏性升级：只认 version 2）──
    json_t *fmt = json_object_get(root, "format");
    json_t *ver = json_object_get(root, "version");
    if (!json_is_string(fmt) || strcmp(json_string_value(fmt), "tro-scene") != 0) {
        set_error("format 必须是 \"tro-scene\"");
        goto out;
    }
    if (!json_is_integer(ver) || json_integer_value(ver) != 2) {
        set_error("不支持的 schema version（v2 起仅支持 2）");
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

    // ── tilesets（图集模式）──；与 palette 互斥
    if (!parse_tilesets(tm, w))
        goto out;
    json_t *palette = json_object_get(tm, "palette");
    bool has_palette = json_is_array(palette);
    if (palette != NULL && !has_palette) {
        set_error("palette 必须是数组（#rrggbb 字符串列表）");
        goto out;
    }
    if (has_palette) {
        if (w->tileset_count > 0) {
            set_error("tilesets 与 palette 互斥，只能选一种模式");
            goto out;
        }
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
    // 三态（v2.1）：图集 / palette 之外，允许 bare 纯实体场景
    // （无 tilesets 且无 palette，0 个 tile 层）；空/缺省 layers 见下
    bool is_bare = (w->tileset_count == 0 && !has_palette);

    json_t *layers = json_object_get(tm, "layers");
    if (is_bare) {
        // bare 场景：layers 允许缺省或空数组；无 tilesets/palette 却想画层 → 拒绝
        if (layers != NULL && (!json_is_array(layers) || json_array_size(layers) > 0)) {
            set_error("bare 场景（无 tilesets/palette）不允许 tile 层，需要层请提供 tilesets 或 palette");
            goto out;
        }
    } else if (!json_is_array(layers)) {
        set_error("缺少 tilemap.layers 数组");
        goto out;
    }
    int layer_n = json_is_array(layers) ? (int)json_array_size(layers) : 0;
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

        // 层级 tileset 引用：图集模式必填，palette 模式禁止
        json_t *jlts = json_object_get(lj, "tileset");
        if (w->tileset_count > 0) {
            if (!json_is_string(jlts)) {
                set_error("layer '%s': 图集模式下每层必须引用 tileset", l->name);
                goto out;
            }
            int idx = world_find_tileset(w, json_string_value(jlts));
            if (idx < 0) {
                set_error("layer '%s': 引用不存在的 tileset '%s'", l->name,
                          json_string_value(jlts));
                goto out;
            }
            l->tileset = w->tilesets[idx];
        } else if (jlts != NULL) {
            set_error("layer '%s': palette 模式下不允许 tileset 字段", l->name);
            goto out;
        }

        int max_tile = l->tileset ? l->tileset->count : w->palette_count;
        if (!parse_tiles_array(json_object_get(lj, "tiles"), l, max_tile))
            goto out;
        w->layer_count++;
        loaded_layers++;
    }
    TraceLog(LOG_INFO, "[scene] loaded %d tile layer(s), %d tileset(s)", loaded_layers,
             w->tileset_count);

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
            TgEntity *e = tg_world_spawn(w,
                                         json_string_value(jid),
                                         json_is_string(jtype) ? json_string_value(jtype) : "unknown",
                                         (float)json_number_value(jx), (float)json_number_value(jy),
                                         (float)(json_is_number(jw) ? json_number_value(jw) : 0),
                                         (float)(json_is_number(jh) ? json_number_value(jh) : 0),
                                         json_is_string(jcolor) ? json_string_value(jcolor) : NULL);
            if (!e) {
                set_error("entities[%d]: spawn 失败（池满或 id 重复）", i);
                goto out;
            }
            json_t *jz = json_object_get(ej, "z");
            if (json_is_number(jz)) // 放宽为 number：2.0 这类浮点 z 也接受（取整）
                e->z = (int)json_number_value(jz);
            if (json_is_true(json_object_get(ej, "solid")))
                e->solid = true;
            json_t *jsprite = json_object_get(ej, "sprite");
            if (jsprite != NULL && !parse_sprite(jsprite, &e->sprite, w)) {
                // tg_scene_last_error 返回的正是 set_error 要写的缓冲，先拷贝避免自重叠 UB
                char inner[256];
                snprintf(inner, sizeof(inner), "%s", tg_scene_last_error());
                set_error("entities[%d] ('%s'): %s", i, e->id, inner);
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
