#ifndef TROGUE_RENDER_H
#define TROGUE_RENDER_H

#include "raylib.h"
#include "world.h"

// 绘制全部 tile 层（调色板色块）与实体（色块 + 描边）。
// 内部使用 BeginMode2D/EndMode2D；调用方负责 ClearBackground。
void tg_render_world(const TgWorld *w, const Camera2D *cam);

// RGBA 字节 → raylib Color
Color tg_color(const unsigned char rgba[4]);

#endif // TROGUE_RENDER_H
