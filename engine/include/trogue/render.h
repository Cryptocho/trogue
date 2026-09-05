#ifndef TROGUE_RENDER_H
#define TROGUE_RENDER_H

#include "raylib.h"
#include "world.h"

// 绘制全部 tile 层（各层按自己的 tileset 走图集区域或 palette 色块）与实体
// （有 sprite 画贴图 + tint，否则色块 + 描边）。
// 实体按 (y, z) 稳定排序绘制；内部使用 BeginMode2D/EndMode2D；调用方负责 ClearBackground。
void tg_render_world(const TgWorld *w, const Camera2D *cam);

// RGBA 字节 → raylib Color
Color tg_color(const unsigned char rgba[4]);

// 释放 render 模块懒加载的独立贴图缓存（GPU 纹理）。应用关闭前调用一次。
void tg_render_shutdown(void);

#endif // TROGUE_RENDER_H
