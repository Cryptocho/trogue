// game_core.cpp —— 回合制核心实现（闭环 + 敌 AI/战斗接入）。
//
// 碰撞语义对齐 trogue-orign（只读参考）：
//   movement.lua onMoveAttempt：目标格地形 solid → 实体互斥 → 斜向切角约束；
//   coordinates.lua canDiagonalMove：仅当两个相邻正交格都被阻挡时才禁止斜切。
//
// 移动与结算语义：
//   - try_move：泛化移动裁决，成功 emit MoveSucceeded（bus 非空时）；
//   - GameSystems 可空重载：完整敌方阶段（AI）vs 静止语义，单一代码路径；
//   - 收尾统一清除 pending_despawn（对齐原版延迟销毁 processDespawns）；
//     GameOver 保持相位：不 +1 回合、不发 TurnEnded。

#include "game_core.hpp"

#include "trogue/animation.hpp"  // AnimationSet::name（entity→动画集映射）

#include "ai.hpp"      // AiSystem（resolve_enemy_turn 完整模式调用）
#include "event_bus.hpp"

namespace game {

// ── 原型门控：战斗原型仅 goblin ──

bool is_combat_archetype(std::string_view type) { return type == "goblin"; }

const char* ai_state_name(AiPhase s) {
    switch (s) {
    case AiPhase::alerted: return "alerted";
    case AiPhase::chasing: return "chasing";
    case AiPhase::idle: break;
    }
    return "idle";
}

// ── 输入缓冲（对齐原版 input.lua addToKeyBuffer/processKeyBuffer/flush） ──

namespace {

// 结算缓冲队列：取前两个键合成方向（各自 clamp 到 [-1,1]），
// 相互抵消（如 [左,右]）则回落第一个键。返回要执行的步。
std::optional<Dir> process_key_buffer(InputBuffer& buf) {
    if (buf.size == 0) return std::nullopt;
    const Dir first = buf.queue[0];
    const bool has_second = buf.size >= 2;
    const Dir second = has_second ? buf.queue[1] : Dir{0, 0};

    buf.size = 0;
    buf.timer = 0.0f;

    if (has_second) {
        const auto clamp1 = [](int v) { return v < -1 ? -1 : (v > 1 ? 1 : v); };
        const int cdx = clamp1(first.dx + second.dx);
        const int cdy = clamp1(first.dy + second.dy);
        if (cdx != 0 || cdy != 0) return Dir{cdx, cdy};
        return first;  // 抵消：只走第一个键
    }
    return first;
}

}  // namespace

std::optional<Dir> input_buffer_push(InputBuffer& buf, Dir d) {
    // 斜向键不缓冲：清空队列并立即执行（原版 addToKeyBuffer 分支）。
    if (d.dx != 0 && d.dy != 0) {
        buf.size = 0;
        buf.timer = 0.0f;
        return d;
    }
    // 4 向键入队（容量 2），重设窗口。
    if (buf.size < 2) buf.queue[buf.size++] = d;
    buf.timer = InputBuffer::kWindow;
    if (buf.size >= 2) return process_key_buffer(buf);  // 满 2 立即合成
    return std::nullopt;                                 // 等第二个键或超时
}

std::optional<Dir> input_buffer_tick(InputBuffer& buf, float dt) {
    if (buf.size == 0 || buf.timer <= 0) return std::nullopt;
    buf.timer -= dt;
    if (buf.timer <= 0) return process_key_buffer(buf);  // 窗口耗尽走第一个键
    return std::nullopt;
}

void input_buffer_flush(InputBuffer& buf) {
    buf.size = 0;
    buf.timer = 0.0f;
}

void import_scene(GameState& gs, const tg::SceneAsset& asset) {
    gs.asset = &asset;
    gs.actors.clear();
    gs.pending_despawn.clear();

    // 地图边界：取第一个层的宽/高（forest 两层同尺寸）。
    // 约束：层 origin 必须为 (0,0)（当前资产无 origin 偏移；若未来资产带负
    // origin，网格换算需重做）。
    if (asset.layer_count() > 0) {
        const tg::LayerInfo& L = asset.layer(0);
        gs.map_w = L.width;
        gs.map_h = L.height;
    } else {
        gs.map_w = gs.map_h = 0;
    }

    const int n = asset.entity_count();
    for (int i = 0; i < n; ++i) {
        const tg::SceneEntity e = asset.entity(i);  // 值快照
        Actor a;
        a.id = e.id;
        a.type = e.type;
        // descriptor x/y 是像素左上角；换算到 tile 格（尺寸固定 16）。
        a.pos = TilePos{static_cast<int>(e.x) / kTileSize,
                        static_cast<int>(e.y) / kTileSize};
        a.is_player = (e.type == "player");
        a.color = e.color;
        a.z = e.z;
        a.sprite = e.sprite;
        // 动画集映射：动画集名 = 所属 entity id；未命中 = -1
        for (int s = 0; s < asset.animation_set_count(); ++s) {
            if (asset.animation_set(s).name() == a.id) {
                a.anim_set = s;
                break;
            }
        }
        // 原型门控：player → hp 100；goblin → hp 25 + AI；
        // 其余惰性实体（coin 等）不参与战斗（对齐原版 ai.lua:41 只查 AIState）。
        if (a.is_player) {
            a.hp = Hp{kPlayerMaxHp, kPlayerMaxHp};
        } else if (is_combat_archetype(a.type)) {
            a.hp = Hp{kGoblinMaxHp, kGoblinMaxHp};
        }
        const std::string key = a.id;  // 键副本（std::move 会清空 a.id）
        gs.actors.emplace(key, std::move(a));
    }

    // 新场景：回合复位（turn_count 初始 1，玩家回合）
    gs.phase = Phase::PlayerTurn;
    gs.turn_count = 1;
}

bool tile_is_solid(const GameState& gs, int tx, int ty) {
    // 越界视为阻挡（对齐原版 MovementSystem:isBlocked；地图边界由墙层表达，
    // 这里兜底防止步出层外）。
    if (tx < 0 || ty < 0 || tx >= gs.map_w || ty >= gs.map_h) return true;
    return tile_solid_terrain(gs, tx, ty);
}

bool tile_solid_terrain(const GameState& gs, int tx, int ty) {
    // 界外/层矩形外 = 无数据 = 不阻挡（引擎原语义；视野用）。
    if (tx < 0 || ty < 0 || tx >= gs.map_w || ty >= gs.map_h) return false;
    if (!gs.asset) return false;
    // 引擎查询用像素：格中心点（尺寸一半）。仅 solid 判阻挡。
    return tg::is_solid_at(*gs.asset,
                           tg::Vec2{static_cast<float>(tx * kTileSize +
                                                        kTileSize / 2),
                                    static_cast<float>(ty * kTileSize +
                                                        kTileSize / 2)}) ==
           tg::TileQueryResult::solid;
}

bool tile_has_entity(const GameState& gs, int tx, int ty,
                     const std::string& exclude_id) {
    for (const auto& [id, a] : gs.actors) {
        if (id == exclude_id) continue;
        if (a.pos.x == tx && a.pos.y == ty) return true;
    }
    return false;
}

bool can_diagonal_move(const GameState& gs, int from_x, int from_y, int dx,
                       int dy, const std::string& exclude_id) {
    // 两个相邻正交格（x+dx, y）与（x, y+dy）——仅当两者都被阻挡时禁止。
    const bool adj1 = tile_is_solid(gs, from_x + dx, from_y) ||
                      tile_has_entity(gs, from_x + dx, from_y, exclude_id);
    const bool adj2 = tile_is_solid(gs, from_x, from_y + dy) ||
                      tile_has_entity(gs, from_x, from_y + dy, exclude_id);
    return !(adj1 && adj2);
}

ActionResult try_move(GameState& gs, EventBus* bus, const std::string& actor_id,
                      int dx, int dy) {
    // 参数合法性：(0,0) 与越界 → Invalid（同 player_move 首段校验）
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || (dx == 0 && dy == 0))
        return ActionResult::Invalid;
    auto it = gs.actors.find(actor_id);
    if (it == gs.actors.end()) return ActionResult::Invalid;
    Actor& a = it->second;

    const int nx = a.pos.x + dx;
    const int ny = a.pos.y + dy;

    // 斜向：先查切过两正交格是否被双重阻挡
    if (dx != 0 && dy != 0 &&
        !can_diagonal_move(gs, a.pos.x, a.pos.y, dx, dy, a.id)) {
        return ActionResult::Blocked;
    }

    // 目标格地形/实体判定
    if (tile_is_solid(gs, nx, ny) || tile_has_entity(gs, nx, ny, a.id))
        return ActionResult::Blocked;

    const TilePos from = a.pos;
    a.pos = TilePos{nx, ny};
    if (bus) {
        bus->emit("MoveSucceeded", tg::Json{{"entity", a.id},
                                            {"from", tg::Json{from.x, from.y}},
                                            {"to", tg::Json{nx, ny}}});
    }
    return ActionResult::Moved;
}

namespace {

// 两条 player_move/player_wait 共享的尾部：行动成功后结算敌方阶段。
ActionResult finish_player_action(GameState& gs, const GameSystems* sys,
                                  ActionResult acted) {
    if (acted != ActionResult::Moved && acted != ActionResult::Waited)
        return acted;
    resolve_enemy_turn(gs, sys);
    return acted;
}

}  // namespace

// ── 兼容签名：敌方阶段 = 静止 + 回合 +1（无 AI、无事件） ──

ActionResult player_move(GameState& gs, int dx, int dy) {
    const Actor* p = gs.player();
    if (!p) return ActionResult::Invalid;
    return finish_player_action(
        gs, nullptr, try_move(gs, nullptr, p->id, dx, dy));
}

ActionResult player_wait(GameState& gs) {
    if (!gs.player()) return ActionResult::Invalid;
    return finish_player_action(gs, nullptr, ActionResult::Waited);
}

// ── GameSystems 版：完整敌方阶段（AI + 事件）；入口 phase 守卫 ──

ActionResult player_move(GameState& gs, GameSystems& sys, int dx, int dy) {
    if (gs.phase != Phase::PlayerTurn) return ActionResult::Invalid;
    const Actor* p = gs.player();
    if (!p) return ActionResult::Invalid;
    return finish_player_action(
        gs, &sys, try_move(gs, sys.bus, p->id, dx, dy));
}

ActionResult player_wait(GameState& gs, GameSystems& sys) {
    if (gs.phase != Phase::PlayerTurn) return ActionResult::Invalid;
    if (!gs.player()) return ActionResult::Invalid;
    return finish_player_action(gs, &sys, ActionResult::Waited);
}

void resolve_enemy_turn(GameState& gs, const GameSystems* sys) {
    gs.phase = Phase::EnemyTurn;

    // 完整模式：AI 行动（run_enemy_phase 内部处理玩家死亡即停）。
    if (sys && sys->ai && sys->bus) {
        sys->ai->run_enemy_phase(gs, *sys->bus);
    }

    // 统一延迟销毁（对齐原版 processDespawns；GameOver 也清除——尸体移除
    // 与相位无关）。emit 回调中只打标记、此处才 erase，遍历安全。
    for (const auto& id : gs.pending_despawn) gs.actors.erase(id);
    gs.pending_despawn.clear();

    // GameOver：收尾不推进回合、不发 TurnEnded、保持相位
    if (gs.phase == Phase::GameOver) return;

    gs.turn_count += 1;
    gs.phase = Phase::PlayerTurn;
    if (sys && sys->bus)
        sys->bus->emit("TurnEnded", tg::Json{{"turn", gs.turn_count}});
}

}  // namespace game
