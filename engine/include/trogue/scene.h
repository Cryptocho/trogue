#ifndef TROGUE_SCENE_H
#define TROGUE_SCENE_H

#include "world.h"

// 解析 tro-scene JSON 并完全替换 world 内容。
// 解析失败时 world 保持原样，可经 tg_scene_last_error() 取原因。
bool tg_scene_load(TgWorld *w, const char *path);

// 热重载：重新载入 w->scene_path，按 id 保留已有实体的运行时位置。
bool tg_scene_reload(TgWorld *w);

// 最近一次解析失败的描述（静态缓冲，非线程安全）
const char *tg_scene_last_error(void);

#endif // TROGUE_SCENE_H
