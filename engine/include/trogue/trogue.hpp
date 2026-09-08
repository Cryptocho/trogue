#pragma once
// trogue.hpp —— 引擎公共伞头（plan-5.1 §3.3）。
// 只聚合 trogue/*.hpp 公共头；不 include 任何第三方头（nlohmann/raylib 类型
// 不泄漏到伞接口外）。分卷 5.2~5.5 落地后逐个解除注释加入对应模块头。

#include "trogue/config.hpp"
#include "trogue/types.hpp"
#include "trogue/coro.hpp"
#include "trogue/scene.hpp"
#include "trogue/render.hpp"
#include "trogue/animation.hpp"
#include "trogue/tween.hpp"
#include "trogue/hotreload.hpp"
#include "trogue/ipc.hpp"