// rules.hpp —— RuleEngine 最小子集（game 层）。
//
// 对齐原版 trogue-orign/src/core/rule_engine.lua 的事件驱动管线，最小子集：
//   1 能力（punch）+ 1 效果（damage_physical 固定值）+ 冷却 + 死亡。
//
// 管线**严格按原版顺序**：
//   AbilityUse → 校验（失败仅 AbilityUseFailed）→ 设冷却（仅 >0 才记）
//   → DamageRequest → (handler) DamageDealt → EntityDied
//   → 最后 AbilityUsed（仅成功）——wire 上 DamageDealt 先于 AbilityUsed。
//   TurnEnded（对外事件，priority 100）→ 全体冷却 -1（下限 0）。
//
// 最小子集差异：省略 learned/passive/cost 检查；
// 射程校验（chebyshev ≤ range）为 C++ 加固；载荷字段为自有设计（与
// events 注册表统一）；死亡 = 延迟销毁标记（enemy → pending_despawn，
// player → GameOver 相位），由 game_core 收尾统一处理。

#pragma once

#include <map>
#include <string>
#include <vector>

#include "event_bus.hpp"
#include "game_core.hpp"

namespace game {

struct AbilityDef {
    std::string id;
    int cooldown = 0;  // 回合数
    int range = 1;     // chebyshev 射程（加固校验用）
    std::vector<std::string> effects;
};

struct EffectDef {
    std::string id;
    int value = 0;  // damage 固定值（公式/武器不在子集）
};

class RuleEngine {
public:
    RuleEngine();  // 内置 punch / damage_physical 定义（数值对齐原版）

    // 订阅 AbilityUse(0) / DamageRequest(100) / TurnEnded(100)。
    // gs 与 bus 必须在 RuleEngine 使用期内存活（main：Demo 成员，程序期；
    // 单测：同作用域局部对象）。热重载只换 asset 不换 gs 对象，无需重绑。
    void bind(GameState& gs, EventBus& bus);

    // 能力存在且冷却为 0（punch 冷却恒 0，结构为后续能力预留）
    bool can_use(const Actor& a, const std::string& ability) const;

    // 完整 tryUseAbility（供 AbilityUse handler 与单测直接驱动）。
    // 返回是否成功；失败已 emit AbilityUseFailed（内部事件，不发布）。
    bool try_use(const std::string& entity, const std::string& ability,
                 TilePos target);

    // 能力/效果定义表（punch / damage_physical 为内置；测试可注册新条目）
    std::map<std::string, AbilityDef> abilities;
    std::map<std::string, EffectDef> effects;

private:
    void process_damage(const EventData& d);  // DamageRequest handler
    void on_turn_ended();                     // TurnEnded handler：冷却 -1

    GameState* gs_ = nullptr;
    EventBus* bus_ = nullptr;
};

}  // namespace game
