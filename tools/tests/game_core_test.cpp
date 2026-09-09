// game_core_test.cpp —— 里程碑 6：回合制核心无窗口单测（docs/plan-6.md §4.e）。
//
// 覆盖：
//   1. import_scene：从 forest.json（palette 手写关卡）导入玩家/敌人、边界、回合复位
//   2. 玩家移动：成功移动消耗回合（+1）、撞墙（地形 solid）不消耗
//   3. 斜向切角：仅当两相邻正交格都被阻挡时禁止（对齐原版 canDiagonalMove）
//   4. 实体互斥：目标格被敌人占用 → blocked
//   5. wait：回合推进
//   6. 玩家不存在 → invalid（移动/等待都拒绝）
//
// 依赖 CWD = CMAKE_SOURCE_DIR（WORKING_DIRECTORY 设置）读取 assets/scenes/forest.json。

#include <cstdio>

#include "test_util.hpp"
#include "game_core.hpp"

namespace {

// 便捷构造：直接在场地上摆一个自定义 actor（不依赖 asset 里的实体）
void put_actor(game::GameState& gs, const std::string& id, int tx, int ty,
               bool is_player = false) {
    game::Actor a;
    a.id = id;
    a.type = is_player ? "player" : "enemy";
    a.pos = game::TilePos{tx, ty};
    a.is_player = is_player;
    gs.actors[id] = a;
}

// 便捷断言：玩家在 (tx, ty)
bool player_at(const game::GameState& gs, int tx, int ty) {
    const game::Actor* p = gs.player();
    return p && p->pos.x == tx && p->pos.y == ty;
}

bool test_import_forest() {
    auto asset = tg::SceneAsset::load("assets/scenes/forest.json");
    REQUIRE(asset.has_value());
    game::GameState gs;
    game::import_scene(gs, *asset);
    CHECK(gs.map_w == 24 && gs.map_h == 16);
    CHECK(gs.actors.size() == 4);
    CHECK(player_at(gs, 2, 2));              // player 在 (2,2)
    CHECK(gs.player() && gs.player()->is_player);
    CHECK(gs.turn_count == 1);
    CHECK(gs.phase == game::Phase::PlayerTurn);
    // 敌人三只（goblin_1/2/3）
    int enemies = 0;
    for (const auto& [id, a] : gs.actors) if (!a.is_player) ++enemies;
    CHECK(enemies == 3);
    return true;
}

bool test_move_consumes_turn() {
    auto gs = game::GameState{};
    auto asset = tg::SceneAsset::load("assets/scenes/forest.json");
    REQUIRE(asset.has_value());
    game::import_scene(gs, *asset);
    // 开场玩家 (2,2)，右移一格到 (3,2)（非墙、无实体）
    const auto r = game::player_move(gs, 1, 0);
    CHECK(r == game::ActionResult::Moved);
    CHECK(player_at(gs, 3, 2));
    CHECK(gs.turn_count == 2);               // 移动吃完一回合
    // 再走一步 (4,2)（也是空地）
    CHECK(game::player_move(gs, 1, 0) == game::ActionResult::Moved);
    CHECK(player_at(gs, 4, 2));
    CHECK(gs.turn_count == 3);
    return true;
}

static bool test_blocked_terrain_no_turn() {
    auto gs = game::GameState{};
    auto g = tg::SceneAsset::load("assets/scenes/forest.json");
    REQUIRE(g.has_value());
    game::import_scene(gs, *g);
    // 玩家移到 (5,2)（内墙 rect(5,2)-(6,3) 的左上），向上走 (5,1) 非墙；向左 (4,2) 非墙
    gs.actors["player"].pos = game::TilePos{4, 2};
    // 撞内墙：向右 (5,2) 是墙 → blocked，回合不前进
    const int tc = gs.turn_count;
    CHECK(game::player_move(gs, 1, 0) == game::ActionResult::Blocked);
    CHECK(player_at(gs, 4, 2));
    CHECK(gs.turn_count == tc);
    // 撞地图外框：玩家放 (1,1)，向左 (0,1) 墙 → blocked
    gs.actors["player"].pos = game::TilePos{1, 1};
    CHECK(game::player_move(gs, -1, 0) == game::ActionResult::Blocked);
    CHECK(player_at(gs, 1, 1));
    return true;
}

static bool test_entity_blocked() {
    auto gs = game::GameState{};
    auto g = tg::SceneAsset::load("assets/scenes/forest.json");
    REQUIRE(g.has_value());
    game::import_scene(gs, *g);
    // 玩家 (2,2)，敌人占 (3,2)（覆写 goblin_1 到该格）
    gs.actors["goblin_1"].pos = game::TilePos{3, 2};
    const int tc = gs.turn_count;
    CHECK(game::player_move(gs, 1, 0) == game::ActionResult::Blocked);
    CHECK(player_at(gs, 2, 2));
    CHECK(gs.turn_count == tc);
    return true;
}

static bool test_diagonal_rule() {
    auto gs = game::GameState{};
    auto g = tg::SceneAsset::load("assets/scenes/forest.json");  // 提供 tile 查询
    REQUIRE(g.has_value());
    game::import_scene(gs, *g);
    gs.actors.clear();  // 丢弃 asset 实体，只用手摆的布局测切角
    put_actor(gs, "player", 2, 2, /*is_player=*/true);

    // 空场地：斜向 (2,2)→(3,1) 正交邻格 (3,2) 与 (2,1) 都空 → allowed
    CHECK(game::player_move(gs, 1, -1) == game::ActionResult::Moved);
    CHECK(player_at(gs, 3, 1));

    // 人为把 (4,1) 与 (3,2) 都堵上 → (3,1)→(4,2) 双堵 → 禁止
    put_actor(gs, "wall1", 4, 1);
    put_actor(gs, "wall2", 3, 2);
    CHECK(game::player_move(gs, 1, 1) == game::ActionResult::Blocked);
    CHECK(player_at(gs, 3, 1));

    // 只堵一边（(4,1) 空、(3,2) 堵）→ 可切
    gs.actors.erase("wall1");
    CHECK(game::player_move(gs, 1, 1) == game::ActionResult::Moved);
    CHECK(player_at(gs, 4, 2));
    return true;
}

static bool test_diagonal_against_forest_wall() {
    auto gs = game::GameState{};
    auto g = tg::SceneAsset::load("assets/scenes/forest.json");
    REQUIRE(g.has_value());
    game::import_scene(gs, *g);
    // 内墙 rect(5,2)-(6,3)：墙格 (5,2)。玩家 (4,2) 斜向右上 (5,1)：
    // 正交邻格 (5,2)[墙] 与 (4,1)[空] → 一侧堵 → 允许切角（贴墙斜走可行）
    gs.actors["player"].pos = game::TilePos{4, 2};
    CHECK(game::player_move(gs, 1, -1) == game::ActionResult::Moved);
    CHECK(player_at(gs, 5, 1));
    return true;
}

static bool test_wait() {
    auto gs = game::GameState{};
    auto g = tg::SceneAsset::load("assets/scenes/forest.json");
    REQUIRE(g.has_value());
    game::import_scene(gs, *g);
    CHECK(gs.turn_count == 1);
    CHECK(game::player_wait(gs) == game::ActionResult::Waited);
    CHECK(gs.turn_count == 2);
    return true;
}

static bool test_no_player() {
    game::GameState gs;  // 空场（无玩家）
    CHECK(game::player_move(gs, 1, 0) == game::ActionResult::Invalid);
    CHECK(game::player_wait(gs) == game::ActionResult::Invalid);
    CHECK(game::player_move(gs, 0, 0) == game::ActionResult::Invalid);  // 无玩家也走 invalid
    return true;
}

// ── input buffer（对齐原版 input.lua addToKeyBuffer/processKeyBuffer） ──

static bool test_key_buffer_single_times_out() {
    game::InputBuffer buf;
    // 单键入队 → 不立即返回（等第二键或超时）
    CHECK(!game::input_buffer_push(buf, {0, -1}).has_value());
    CHECK(buf.size == 1);
    // 窗口耗尽 → 走第一个键
    auto step = game::input_buffer_tick(buf, game::InputBuffer::kWindow + 0.01f);
    REQUIRE(step.has_value());
    CHECK(step->dx == 0 && step->dy == -1);
    CHECK(buf.size == 0);
    return true;
}

static bool test_key_buffer_two_keys_diagonal() {
    game::InputBuffer buf;
    CHECK(!game::input_buffer_push(buf, {0, -1}).has_value());  // up
    // 0.18s 内按 right → 立即合成右上 (1,-1)
    auto step = game::input_buffer_push(buf, {1, 0});
    REQUIRE(step.has_value());
    CHECK(step->dx == 1 && step->dy == -1);
    CHECK(buf.size == 0);
    return true;
}

static bool test_key_buffer_opposite_falls_back_to_first() {
    game::InputBuffer buf;
    CHECK(!game::input_buffer_push(buf, {1, 0}).has_value());  // right
    // 再按 left → 抵消 (0,0) → 回落第一个键 right
    auto step = game::input_buffer_push(buf, {-1, 0});
    REQUIRE(step.has_value());
    CHECK(step->dx == 1 && step->dy == 0);
    return true;
}

static bool test_key_buffer_diagonal_key_immediate() {
    game::InputBuffer buf;
    // 斜向键（q/e/z/c）→ 清缓冲并立即走
    CHECK(!game::input_buffer_push(buf, {0, 1}).has_value());  // down 入队
    auto step = game::input_buffer_push(buf, {-1, 1});
    REQUIRE(step.has_value());
    CHECK(step->dx == -1 && step->dy == 1);
    CHECK(buf.size == 0);  // 已清空
    return true;
}

static bool test_key_buffer_flush() {
    game::InputBuffer buf;
    CHECK(!game::input_buffer_push(buf, {0, -1}).has_value());
    game::input_buffer_flush(buf);
    CHECK(buf.size == 0);
    CHECK(!game::input_buffer_tick(buf, 1.0f).has_value());  // 空队列无动作
    return true;
}

}  // namespace

int main() {
    const bool all_ok =
        test_import_forest() && test_move_consumes_turn() &&
        test_blocked_terrain_no_turn() && test_entity_blocked() &&
        test_diagonal_rule() && test_diagonal_against_forest_wall() &&
        test_wait() && test_no_player() && test_key_buffer_single_times_out() &&
        test_key_buffer_two_keys_diagonal() &&
        test_key_buffer_opposite_falls_back_to_first() &&
        test_key_buffer_diagonal_key_immediate() && test_key_buffer_flush();

    std::printf("game_core_test: %d checks, %d failures\n",
                tg_test::g_checks, tg_test::g_failures);
    if (all_ok && tg_test::g_failures == 0) {
        std::printf("[PASS] game_core_test\n");
        return 0;
    }
    std::printf("[FAIL] game_core_test\n");
    return 1;
}