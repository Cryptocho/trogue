// ai.cpp —— 敌人 AI 实现。
//
// 语义逐条对齐原版 ai.lua（行号见各段注释）：
//   - 状态迁移（:64-91）先迁移、后按新状态取动作（:94-100）；
//   - 视野（:108-121）：chebyshev ≤ 5 且 Bresenham LOS（两端点不判定，
//     solid 层遮挡；层外/界外 = 无数据 = 不阻挡）；
//   - 游走（:123-136）：70% 概率 4 向均匀随机一步（失败即原地，不重试）；
//   - 追击（:138-187）：dist ≤ 1 → emit AbilityUse；否则 A* 一步
//     （玩家格不阻挡——原版 player 原型无 Actor 组件）。
#include "ai.hpp"

#include <vector>

#include "nav.hpp"

namespace game {

namespace {

// 对齐 ai.lua:7-8
constexpr int kVisionRange = 5;   // chebyshev（11×11 视野）
constexpr int kAlertDelay = 1;    // 发现后停 1 个敌方回合再追
constexpr float kWanderChance = 0.7f;  // idle 游走概率（ai.lua:124）

// 视野判定（solid 注入引擎查询；仅战斗原型敌人使用）
bool can_see_player(const GameState& gs, const Actor& e, const Actor& p) {
    if (nav::chebyshev(e.pos.x, e.pos.y, p.pos.x, p.pos.y) > kVisionRange)
        return false;
    return nav::has_line_of_sight(e.pos.x, e.pos.y, p.pos.x, p.pos.y,
                                  [&](int x, int y) {
                                      return tile_solid_terrain(gs, x, y);
                                  });
}

}  // namespace

void AiSystem::run_enemy_phase(GameState& gs, EventBus& bus) {
    // 攻击经 bus 发 AbilityUse（rules.bind 已订阅规则管线），本层不直接
    // 依赖规则引擎。

    const Actor* player = gs.player();
    if (!player || !player->hp || player->hp->cur <= 0) return;  // 无玩家/已死
    if (gs.phase == Phase::GameOver) return;

    const int turn = gs.turn_count;

    // id 序遍历（std::map，确定性）；迁移/行动中不增删元素（延迟销毁），
    // 引用稳定。
    for (auto& [id, a] : gs.actors) {
        if (a.is_player) continue;
        if (!is_combat_archetype(a.type)) continue;  // 惰性实体（coin 等）跳过
        if (!a.hp || a.hp->cur <= 0) continue;       // 死亡待清除的不行动

        // 阶段内玩家死亡 → 剩余敌人立即停止（对齐原版
        // 「无玩家则直接收尾」ai.lua:43-46）
        player = gs.player();
        if (!player || !player->hp || player->hp->cur <= 0 ||
            gs.phase == Phase::GameOver)
            break;

        const bool can_see = can_see_player(gs, a, *player);
        const int dist = nav::chebyshev(a.pos.x, a.pos.y, player->pos.x,
                                        player->pos.y);
        const char* from = ai_state_name(a.ai.state);

        // ── 状态迁移（ai.lua:64-91；先迁移后行动） ──
        switch (a.ai.state) {
        case AiPhase::idle:
            if (can_see) {
                a.ai.state = AiPhase::alerted;
                a.ai.alerted_turn = turn;
                a.ai.target = player->pos;
                a.ai.has_target = true;
                bus.emit("StateChanged", tg::Json{{"entity", id},
                                                  {"from", from},
                                                  {"to", "alerted"},
                                                  {"turn", turn}});
            }
            break;
        case AiPhase::alerted:
            if (turn - a.ai.alerted_turn >= kAlertDelay) {
                a.ai.state = AiPhase::chasing;
                bus.emit("StateChanged", tg::Json{{"entity", id},
                                                  {"from", from},
                                                  {"to", "chasing"},
                                                  {"turn", turn}});
            }
            if (can_see) {
                a.ai.target = player->pos;
                a.ai.has_target = true;
            }
            break;
        case AiPhase::chasing:
            if (can_see) {
                a.ai.target = player->pos;
                a.ai.has_target = true;
            }
            // 丢失视线且抵达最后记忆位 → idle（ai.lua:85-90；清 target）
            if (!can_see && a.ai.has_target && a.pos == a.ai.target) {
                a.ai.state = AiPhase::idle;
                a.ai.has_target = false;
                bus.emit("StateChanged", tg::Json{{"entity", id},
                                                  {"from", from},
                                                  {"to", "idle"},
                                                  {"turn", turn}});
            }
            break;
        }

        // ── 行动（ai.lua:94-100；按迁移后的新状态） ──
        switch (a.ai.state) {
        case AiPhase::idle: {
            // 70% 概率 4 向均匀游走（失败即原地，不重试）
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            if (unit(rng) < kWanderChance) {
                static constexpr Dir kDirs4[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                std::uniform_int_distribution<int> pick(0, 3);
                const Dir d = kDirs4[pick(rng)];
                try_move(gs, &bus, id, d.dx, d.dy);
            }
            break;
        }
        case AiPhase::alerted:
            break;  // 停一回合（不动作）

        case AiPhase::chasing: {
            const Actor* p = gs.player();  // 防御：中途死亡则不动
            if (!p || !p->hp || p->hp->cur <= 0) break;
            if (dist <= 1) {
                // 贴脸攻击（经事件进规则管线；AI 不知道 RuleEngine 存在）
                bus.emit("AbilityUse", tg::Json{{"entity", id},
                                                {"ability", "punch"},
                                                {"target", tg::Json{p->pos.x,
                                                                    p->pos.y}}});
            } else {
                // A* 一步：玩家格不阻挡（对齐原版 blocking 只查 Solid/Actor
                // 而 player 无 Actor）；其他战斗原型互挡；惰性实体不挡。
                const auto blocked = [&](int x, int y) {
                    if (x == p->pos.x && y == p->pos.y) return false;
                    for (const auto& [oid, oa] : gs.actors) {
                        if (oid == id) continue;
                        if (oa.pos.x == x && oa.pos.y == y &&
                            is_combat_archetype(oa.type))
                            return true;
                    }
                    return false;
                };
                if (auto step = nav::astar_step(gs, a.pos, p->pos, blocked)) {
                    try_move(gs, &bus, id, step->x - a.pos.x, step->y - a.pos.y);
                }
                // 无路径 → 本回合原地（对齐原版 path nil）
            }
            break;
        }
        }
    }
}

}  // namespace game
