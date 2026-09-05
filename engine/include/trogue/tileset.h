#ifndef TROGUE_TILESET_H
#define TROGUE_TILESET_H

#include "raylib.h"

#include "config.h"

// tro-tileset v2：图集 + tile id → 图集区域映射。
// 每个文件单贴图；一个场景的多张贴图由 tro-scene v2 的 tilesets 数组组合。
// terrain_sets/peering_bits/custom_data 为透传字段，引擎忽略（autotile 阶段消费）。
// path 相对 assets/ 目录。

typedef struct TgTileset {
    Texture2D texture;
    char texture_path[TROGUE_PATH_MAX];
    int tile_w, tile_h;
    int count;        // tile 数，id 范围 [0, count)
    Rectangle *rects; // count 个，rects[id] = 图集区域（像素）
} TgTileset;

// path 形如 "tilesets/tile_set.json"（相对 assets/）。失败返回 NULL。
TgTileset *tg_tileset_load(const char *path);
void tg_tileset_destroy(TgTileset *ts);

#endif // TROGUE_TILESET_H
