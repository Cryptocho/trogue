// rules.cpp —— RuleEngine 最小子集实现。
//
// 管线顺序对齐原版 rule_engine.lua：
//   tryUseAbility（:183-268）：canUse 校验（失败 :232 AbilityUseFailed）→
//   扣资源/设冷却（:244-255，仅 cooldown>0 才写）→ applyAbility（:258，
//   emit DamageRequest → handler :467-556 扣血 → DamageDealt :543 →
//   EntityDied :669-681）→ 最后仅成功时 emit AbilityUsed（:261-268）。
#include "rules.hpp"

#include "nav.hpp"  // nav::chebyshev（射程校验）

namespace game {

RuleEngine::RuleEngine() {
    // punch：对齐原版 ability.lua:81-90（cooldown=0、cost={}、range=1、
    // effects={damage_physical}）
    abilities["punch"] = AbilityDef{"punch", /*cooldown=*/0, /*range=*/1,
                                    {"damage_physical"}};
    // damage_physical：对齐原版 effect.lua:77-84（type=damage、value=5）
    effects["damage_physical"] = EffectDef{"damage_physical", /*value=*/5};
}

void RuleEngine::bind(GameState& gs, EventBus& bus) {
    gs_ = &gs;
    bus_ = &bus;
    // AbilityUse 最高优先（规则管线入口）；结算/冷却对齐原版 priority 100。
    bus.on("AbilityUse",
           [this](const EventData& d) {
               try_use(d.at("entity").get<std::string>(),
                       d.at("ability").get<std::string>(),
                       TilePos{d.at("target").at(0).get<int>(),
                               d.at("target").at(1).get<int>()});
           },
           0);
    bus.on("DamageRequest", [this](const EventData& d) { process_damage(d); },
           100);
    bus.on("TurnEnded", [this](const EventData&) { on_turn_ended(); }, 100);
}

bool RuleEngine::can_use(const Actor& a, const std::string& ability) const {
    if (!abilities.count(ability)) return false;
    const auto it = a.cooldowns.find(ability);
    return it == a.cooldowns.end() || it->second <= 0;
}

bool RuleEngine::try_use(const std::string& entity, const std::string& ability,
                         TilePos target) {
    // gs_/bus_ 由 bind 设置；未绑定即调用属编程错误（断言失败优于悬垂）。
    GameState& gs = *gs_;
    EventBus& bus = *bus_;

    const auto fail = [&](const char* reason) {
        bus.emit("AbilityUseFailed", tg::Json{{"entity", entity},
                                              {"ability", ability},
                                              {"reason", reason}});
        return false;
    };

    const auto src = gs.actors.find(entity);
    if (src == gs.actors.end()) return fail("no_source");
    const auto def = abilities.find(ability);
    if (def == abilities.end()) return fail("no_ability");
    if (!can_use(src->second, ability)) return fail("cooldown");

    // 目标校验：存在 / 非自身 / 存活且有 hp / 在射程内（射程为 C++ 加固）。
    Actor* tgt = nullptr;
    for (auto& [id, a] : gs.actors) {
        if (a.pos == target) {
            tgt = &a;
            break;
        }
    }
    if (tgt == nullptr || tgt->id == entity || !tgt->hp ||
        tgt->hp->cur <= 0)
        return fail("no_target");
    if (nav::chebyshev(src->second.pos.x, src->second.pos.y, tgt->pos.x,
                       tgt->pos.y) > def->second.range)
        return fail("out_of_range");

    // 设冷却：仅 >0 才登记（对齐原版 :252-255；punch=0 不写）。
    if (def->second.cooldown > 0)
        src->second.cooldowns[ability] = def->second.cooldown;

    // 效果结算先于 AbilityUsed（原版 applyAbility :258 → AbilityUsed :261）。
    for (const auto& fx : def->second.effects) {
        const auto e = effects.find(fx);
        if (e == effects.end()) continue;
        bus.emit("DamageRequest", tg::Json{{"source", entity},
                                           {"target", tgt->id},
                                           {"amount", e->second.value}});
    }

    // 仅成功时发布，且在效果之后（wire 序：DamageDealt → AbilityUsed）。
    bus.emit("AbilityUsed", tg::Json{{"entity", entity},
                                     {"ability", ability},
                                     {"target", tg::Json{target.x, target.y}},
                                     {"turn", gs.turn_count}});
    return true;
}

void RuleEngine::process_damage(const EventData& d) {
    GameState& gs = *gs_;
    EventBus& bus = *bus_;
    const auto src = d.at("source").get<std::string>();
    const auto tgt_id = d.at("target").get<std::string>();
    const int amount = d.at("amount").get<int>();
    const auto it = gs.actors.find(tgt_id);
    // 防御：目标缺失/无 hp/已死（try_use 已校验，这里兜底）。
    if (it == gs.actors.end() || !it->second.hp || it->second.hp->cur <= 0)
        return;
    Actor& t = it->second;
    t.hp->cur -= amount;
    bus.emit("DamageDealt", tg::Json{{"source", src},
                                     {"target", tgt_id},
                                     {"amount", amount},
                                     {"hp", t.hp->cur},
                                     {"max_hp", t.hp->max}});
    if (t.hp->cur > 0) return;

    // 死亡：延迟销毁（enemy → pending_despawn，收尾统一清除；
    // player → GameOver 相位，不 despawn）。
    bus.emit("EntityDied",
             tg::Json{{"entity", tgt_id}, {"turn", gs.turn_count}});
    if (t.is_player) {
        gs.phase = Phase::GameOver;
    } else {
        gs.pending_despawn.push_back(tgt_id);
    }
}

void RuleEngine::on_turn_ended() {
    // 全体冷却 -1（下限 0；对齐原版 :779-796）。归零即移除条目，
    // 维持「cooldowns 仅含 >0」的不变量。
    for (auto& [id, a] : gs_->actors) {
        for (auto it = a.cooldowns.begin(); it != a.cooldowns.end();) {
            if (it->second > 1) {
                it->second -= 1;
                ++it;
            } else {
                it = a.cooldowns.erase(it);
            }
        }
    }
}

}  // namespace game
