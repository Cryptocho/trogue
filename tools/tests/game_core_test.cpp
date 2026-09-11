// game_core_test.cpp —— 回合制核心无窗口单测。
//
// 回合与移动：
//   1. import_scene：从 forest.json（palette 手写关卡）导入玩家/敌人、边界、回合复位
//   2. 玩家移动：成功移动消耗回合（+1）、撞墙（地形 solid）不消耗
//   3. 斜向切角：仅当两相邻正交格都被阻挡时禁止（对齐原版 canDiagonalMove）
//   4. 实体互斥：目标格被敌人占用 → blocked
//   5. wait：回合推进
//   6. 玩家不存在 → invalid（移动/等待都拒绝）
// AI / 规则 / 导航：
//   7. EventBus：priority 顺序 / off / emit 内 off 自身与嵌套 emit（快照语义）
//   8. nav：chebyshev / Bresenham LOS（端点不判定）/ A* 绕墙一步、围死 nullopt
//   9. AI 状态机：idle→alerted（停）→chasing、贴脸攻击（wire 序 DamageDealt→AbilityUsed）
//  10. 死亡与 GameOver：敌人延迟销毁、玩家死亡收尾不推进回合
//  11. 游走确定性：固定种子跨运行逐回合可复现
//  12. 冷却：TurnEnded 递减、punch 不登记 0 冷却
//  13. 惰性实体门控：coin 无 hp/无 AI 不行动
//
// 依赖 CWD = CMAKE_SOURCE_DIR（WORKING_DIRECTORY 设置）读取 assets/scenes/forest.json。

#include <cstdio>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

#include "test_util.hpp"
#include "ai.hpp"
#include "event_bus.hpp"
#include "game_core.hpp"
#include "nav.hpp"
#include "rules.hpp"

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

// ══════════════ AI / 规则 / 导航 ══════════════

// 便捷构造：带 AI 系统上下文的场景（forest + 规则绑定）。
// 构造函数不用 REQUIRE（宏含 return，构造函数禁用）；各测试先
// REQUIRE(rig.ok)。
struct AiRig {
    tg::expected<tg::SceneAsset, tg::Error> asset{tg::SceneAsset::load("assets/scenes/forest.json")};
    game::GameState gs;
    game::EventBus bus;
    game::RuleEngine rules;
    game::AiSystem ai;
    game::GameSystems sys{&bus, &rules, &ai};

    explicit AiRig(std::uint32_t seed = game::AiSystem::kDefaultSeed) : ai(seed) {
        if (asset) {
            game::import_scene(gs, *asset);
            rules.bind(gs, bus);
        }
    }
    bool ok() const { return asset.has_value(); }
};

// ── EventBus ──

bool test_event_bus_priority_and_off() {
    game::EventBus bus;
    std::vector<int> order;
    bus.on("E", [&](const tg::Json&) { order.push_back(10); }, 10);
    const auto h5 = bus.on("E", [&](const tg::Json&) { order.push_back(5); }, 5);
    bus.on("E", [&](const tg::Json&) { order.push_back(0); }, 0);
    bus.emit("E", tg::Json::object());
    CHECK((order == std::vector<int>{0, 5, 10}));  // priority 越小越先
    bus.off(h5);
    order.clear();
    bus.emit("E", tg::Json::object());
    CHECK((order == std::vector<int>{0, 10}));  // off 生效
    CHECK(bus.count("E") == 2);
    bus.off(0);  // 无效句柄 no-op
    CHECK(bus.count("E") == 2);
    return true;
}

bool test_event_bus_snapshot_reentrancy() {
    game::EventBus bus;
    int calls = 0;
    game::Subscription self = 0;
    self = bus.on("E", [&](const tg::Json&) {
        ++calls;
        bus.off(self);  // emit 内注销自身：快照语义下本次仍执行
    });
    bus.on("E", [&](const tg::Json&) { ++calls; });
    bus.emit("E", tg::Json::object());
    CHECK(calls == 2);
    bus.emit("E", tg::Json::object());
    CHECK(calls == 3);  // 第一个已注销
    // 嵌套 emit 同名事件（重入）不崩溃、不丢调用
    int nested = 0;
    bus.on("N", [&](const tg::Json&) {
        ++nested;
        if (nested == 1) bus.emit("N", tg::Json::object());
    });
    bus.emit("N", tg::Json::object());
    CHECK(nested == 2);
    return true;
}

// ── nav ──

bool test_nav_chebyshev_los() {
    CHECK(game::nav::chebyshev(0, 0, 3, 4) == 4);
    CHECK(game::nav::chebyshev(2, 2, 2, 2) == 0);
    CHECK(game::nav::chebyshev(5, 5, 2, 2) == 3);
    const std::function<bool(int, int)> no_walls = [](int, int) { return false; };
    CHECK(game::nav::has_line_of_sight(0, 0, 5, 0, no_walls));
    CHECK(game::nav::has_line_of_sight(0, 0, 4, 4, no_walls));   // 斜线
    CHECK(game::nav::has_line_of_sight(4, 4, 2, 2, no_walls));   // 反向
    // 一格墙挡住直线
    const std::function<bool(int, int)> one_wall = [](int x, int y) {
        return x == 3 && y == 0;
    };
    CHECK(!game::nav::has_line_of_sight(0, 0, 6, 0, one_wall));
    // 终点在墙内仍可见（端点不判定，对齐原版 coordinates.lua:212）
    CHECK(game::nav::has_line_of_sight(0, 0, 3, 0, one_wall));
    // 起点自身在墙内不受影响（起点不判定）
    const std::function<bool(int, int)> wall_at_start = [](int x, int y) {
        return x == 0 && y == 0;
    };
    CHECK(game::nav::has_line_of_sight(0, 0, 3, 0, wall_at_start));
    return true;
}

namespace {

// 生成 palette 模式临时场景 JSON（墙层由 is_wall 决定），供 A* 精确控场。
std::string make_scene_json(
    int w, int h, const std::function<bool(int, int)>& is_wall,
    const std::vector<std::tuple<std::string, std::string, int, int>>& ents) {
    std::string ground, walls;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            ground += "0,";
            walls += is_wall(x, y) ? "1," : "-1,";
        }
    }
    if (!ground.empty()) { ground.pop_back(); walls.pop_back(); }
    std::string s = R"({"format":"tro-scene","version":2,)";
    s += R"("meta":{"name":"tmp_nav","background":"#000000"},)";
    s += R"("tilemap":{"tile_width":16,"tile_height":16,)";
    s += R"("palette":["#000000","#ff0000"],"layers":[)";
    s += R"({"name":"ground","width":)" + std::to_string(w) +
         R"(,"height":)" + std::to_string(h) + R"(,"solid":false,"tiles":[)" +
         ground + "]},";
    s += R"({"name":"walls","width":)" + std::to_string(w) +
         R"(,"height":)" + std::to_string(h) + R"(,"solid":true,"tiles":[)" +
         walls + "]}]},";
    s += R"("entities":[)";
    for (std::size_t i = 0; i < ents.size(); ++i) {
        const auto& [id, type, tx, ty] = ents[i];
        if (i) s += ",";
        s += R"({"id":")" + id + R"(","type":")" + type + R"(","x":)" +
             std::to_string(tx * 16) + R"(,"y":)" + std::to_string(ty * 16) + "}";
    }
    s += "]}";
    return s;
}

bool write_temp_file(const std::string& path, const std::string& content) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::size_t n = std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
    return n == content.size();
}

}  // namespace

bool test_nav_astar_around_wall() {
    // 7x7：墙列 x=3（y=1..5），左右两室仅经 y=0/y=6 连通
    const auto json = make_scene_json(
        7, 7,
        [](int x, int y) { return x == 3 && y >= 1 && y <= 5; },
        {{"player", "player", 1, 3}, {"goblin_1", "goblin", 5, 3}});
    const std::string path = "_tmp_nav_test.json";
    REQUIRE(write_temp_file(path, json));
    auto asset = tg::SceneAsset::load(path);
    std::remove(path.c_str());
    REQUIRE(asset.has_value());
    game::GameState gs;
    game::import_scene(gs, *asset);

    // goblin(5,3) → player(1,3)：必须绕墙列；逐步 astar_step 应最终抵达
    const game::TilePos from{5, 3}, goal{1, 3};
    const std::function<bool(int, int)> no_block = [](int, int) { return false; };
    game::TilePos cur = from;
    bool arrived = false;
    for (int i = 0; i < 24 && !arrived; ++i) {
        const auto step = game::nav::astar_step(gs, cur, goal, no_block);
        REQUIRE(step.has_value());  // 路径存在（围死才会 nullopt）
        // 每步必为相邻格（chebyshev 1）且可通行（非墙）
        CHECK(game::nav::chebyshev(cur.x, cur.y, step->x, step->y) == 1);
        CHECK(!game::tile_solid_terrain(gs, step->x, step->y));
        cur = *step;
        if (cur == goal) arrived = true;
    }
    CHECK(arrived);

    // 目标格被其他敌人占据仍可作为寻路目标（blocked 注入方语义；此处
    // (1,3) 有玩家不挡——AI 侧同构，见 ai.cpp blocked lambda）
    return true;
}

bool test_nav_astar_unreachable() {
    // 5x5：全墙包裹 (1,1)，目标 (3,3) 不可达 → nullopt
    const auto json = make_scene_json(
        5, 5,
        [](int x, int y) {
            return !(x == 1 && y == 1);  // 仅 (1,1) 通行
        },
        {{"player", "player", 3, 3}, {"goblin_1", "goblin", 1, 1}});
    const std::string path = "_tmp_nav_test.json";
    REQUIRE(write_temp_file(path, json));
    auto asset = tg::SceneAsset::load(path);
    std::remove(path.c_str());
    REQUIRE(asset.has_value());
    game::GameState gs;
    game::import_scene(gs, *asset);
    const std::function<bool(int, int)> no_block = [](int, int) { return false; };
    CHECK(!game::nav::astar_step(gs, {1, 1}, {3, 3}, no_block).has_value());
    CHECK(!game::nav::astar_step(gs, {1, 1}, {1, 1}, no_block).has_value());  // 自身
    return true;
}

// ── AI 状态机（forest：内墙 rect(5,2)-(6,3)） ──

bool test_ai_state_transitions() {
    AiRig rig;
    REQUIRE(rig.ok());
    auto& gs = rig.gs;
    // goblin_1 → (4,4)：chebyshev((4,4),(2,2))=2，LOS 畅通
    gs.actors["goblin_1"].pos = game::TilePos{4, 4};

    // wait ×1：idle→alerted（记录回合与 target；当回合「停」不移动）
    CHECK(game::player_wait(gs, rig.sys) == game::ActionResult::Waited);
    CHECK(gs.actors["goblin_1"].ai.state == game::AiPhase::alerted);
    CHECK(gs.actors["goblin_1"].ai.alerted_turn == 1);
    CHECK(gs.actors["goblin_1"].ai.has_target);
    CHECK((gs.actors["goblin_1"].ai.target == game::TilePos{2, 2}));
    CHECK((gs.actors["goblin_1"].pos == game::TilePos{4, 4}));
    CHECK(gs.turn_count == 2);

    // wait ×2：alerted→chasing（ALERT_DELAY=1），且 dist=2 → A* 一步逼近
    CHECK(game::player_wait(gs, rig.sys) == game::ActionResult::Waited);
    CHECK(gs.actors["goblin_1"].ai.state == game::AiPhase::chasing);
    CHECK((gs.actors["goblin_1"].pos == game::TilePos{3, 3}));  // 对角最优唯一
    CHECK(gs.turn_count == 3);

    // 玩家瞬移到 (7,2)：chebyshev((3,3),(7,2))=4 ≤ 5，但 Bresenham 中间格
    // (5,3) 是内墙（墙层实测 {(5,2),(6,2),(5,3),(6,3)}）→ 真丢视线；
    // 未达记忆位 (2,2) → 保持 chasing，且 target 不刷新（仍为最后可见位）。
    gs.actors["player"].pos = game::TilePos{7, 2};
    CHECK(game::player_wait(gs, rig.sys) == game::ActionResult::Waited);
    CHECK(gs.actors["goblin_1"].ai.state == game::AiPhase::chasing);  // 未到记忆位
    CHECK((gs.actors["goblin_1"].ai.target == game::TilePos{2, 2}));  // 不刷新
    return true;
}

bool test_ai_lose_sight_reach_memory() {
    AiRig rig;
    REQUIRE(rig.ok());
    auto& gs = rig.gs;
    // goblin (4,4) chasing，target=(2,2)（已在上回合记录）；玩家瞬移到
    // (4,2)——(4,4)→(4,2) 直线上无墙 → 仍可见，target 刷新为 (4,2)。
    // 换方案：玩家瞬移出视野（chebyshev>5）：(10,2)。
    gs.actors["goblin_1"].pos = game::TilePos{4, 4};
    gs.actors["goblin_1"].ai.state = game::AiPhase::chasing;
    gs.actors["goblin_1"].ai.has_target = true;
    gs.actors["goblin_1"].ai.target = game::TilePos{4, 4};  // 记忆位 = 当前位
    gs.actors["player"].pos = game::TilePos{10, 10};        // 远离 → 不可见

    CHECK(game::player_wait(gs, rig.sys) == game::ActionResult::Waited);
    // 丢视线且已抵达记忆位 → idle（清 target）
    CHECK(gs.actors["goblin_1"].ai.state == game::AiPhase::idle);
    CHECK(!gs.actors["goblin_1"].ai.has_target);
    return true;
}

bool test_ai_attack_pipeline_order() {
    AiRig rig;
    REQUIRE(rig.ok());
    auto& gs = rig.gs;
    // goblin (3,3) 贴脸（chebyshev 1），预置 chasing（跳过 alerted 延迟）
    gs.actors["goblin_1"].pos = game::TilePos{3, 3};
    gs.actors["goblin_1"].ai.state = game::AiPhase::chasing;
    gs.actors["goblin_1"].ai.has_target = true;
    gs.actors["goblin_1"].ai.target = game::TilePos{2, 2};

    // 记录 wire 事件序（DamageDealt 先于 AbilityUsed）
    std::vector<std::string> seq;
    rig.bus.on("DamageDealt", [&](const tg::Json& j) {
        seq.push_back("DamageDealt@" + j.at("target").get<std::string>());
    });
    rig.bus.on("AbilityUsed", [&](const tg::Json& j) {
        seq.push_back("AbilityUsed@" + j.at("entity").get<std::string>());
    });

    const int hp_before = gs.player()->hp->cur;  // 100
    CHECK(game::player_wait(gs, rig.sys) == game::ActionResult::Waited);
    CHECK(seq.size() == 2);
    CHECK(seq[0] == "DamageDealt@player");    // 先结算
    CHECK(seq[1] == "AbilityUsed@goblin_1");  // 后发布（原版顺序）
    CHECK(gs.player()->hp->cur == hp_before - 5);  // damage_physical = 5
    // punch cooldown=0：不登记冷却条目（对齐原版仅 >0 才写）
    CHECK(gs.actors["goblin_1"].cooldowns.empty());
    CHECK(gs.turn_count == 2);  // 正常推进（未 GameOver）
    return true;
}

bool test_ai_death_and_gameover() {
    AiRig rig;
    REQUIRE(rig.ok());
    auto& gs = rig.gs;
    // 玩家 hp 压到 5，goblin 贴脸 chasing → 一下打死
    gs.actors["goblin_1"].pos = game::TilePos{3, 2};
    gs.actors["goblin_1"].ai.state = game::AiPhase::chasing;
    gs.actors["goblin_1"].ai.has_target = true;
    gs.actors["goblin_1"].ai.target = game::TilePos{2, 2};
    gs.player()->hp->cur = 5;

    bool turn_ended = false;
    bool player_died = false;
    rig.bus.on("TurnEnded", [&](const tg::Json&) { turn_ended = true; });
    rig.bus.on("EntityDied",
               [&](const tg::Json& j) {
                   if (j.at("entity").get<std::string>() == "player")
                       player_died = true;
               });

    CHECK(game::player_wait(gs, rig.sys) == game::ActionResult::Waited);
    CHECK(player_died);
    CHECK(gs.phase == game::Phase::GameOver);
    CHECK(gs.turn_count == 1);      // 收尾不 +1
    CHECK(!turn_ended);             // 不发 TurnEnded
    CHECK(gs.player() != nullptr);  // 玩家不 despawn
    // GameOver 下玩家行动被拒（GameSystems 版入口 phase 守卫）
    CHECK(game::player_move(gs, rig.sys, 1, 0) == game::ActionResult::Invalid);
    CHECK(game::player_wait(gs, rig.sys) == game::ActionResult::Invalid);
    return true;
}

bool test_enemy_death_deferred_despawn() {
    AiRig rig;
    REQUIRE(rig.ok());
    auto& gs = rig.gs;
    // 规则层直驱（测试允许）：玩家 punch 打 5 hp 的 goblin_2（(3,2) 邻格）
    gs.actors["goblin_2"].pos = game::TilePos{3, 2};
    gs.actors["goblin_2"].hp = game::Hp{5, 25};
    CHECK(gs.actors["goblin_2"].ai.state == game::AiPhase::idle);
    CHECK(rig.rules.try_use("player", "punch", game::TilePos{3, 2}));
    // 死亡：延迟销毁标记（对齐原版 ShouldDespawn），尚未移除
    CHECK(gs.pending_despawn.size() == 1);
    CHECK(gs.pending_despawn[0] == "goblin_2");
    CHECK(gs.actors.count("goblin_2") == 1);
    // 收尾统一清除
    CHECK(game::player_wait(gs, rig.sys) == game::ActionResult::Waited);
    CHECK(gs.actors.count("goblin_2") == 0);
    return true;
}

bool test_rules_cooldown_and_failures() {
    AiRig rig;
    REQUIRE(rig.ok());
    auto& gs = rig.gs;
    // 注册 cooldown=2 的测试能力（punch 之外；范围校验加固）
    rig.rules.abilities["test_cool"] = game::AbilityDef{"test_cool", 2, 1, {}};
    gs.actors["goblin_2"].pos = game::TilePos{3, 2};  // 玩家邻格

    CHECK(rig.rules.try_use("player", "test_cool", game::TilePos{3, 2}));
    CHECK(gs.player()->cooldowns["test_cool"] == 2);
    CHECK(!rig.rules.can_use(*gs.player(), "test_cool"));
    // 冷却中再放 → 失败 + AbilityUseFailed
    std::string fail_reason;
    rig.bus.on("AbilityUseFailed",
               [&](const tg::Json& j) { fail_reason = j.at("reason").get<std::string>(); });
    CHECK(!rig.rules.try_use("player", "test_cool", game::TilePos{3, 2}));
    CHECK(fail_reason == "cooldown");
    // 两次 TurnEnded（冷却 -1/回合）→ 恢复可用
    CHECK(game::player_wait(gs, rig.sys) == game::ActionResult::Waited);
    CHECK(gs.player()->cooldowns["test_cool"] == 1);
    CHECK(game::player_wait(gs, rig.sys) == game::ActionResult::Waited);
    CHECK(gs.player()->cooldowns.count("test_cool") == 0);  // 归零即移除
    CHECK(rig.rules.can_use(*gs.player(), "test_cool"));

    // 射程加固：目标超 range → out_of_range（不扣冷却不发请求）
    gs.actors["goblin_3"].pos = game::TilePos{6, 2};  // chebyshev 4 > 1
    CHECK(!rig.rules.try_use("player", "test_cool", game::TilePos{6, 2}));
    CHECK(fail_reason == "out_of_range");
    return true;
}

bool test_ai_wander_deterministic() {
    // 固定种子跨运行复现（goblin_2 在 (4,13)，距玩家 chebyshev 11 > 5 → 恒 idle 游走）
    std::vector<game::TilePos> run1, run2;
    for (int run = 0; run < 2; ++run) {
        AiRig rig(20260909u);  // 同种子
        REQUIRE(rig.ok());
        auto& gs = rig.gs;
        for (int t = 0; t < 6; ++t) {
            CHECK(game::player_wait(gs, rig.sys) == game::ActionResult::Waited);
            if (run == 0)
                run1.push_back(gs.actors["goblin_2"].pos);
            else
                run2.push_back(gs.actors["goblin_2"].pos);
        }
    }
    CHECK(run1.size() == 6);
    CHECK(run1 == run2);  // 同种子逐回合位置完全一致
    return true;
}

bool test_lazy_entity_gating() {
    // coin 类惰性实体：无 hp / 无 AI / 敌方阶段不行动（门控）
    AiRig rig;
    REQUIRE(rig.ok());
    auto& gs = rig.gs;
    game::Actor coin;
    coin.id = "coin_x";
    coin.type = "coin";  // 非战斗原型
    coin.pos = game::TilePos{4, 4};
    gs.actors["coin_x"] = coin;
    CHECK(!game::is_combat_archetype("coin"));
    CHECK(!gs.actors["coin_x"].hp.has_value());
    // 6 个回合：coin 纹丝不动（AI 只遍历战斗原型）
    for (int t = 0; t < 6; ++t)
        CHECK(game::player_wait(gs, rig.sys) == game::ActionResult::Waited);
    CHECK((gs.actors["coin_x"].pos == game::TilePos{4, 4}));
    // goblin 有 hp（原型表），player 100
    CHECK(gs.actors["goblin_1"].hp.has_value());
    CHECK(gs.actors["goblin_1"].hp->max == 25);
    CHECK(gs.player()->hp->max == 100);
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
        test_key_buffer_diagonal_key_immediate() && test_key_buffer_flush() &&
        // ── AI / 规则 / 导航 ──
        test_event_bus_priority_and_off() &&
        test_event_bus_snapshot_reentrancy() && test_nav_chebyshev_los() &&
        test_nav_astar_around_wall() && test_nav_astar_unreachable() &&
        test_ai_state_transitions() && test_ai_lose_sight_reach_memory() &&
        test_ai_attack_pipeline_order() && test_ai_death_and_gameover() &&
        test_enemy_death_deferred_despawn() &&
        test_rules_cooldown_and_failures() && test_ai_wander_deterministic() &&
        test_lazy_entity_gating();

    std::printf("game_core_test: %d checks, %d failures\n",
                tg_test::g_checks, tg_test::g_failures);
    if (all_ok && tg_test::g_failures == 0) {
        std::printf("[PASS] game_core_test\n");
        return 0;
    }
    std::printf("[FAIL] game_core_test\n");
    return 1;
}