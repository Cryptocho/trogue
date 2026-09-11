// ai.hpp —— 敌人 AI（game 层）。
//
// 对齐原版 trogue-orign/src/systems/ai.lua：
//   - VISION_RANGE = 5（chebyshev）、ALERT_DELAY = 1（ai.lua:7-8）；
//   - 三态状态机（先迁移后行动）；
//   - idle 70% 4 向均匀游走 / alerted 停 / chasing 贴脸攻击否则 A* 一步；
//   - 触发点 = 玩家回合结束（本项目的 resolve_enemy_turn 收尾内同步结算）。
//
// 随机数：std::mt19937 固定种子（默认常量，单测/复现可另设），替代原版
// 全局 math.random——确定性是 AI 单测与崩溃复现的前提。

#pragma once

#include <random>

#include "event_bus.hpp"
#include "game_core.hpp"
#include "rules.hpp"

namespace game {

struct AiSystem {
    static constexpr std::uint32_t kDefaultSeed = 20260909u;

    explicit AiSystem(std::uint32_t seed = kDefaultSeed) : rng(seed) {}

    // 敌方阶段：逐战斗原型敌人（id 序，确定性）执行状态迁移 + 行动。
    // 攻击经 bus 发 AbilityUse（rules.bind 已订阅规则管线），本层不直接
    // 依赖规则引擎。玩家缺失/死亡/GameOver → 直接返回（收尾由
    // resolve_enemy_turn 负责）；阶段内玩家死亡 → 立即停止剩余敌人。
    void run_enemy_phase(GameState& gs, EventBus& bus);

    std::mt19937 rng;
};

}  // namespace game
