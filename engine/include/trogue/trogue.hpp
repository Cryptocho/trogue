#pragma once
// trogue.hpp —— 引擎公共伞头。
// 只聚合 trogue/*.hpp 公共头；不 include 任何第三方头（nlohmann/raylib 类型
// 不泄漏到伞接口外）。

#include "trogue/config.hpp"
#include "trogue/types.hpp"
#include "trogue/random.hpp"
#include "trogue/coro.hpp"
#include "trogue/task_runner.hpp"
#include "trogue/scene.hpp"
#include "trogue/collision.hpp"
#include "trogue/terrain.hpp"
#include "trogue/render.hpp"
#include "trogue/animation.hpp"
#include "trogue/tween.hpp"
#include "trogue/hotreload.hpp"
#include "trogue/ipc.hpp"