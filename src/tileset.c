#include "trogue/tileset.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

TgTileset *tg_tileset_load(const char *path)
{
    char full[TROGUE_PATH_MAX];
    snprintf(full, sizeof(full), TROGUE_ASSETS_DIR "/%s", path);

    json_error_t jerr;
    json_t *root = json_load_file(full, 0, &jerr);
    if (!root) {
        TraceLog(LOG_ERROR, "[tileset] JSON 解析失败: %s (line %d)", jerr.text, jerr.line);
        return NULL;
    }

    TgTileset *ts = NULL;
    json_t *tiles = NULL;

    json_t *fmt = json_object_get(root, "format");
    json_t *ver = json_object_get(root, "version");
    if (!json_is_string(fmt) || strcmp(json_string_value(fmt), "tro-tileset") != 0) {
        TraceLog(LOG_ERROR, "[tileset] format 必须是 \"tro-tileset\"");
        goto out;
    }
    if (!json_is_integer(ver) || json_integer_value(ver) != 1) {
        TraceLog(LOG_ERROR, "[tileset] 不支持的 schema version（仅支持 1）");
        goto out;
    }

    json_t *jtex = json_object_get(root, "texture");
    if (!json_is_string(jtex)) {
        TraceLog(LOG_ERROR, "[tileset] 缺少 texture 字段");
        goto out;
    }
    json_t *jtw = json_object_get(root, "tile_width");
    json_t *jth = json_object_get(root, "tile_height");
    if (!json_is_integer(jtw) || !json_is_integer(jth)) {
        TraceLog(LOG_ERROR, "[tileset] 缺少 tile_width/height");
        goto out;
    }
    tiles = json_object_get(root, "tiles");
    if (!json_is_array(tiles) || json_array_size(tiles) == 0) {
        TraceLog(LOG_ERROR, "[tileset] tiles 数组为空或缺失");
        goto out;
    }

    ts = calloc(1, sizeof(*ts));
    if (!ts)
        goto out;
    ts->tile_w = (int)json_integer_value(jtw);
    ts->tile_h = (int)json_integer_value(jth);
    ts->count = (int)json_array_size(tiles);
    snprintf(ts->texture_path, sizeof(ts->texture_path), TROGUE_ASSETS_DIR "/%s",
             json_string_value(jtex));

    ts->rects = calloc((size_t)ts->count, sizeof(Rectangle));
    char *seen = calloc((size_t)ts->count, 1);
    if (!ts->rects || !seen) {
        free(seen);
        tg_tileset_destroy(ts);
        ts = NULL;
        goto out;
    }
    for (int i = 0; i < ts->count; i++) {
        json_t *t = json_array_get(tiles, i);
        if (!json_is_object(t)) {
            TraceLog(LOG_ERROR, "[tileset] tiles[%d] 不是对象", i);
            free(seen);
            tg_tileset_destroy(ts);
            ts = NULL;
            goto out;
        }
        int col = 0, row = 0, id = i;
        json_t *jid = json_object_get(t, "id");
        json_t *jcol = json_object_get(t, "col");
        json_t *jrow = json_object_get(t, "row");
        if (json_is_integer(jid))
            id = (int)json_integer_value(jid);
        if (json_is_integer(jcol))
            col = (int)json_integer_value(jcol);
        if (json_is_integer(jrow))
            row = (int)json_integer_value(jrow);
        if (id < 0 || id >= ts->count || seen[id]) {
            TraceLog(LOG_ERROR, "[tileset] tile id %d 越界或重复（合法范围 [0,%d)）", id,
                     ts->count);
            free(seen);
            tg_tileset_destroy(ts);
            ts = NULL;
            goto out;
        }
        seen[id] = 1;
        ts->rects[id] = (Rectangle){
            (float)(col * ts->tile_w), (float)(row * ts->tile_h),
            (float)ts->tile_w, (float)ts->tile_h,
        };
    }
    free(seen);

    ts->texture = LoadTexture(ts->texture_path);
    if (ts->texture.id == 0) {
        TraceLog(LOG_ERROR, "[tileset] 贴图加载失败: %s", ts->texture_path);
        tg_tileset_destroy(ts);
        ts = NULL;
        goto out;
    }
    SetTextureFilter(ts->texture, TEXTURE_FILTER_POINT); // 像素风：最近邻采样
    TraceLog(LOG_INFO, "[tileset] '%s' 载入成功 (%d tiles, texture %s)", path, ts->count,
             ts->texture_path);

out:
    json_decref(root);
    return ts;
}

void tg_tileset_destroy(TgTileset *ts)
{
    if (!ts)
        return;
    if (ts->texture.id != 0)
        UnloadTexture(ts->texture);
    free(ts->rects);
    free(ts);
}
