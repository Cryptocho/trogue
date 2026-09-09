// game_core.cpp —— 里程碑 6 纯逻辑核心实现（见 game_core.hpp 头注释）。
//
// 碰撞语义对齐 trogue-orign（只读参考）：
//   movement.lua onMoveAttempt：目标格地形 solid → 实体互斥 → 斜向切角约束；
//   coordinates.lua canDiagonalMove：仅当两个相邻正交格都被阻挡时才禁止斜切。

#include "game_core.hpp"

namespace game {

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

    // 地图边界：取第一个层的宽/高（本里程碑 forest 两层同尺寸）。
    // 约束：层 origin 必须为 (0,0)（当前资产无 origin 偏移；若未来资产带负
    // origin，网格换算需重做——见 docs/plan-6.md §2）。
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
        const std::string key = a.id;  // 键副本（std::move 会清空 a.id）
        gs.actors.emplace(key, std::move(a));
    }

    // 新场景：回合复位（计划 §3.4：turn_count 初始 1，玩家回合）
    gs.phase = Phase::PlayerTurn;
    gs.turn_count = 1;
}

bool tile_is_solid(const GameState& gs, int tx, int ty) {
    // 越界视为阻挡（对齐原版 MovementSystem:isBlocked；地图边界由墙层表达，
    // 这里兜底防止步出层外）。
    if (tx < 0 || ty < 0 || tx >= gs.map_w || ty >= gs.map_h) return true;
    if (!gs.asset) return true;
    // 引擎查询用像素：格中心点（尺寸一半）。
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

// 结算敌方回合：本里程碑为「静止」策略——遍历敌人但不动任何实体。
// 回合 +1 并回到玩家回合（对齐原版 TurnEnd 语义：turnCount 每次 +1）。
void resolve_enemy_turn(GameState& gs) {
    gs.phase = Phase::EnemyTurn;
    // （未来 AI 里程碑：此处让敌人按策略行动）
    gs.turn_count += 1;
    gs.phase = Phase::PlayerTurn;
}

ActionResult player_move(GameState& gs, int dx, int dy) {
    // 参数合法性：(0,0) 与越界 → Invalid（计划 §3.4 拒绝路径）
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || (dx == 0 && dy == 0))
        return ActionResult::Invalid;

    const Actor* p = gs.player();
    if (!p) return ActionResult::Invalid;

    const int nx = p->pos.x + dx;
    const int ny = p->pos.y + dy;

    // 斜向：先查切过两正交格是否被双重阻挡
    if (dx != 0 && dy != 0 &&
        !can_diagonal_move(gs, p->pos.x, p->pos.y, dx, dy, p->id)) {
        return ActionResult::Blocked;
    }

    // 目标格地形/实体判定
    if (tile_is_solid(gs, nx, ny) || tile_has_entity(gs, nx, ny, p->id))
        return ActionResult::Blocked;

    // 行动成功：更新位置 → 结算敌方回合 → 回合 +1
    auto& player_ref = gs.actors.at(p->id);  // 重新取引用（避免悬垂）
    player_ref.pos = TilePos{nx, ny};
    resolve_enemy_turn(gs);
    return ActionResult::Moved;
}

ActionResult player_wait(GameState& gs) {
    if (!gs.player()) return ActionResult::Invalid;
    resolve_enemy_turn(gs);
    return ActionResult::Waited;
}

}  // namespace game