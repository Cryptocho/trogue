#include "trogue/render.h"

#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "trogue/config.h"
#include "trogue/tileset.h"

Color tg_color(const unsigned char rgba[4])
{
    return (Color){ rgba[0], rgba[1], rgba[2], rgba[3] };
}

static Color palette_color(const TgWorld *w, short idx)
{
    if (idx < 0 || idx >= w->palette_count)
        return MAGENTA; // 越界索引显式可见
    return tg_color(w->palette[idx]);
}

// ── 独立贴图缓存：按路径懒加载 GPU 纹理，tg_render_shutdown 统一释放。
// 加载失败同样占位（tex.id==0 哨兵）：避免每帧重复加载刷日志，重启或重载前不再重试。
// 热重载不换贴图路径，缓存跨重载复用；容量按静态关卡规模限定。 ──
typedef struct SpriteTexEntry {
    char path[TROGUE_PATH_MAX];
    Texture2D tex;
    bool used;
} SpriteTexEntry;

static SpriteTexEntry g_sprite_tex[TROGUE_MAX_SPRITE_TEXTURES];

static const Texture2D *sprite_texture_get(const char *path)
{
    for (int i = 0; i < TROGUE_MAX_SPRITE_TEXTURES; i++) {
        if (g_sprite_tex[i].used && strcmp(g_sprite_tex[i].path, path) == 0)
            return &g_sprite_tex[i].tex;
    }
    for (int i = 0; i < TROGUE_MAX_SPRITE_TEXTURES; i++) {
        if (!g_sprite_tex[i].used) {
            char full[TROGUE_PATH_MAX];
            snprintf(full, sizeof(full), TROGUE_ASSETS_DIR "/%s", path);
            snprintf(g_sprite_tex[i].path, TROGUE_PATH_MAX, "%s", path);
            g_sprite_tex[i].tex = LoadTexture(full);
            g_sprite_tex[i].used = true;
            if (g_sprite_tex[i].tex.id == 0) {
                TraceLog(LOG_ERROR, "[render] 实体贴图加载失败（本会话内以色块渲染）: %s", full);
                return &g_sprite_tex[i].tex; // 失败哨兵：占用槽位，调用方回退色块
            }
            SetTextureFilter(g_sprite_tex[i].tex, TEXTURE_FILTER_POINT); // 像素风：最近邻采样
            return &g_sprite_tex[i].tex;
        }
    }
    TraceLog(LOG_WARNING, "[render] 独立贴图缓存已满（%d 张）", TROGUE_MAX_SPRITE_TEXTURES);
    return NULL;
}

void tg_render_shutdown(void)
{
    for (int i = 0; i < TROGUE_MAX_SPRITE_TEXTURES; i++) {
        if (g_sprite_tex[i].used && g_sprite_tex[i].tex.id != 0)
            UnloadTexture(g_sprite_tex[i].tex);
        g_sprite_tex[i].used = false;
    }
}

// ── tile 层：各层按自己的 tileset 走图集，否则 palette 色块 ──
static void draw_layers(const TgWorld *w)
{
    for (int li = 0; li < w->layer_count; li++) {
        const TgTileLayer *l = &w->layers[li];
        for (int ty = 0; ty < l->height; ty++) {
            for (int tx = 0; tx < l->width; tx++) {
                short t = l->tiles[ty * l->width + tx];
                if (t < 0)
                    continue;
                Vector2 pos = { (float)(l->origin_x + tx * w->tile_w),
                                (float)(l->origin_y + ty * w->tile_h) };
                if (l->tileset) {
                    if (t < l->tileset->count) {
                        DrawTextureRec(l->tileset->texture, l->tileset->rects[t], pos, WHITE);
                    } else {
                        // 解析层已按 tileset.count 校验值域，这里仅为渲染侧防御
                        DrawRectangle(pos.x, pos.y, (float)w->tile_w, (float)w->tile_h, MAGENTA);
                    }
                } else {
                    DrawRectangle(pos.x, pos.y, (float)w->tile_w, (float)w->tile_h,
                                  palette_color(w, t));
                }
            }
        }
    }
}

// 实体贴图区域与纹理；贴图不可用时返回 false（回退色块，保持可见性）
static bool entity_sprite_src(const TgWorld *w, const TgEntity *e,
                              const Texture2D **tex, Rectangle *src)
{
    const TgSprite *sp = &e->sprite;
    if (sp->tileset >= 0) {
        if (sp->tileset >= w->tileset_count) // 解析层已保证，渲染侧防御
            return false;
        const TgTileset *ts = w->tilesets[sp->tileset];
        if (sp->tile < 0 || sp->tile >= ts->count)
            return false;
        *tex = &ts->texture;
        *src = ts->rects[sp->tile];
        return true;
    }
    const Texture2D *t = sprite_texture_get(sp->texture);
    if (!t || t->id == 0) // 未命中缓存 / 加载失败哨兵 → 色块回退
        return false;
    float rw = sp->rw > 0 ? sp->rw : (float)t->width;
    float rh = sp->rh > 0 ? sp->rh : (float)t->height;
    *tex = t;
    *src = (Rectangle){ sp->rx, sp->ry, rw, rh };
    return true;
}

static void draw_entity(const TgWorld *w, const TgEntity *e)
{
    const Texture2D *tex = NULL;
    Rectangle src = { 0 };
    if (e->sprite.has && entity_sprite_src(w, e, &tex, &src)) {
        Vector2 pos = { e->x + e->sprite.ox, e->y + e->sprite.oy };
        Color tint = { e->r, e->g, e->b, e->a };
        DrawTextureRec(*tex, src, pos, tint);
        return;
    }
    // 无 sprite 或贴图不可用：色块 + 描边
    Rectangle rec = { e->x, e->y, e->w, e->h };
    Color fill = { e->r, e->g, e->b, e->a };
    DrawRectangleRec(rec, fill);
    Color border = { (unsigned char)(e->r / 3), (unsigned char)(e->g / 3),
                     (unsigned char)(e->b / 3), 255 };
    DrawRectangleLinesEx(rec, 1.0f, border);
}

// 实体按 (y 升序, z 升序) 稳定排序后绘制；完全并列保持数组序
// （导出器把场景 tile 实体排在数组前部，复刻 LÖVE「同行树先画、人后画」）。
static void draw_entities(const TgWorld *w)
{
    int idx[TROGUE_MAX_ENTITIES];
    int n = 0;
    for (int i = 0; i < w->entity_count; i++)
        if (w->entities[i].active)
            idx[n++] = i;

    for (int a = 1; a < n; a++) {
        int cur = idx[a];
        const TgEntity *ce = &w->entities[cur];
        int b = a - 1;
        while (b >= 0) {
            const TgEntity *pe = &w->entities[idx[b]];
            if (pe->y < ce->y || (pe->y == ce->y && pe->z <= ce->z))
                break;
            idx[b + 1] = idx[b];
            b--;
        }
        idx[b + 1] = cur;
    }
    for (int i = 0; i < n; i++)
        draw_entity(w, &w->entities[idx[i]]);
}

void tg_render_world(const TgWorld *w, const Camera2D *cam)
{
    BeginMode2D(*cam);
    draw_layers(w);
    draw_entities(w);
    EndMode2D();
}
