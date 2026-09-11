// nav.hpp —— 导航原语（纯函数，无窗口可测）。
//
// 对齐原版 trogue-orign/src/core/coordinates.lua + systems/ai.lua：
//   - chebyshevDistance / hasLineOfSight（Bresenham，两端点均不判定遮挡）
//   - findPath 的 8 向 A*（chebyshev 启发 + 对角代价 1.414 + 迭代上限 1000），
//     本模块只返回下一步格（astar_step，调用方零状态）。

#pragma once

#include <functional>
#include <optional>

#include "game_core.hpp"

namespace game::nav {

int chebyshev(int x1, int y1, int x2, int y2);

// Bresenham 视线：起点与终点都不检查 solid（对齐 coordinates.lua:202-215——
// 终点格不判定，贴墙目标仍可见）。遮挡物由 is_solid 注入（AI 注入引擎
// solid 层查询；单测注入字面地图）。界外/层外由注入方决定（引擎语义 =
// 无数据不阻挡）。
bool has_line_of_sight(int x1, int y1, int x2, int y2,
                       const std::function<bool(int, int)>& is_solid);

// A* 单步：返回从 from 朝 goal 的下一格；无路径或 from==goal → nullopt。
//   - 地形阻挡经 gs.asset 的 tg::is_solid_at（仅 solid 判阻挡；层外不阻挡），
//     界内判定用 gs.map_w/h；
//   - 实体阻挡经 blocked_by_actor 注入（敌人互挡、玩家格不挡——对齐原版
//     「blocking 只查 Solid/Actor，而 player 原型无 Actor」的语义）；
//   - 斜切约束 = 地形 + 战斗实体（注入的 blocked_by_actor；coin 等惰性实体
//     不参与——比 try_move 的「地形 + 全部实体」窄，与原版 A* 仅地形的
//     差异为有意取舍）；
//   - 启发 = chebyshev，对角步代价 1.414，pop 迭代上限 1000（同原版）。
std::optional<TilePos> astar_step(
    const GameState& gs, TilePos from, TilePos goal,
    const std::function<bool(int, int)>& blocked_by_actor);

}  // namespace game
