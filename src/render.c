#include "trogue/render.h"

#include <stdio.h>

#include "raylib.h"
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

void tg_render_world(const TgWorld *w, const Camera2D *cam)
{
    BeginMode2D(*cam);

    // tile 层：tileset 模式画图集区域，palette 模式画色块
    for (int li = 0; li < w->layer_count; li++) {
        const TgTileLayer *l = &w->layers[li];
        for (int ty = 0; ty < l->height; ty++) {
            for (int tx = 0; tx < l->width; tx++) {
                short t = l->tiles[ty * l->width + tx];
                if (t < 0)
                    continue;
                Vector2 pos = { (float)(l->origin_x + tx * w->tile_w),
                                (float)(l->origin_y + ty * w->tile_h) };
                if (w->tileset && t < w->tileset->count) {
                    DrawTextureRec(w->tileset->texture, w->tileset->rects[t], pos, WHITE);
                } else {
                    DrawRectangle(pos.x, pos.y, (float)w->tile_w, (float)w->tile_h,
                                  palette_color(w, t));
                }
            }
        }
    }

    // 实体：色块 + 描边
    for (int i = 0; i < w->entity_count; i++) {
        const TgEntity *e = &w->entities[i];
        if (!e->active)
            continue;
        Rectangle rec = { e->x, e->y, e->w, e->h };
        Color fill = { e->r, e->g, e->b, e->a };
        DrawRectangleRec(rec, fill);
        Color border = { (unsigned char)(e->r / 3), (unsigned char)(e->g / 3),
                         (unsigned char)(e->b / 3), 255 };
        DrawRectangleLinesEx(rec, 1.0f, border);
    }

    EndMode2D();
}
