#include "trogue/world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "trogue/tileset.h"

static unsigned char g_default_entity_color[4] = { 255, 255, 255, 255 };

// 语义：仅在解析成功时写 out；失败保持调用方预填的默认值（如 spawn 的白色）
bool tg_parse_hex_color(const char *hex, unsigned char out[4])
{
    if (!hex || hex[0] != '#')
        return false;
    size_t len = strlen(hex);
    if (len != 7 && len != 9)
        return false;
    unsigned r, g, b, a = 255;
    if (sscanf(hex + 1, "%2x%2x%2x%2x", &r, &g, &b, &a) < 3)
        return false;
    out[0] = (unsigned char)r;
    out[1] = (unsigned char)g;
    out[2] = (unsigned char)b;
    out[3] = (unsigned char)a;
    return true;
}

TgWorld *tg_world_create(void)
{
    TgWorld *w = calloc(1, sizeof(TgWorld));
    if (!w) {
        TraceLog(LOG_ERROR, "[world] calloc failed");
        return NULL;
    }
    w->bg[0] = 16;
    w->bg[1] = 16;
    w->bg[2] = 24;
    w->bg[3] = 255;
    w->tile_w = 16;
    w->tile_h = 16;
    return w;
}

void tg_world_destroy(TgWorld *w)
{
    if (!w)
        return;
    for (int i = 0; i < w->tileset_count; i++)
        tg_tileset_destroy(w->tilesets[i]);
    for (int i = 0; i < w->layer_count; i++)
        free(w->layers[i].tiles);
    free(w);
}

TgEntity *tg_world_find_entity(TgWorld *w, const char *id)
{
    if (!w || !id)
        return NULL;
    for (int i = 0; i < w->entity_count; i++) {
        TgEntity *e = &w->entities[i];
        if (e->active && strcmp(e->id, id) == 0)
            return e;
    }
    return NULL;
}

TgEntity *tg_world_spawn(TgWorld *w, const char *id, const char *type,
                         float x, float y, float w_, float h_,
                         const char *color_hex)
{
    if (!w)
        return NULL;
    // 优先复用 despawn 空槽，避免长期运行（Agent 反复 spawn/despawn）耗尽池
    TgEntity *e = NULL;
    for (int i = 0; i < w->entity_count; i++) {
        if (!w->entities[i].active) {
            e = &w->entities[i];
            break;
        }
    }
    if (!e) {
        if (w->entity_count >= TROGUE_MAX_ENTITIES) {
            TraceLog(LOG_WARNING, "[world] entity pool full (%d)", TROGUE_MAX_ENTITIES);
            return NULL;
        }
        e = &w->entities[w->entity_count++];
    }
    memset(e, 0, sizeof(*e));
    e->a = 255;
    e->active = true; // 先置位，唯一性检查才能匹配到自身

    if (id && id[0])
        snprintf(e->id, sizeof(e->id), "%s", id);
    else
        snprintf(e->id, sizeof(e->id), "ent_%d", w->entity_count);

    // 保证 id 唯一：与其他实体冲突时追加序号
    TgEntity *clash = tg_world_find_entity(w, e->id);
    if (clash != NULL && clash != e) {
        bool renamed = false;
        for (int n = 2; n < 1000; n++) {
            char cand[TROGUE_NAME_MAX];
            snprintf(cand, sizeof(cand), "%s_%d", e->id, n);
            if (!tg_world_find_entity(w, cand)) {
                snprintf(e->id, sizeof(e->id), "%s", cand);
                renamed = true;
                break;
            }
        }
        if (!renamed) {
            TraceLog(LOG_WARNING, "[world] 无法为实体生成唯一 id '%s'", e->id);
            e->active = false;
            return NULL;
        }
    }

    snprintf(e->type, sizeof(e->type), "%s", type ? type : "unknown");
    e->x = x;
    e->y = y;
    e->w = w_ > 0 ? w_ : (float)w->tile_w;
    e->h = h_ > 0 ? h_ : (float)w->tile_h;

    unsigned char rgba[4];
    memcpy(rgba, g_default_entity_color, 4);
    tg_parse_hex_color(color_hex, rgba);
    e->r = rgba[0];
    e->g = rgba[1];
    e->b = rgba[2];
    e->a = rgba[3];

    TraceLog(LOG_INFO, "[world] spawn entity '%s' (type=%s)", e->id, e->type);
    return e;
}

bool tg_world_despawn(TgWorld *w, const char *id)
{
    TgEntity *e = tg_world_find_entity(w, id);
    if (!e)
        return false;
    e->active = false;
    TraceLog(LOG_INFO, "[world] despawn entity '%s'", id);
    return true;
}

// 语义：solid 层只在自身矩形内阻挡（有 tile 即阻挡）；矩形外 = 该层无数据 = 不阻挡。
// 地图边界由关卡自身绘制的边墙表达（Godot 导出的层矩形只覆盖已绘制区域）。
static bool layer_tile_is_solid(TgWorld *w, TgTileLayer *l, float px, float py)
{
    int tx = (int)floorf((px - (float)l->origin_x) / (float)w->tile_w);
    int ty = (int)floorf((py - (float)l->origin_y) / (float)w->tile_h);
    if (tx < 0 || ty < 0 || tx >= l->width || ty >= l->height)
        return false; // 层外无数据
    return l->tiles[ty * l->width + tx] >= 0;
}

bool tg_world_is_solid_at(TgWorld *w, float px, float py)
{
    if (w->tile_w <= 0 || w->tile_h <= 0)
        return false;
    for (int i = 0; i < w->layer_count; i++)
        if (w->layers[i].solid && layer_tile_is_solid(w, &w->layers[i], px, py))
            return true;
    // 与 rect 查询保持一致：solid 实体同样阻挡
    for (int i = 0; i < w->entity_count; i++) {
        const TgEntity *e = &w->entities[i];
        if (!e->active || !e->solid)
            continue;
        if (px >= e->x && px < e->x + e->w && py >= e->y && py < e->y + e->h)
            return true;
    }
    return false;
}

bool tg_world_rect_hits_solid(TgWorld *w, float x, float y, float w_, float h_)
{
    if (w->tile_w <= 0 || w->tile_h <= 0)
        return false;
    for (int i = 0; i < w->layer_count; i++) {
        if (!w->layers[i].solid)
            continue;
        TgTileLayer *l = &w->layers[i];
        int tx0 = (int)floorf((x - (float)l->origin_x) / (float)w->tile_w);
        int ty0 = (int)floorf((y - (float)l->origin_y) / (float)w->tile_h);
        int tx1 = (int)floorf((x + w_ - 0.001f - (float)l->origin_x) / (float)w->tile_w);
        int ty1 = (int)floorf((y + h_ - 0.001f - (float)l->origin_y) / (float)w->tile_h);
        for (int ty = ty0; ty <= ty1; ty++) {
            for (int tx = tx0; tx <= tx1; tx++) {
                if (tx < 0 || ty < 0 || tx >= l->width || ty >= l->height)
                    continue; // 层外无数据
                if (l->tiles[ty * l->width + tx] >= 0)
                    return true;
            }
        }
    }
    // solid 实体同样阻挡（场景 tile 转出的实体如树，碰撞足印 = 实体 w/h）
    for (int i = 0; i < w->entity_count; i++) {
        const TgEntity *e = &w->entities[i];
        if (!e->active || !e->solid)
            continue;
        if (x < e->x + e->w && x + w_ > e->x && y < e->y + e->h && y + h_ > e->y)
            return true;
    }
    return false;
}
