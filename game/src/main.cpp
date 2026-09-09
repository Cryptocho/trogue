// main.cpp —— 里程碑 6：回合制 roguelike demo（移植 trogue-orign 最小闭环）。
//
// 仅使用 trogue/*.hpp 公共 API + game_core（game 层纯逻辑）。
// demo 演示：
//   - 回合制：玩家移动（8 向 + 斜切约束）→ 敌方回合（静止策略）→ 回合 +1
//   - 键盘：WASD/方向键 4 向、Q/E/Z/C 斜向（对齐原版 input.lua KEY_MOVEMENTS）、
//     空格等待；视觉平滑插值（帧间 lerp）
//   - 渲染：render_scene 画 tile 层；实体按 (y, z) 排序后显式绘制（色块/sprite）
//   - 热重载：Watcher + F5 + IPC reload（candidate load → 帧外 swap，位置+回合保留）
//   - IPC 命令 handler（全部命令语义在 game；engine 只传 ping）
//   - 新增 IPC 回合命令 turn/move/wait（docs/plan-6.md §3.4；场景无关，非视觉 Agent 可驱动）
//   - 截图（game 排队，帧后 ExportImage）、log、quit
//
// 命令归属表见 docs/plan-5.6.md §1 与 docs/plan-6.md §3.4。
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <raylib.h>
#include <rlgl.h>  // rlDrawRenderBatchActive（截图前强制 flush 渲染批）

#include "trogue/trogue.hpp"
#include "game_core.hpp"

namespace {

// ── 骨架回归（启动自检：协程/值类型/错误载体/资产，证明引擎可用） ──

tg::task<int> compute_answer() { co_return 6 * 7; }

tg::task<> wait_for_signal(tg::single_consumer_event& ev) { co_await ev; }

tg::generator<int> count_to_three() {
    co_yield 1;
    co_yield 2;
    co_yield 3;
}

int skeleton_regression() {
    // task
    tg::task<int> ans = compute_answer();
    if (ans.done()) return 1;
    ans.start();
    if (!ans.done() || std::move(ans).result() != 42) return 1;
    // event
    tg::single_consumer_event ev(false);
    tg::task<> w = wait_for_signal(ev);
    w.start();
    if (w.done()) return 1;
    ev.set();
    if (!w.done() || ev.is_set()) return 1;
    // generator
    int sum = 0;
    for (int v : count_to_three()) sum += v;
    if (sum != 6) return 1;
    // error
    const tg::ErrorOr<int> bad = tl::unexpected(
        tg::Error{tg::ErrorCode::kParseError, "骨架自检失败路径"});
    if (bad) return 1;
    // 资产回归：demo（palette）+ test（atlas）+ soldier（bare+动画）+ forest（回合）
    auto r1 = tg::SceneAsset::load("assets/scenes/demo.json");
    if (!r1) return 1;
    if (tg::is_solid_at(*r1, tg::Vec2{8.0f, 8.0f}) != tg::TileQueryResult::solid)
        return 1;
    auto r2 = tg::SceneAsset::load("assets/scenes/test.json");
    if (!r2 || r2->layer_count() == 0) return 1;
    auto r3 = tg::SceneAsset::load("assets/scenes/soldier_animated_sprite_2d.json");
    if (!r3 || r3->animation_set_count() != 1) return 1;
    const tg::AnimationSet& set = r3->animation_set(0);
    if (set.clip_count() != 7 || !set.has_clip("idle")) return 1;
    auto r4 = tg::SceneAsset::load("assets/scenes/forest.json");
    if (!r4 || r4->layer_count() < 2 || r4->entity_count() < 4) return 1;
    return 0;
}

// ── 工具：颜色 ──

std::string color_hex(const tg::Color& c) {
    const char* d = "0123456789abcdef";
    auto h2 = [&](std::uint8_t x) {
        std::string s;
        s += d[x >> 4];
        s += d[x & 15];
        return s;
    };
    return "#" + h2(c.r) + h2(c.g) + h2(c.b);
}

tg::Color parse_color(const std::string& hex) {
    tg::Color c{255, 255, 255, 255};
    if (hex.size() == 7 && hex[0] == '#') {
        auto hexv = [&](char x) -> int {
            if (x >= '0' && x <= '9') return x - '0';
            if (x >= 'a' && x <= 'f') return x - 'a' + 10;
            if (x >= 'A' && x <= 'F') return x - 'A' + 10;
            return -1;
        };
        auto byte = [&](int i) -> int {
            const int h = hexv(hex[i]), l = hexv(hex[i + 1]);
            return (h < 0 || l < 0) ? 0 : h * 16 + l;
        };
        c.r = static_cast<std::uint8_t>(byte(1));
        c.g = static_cast<std::uint8_t>(byte(3));
        c.b = static_cast<std::uint8_t>(byte(5));
    }
    return c;
}

// ── actor → JSON 快照（inspector 式：逻辑坐标 + transform 视觉视图） ──
// 设计（用户 2026-09-09 拍板）：IPC 应像 inspector 一样能观察实体，
// 且机制在 game 层可扩展（未来 ECS 组件观察走同一注入点）。
//   - 既有字段 id/type/x/y/w/h/color/z/sprite 不变（wire 兼容）：
//     x/y 为逻辑格像素（静态断言依赖）；
//   - 新增 transform：{ visual:[vx,vy], moving } —— 玩家移动中 vx/vy 为
//     引擎 tween 插值位置（浮点），静止时 == 逻辑格像素；其余实体无插值，
//     visual == 逻辑位置。Agent 凭"静止时 visual==x/y"即可数值发现错位/抖动。
//   - 扩展点：Demo::extra_entity_fields（std::function）可在快照上追加任意
//     字段（如未来 ECS 组件的观察输出），未注册则为空。
struct Demo;  // 前向（Demo 定义在下方；此处仅声明，定义在 Demo 之后）
void entity_visual(const Demo& d, const game::Actor& a,
                   float& vx, float& vy, bool& moving);
tg::Json actor_to_json(const Demo& d, const game::Actor& a);

// ── demo 应用状态 ──

struct Demo {
    std::unique_ptr<tg::SceneAsset> asset;   // 只读资产（帧外 swap）
    game::GameState gs;                      // game_core：actor+回合唯一所有权
    int reloads = 0;

    tg::TweenManager tween;
    tg::AnimationPlayer anim;                // 绑定首个动画集（示意；可共存）
    bool has_anim = false;

    int window_w = 960, window_h = 540;
    tg::Vec2 cam{0, 0};
    bool quit = false;

    tg::Ipc ipc;
    int port = tg::kIpcPortDefault;

    bool shot_requested = false;
    std::string shot_path;

    tg::Watcher watcher;
    std::string scene_path = "assets/scenes/demo.json";  // 当前场景（reload 目标）

    // ── 玩家视觉移动：**由引擎 tg::TweenManager 驱动**（不重复造轮子） ──
    // 每次移动成功后 add_vec2(当前位置 → 新格, 0.12s quad_out)，update 回调
    // 写 view_x/view_y；播完精确落格（tween 终值=目标整数像素 → 静止时与 tile
    // 网格严格对齐，无指数趋近残差、无像素抖动）。引擎 quad_out=1-(1-t)²=2t-t²
    // 与原版 tween_system.lua 的 easeOutQuad(t)=-t*(t-2) 完全一致。
    float view_x = 0, view_y = 0;       // 显示（插值）位置，由引擎 tween 写入
    bool view_init = false;
    tg::TweenManager::Id view_tween = 0;  // 当前移动 tween id（用于打断/清理）

    // ── 输入缓冲（对齐原版 input.lua：0.18s 窗口，双键合成对角） ──
    // 逻辑在 game_core（InputBuffer + 自由函数，可无窗口单测）；main 只喂键。
    game::InputBuffer input;

    // ── IPC 实体快照的 game 层扩展注入点（inspector 可扩展性） ──
    // 未来 ECS 组件观察等自定义属性在这里注册：回调在生成实体 JSON 快照时被
    // 调用，可向快照追加任意字段（如 components: {...}）。
    std::function<void(tg::Json&, const game::Actor&)> extra_entity_fields;
};

// 实体视觉状态：玩家取引擎 tween 插值位置（未在动画时 == 逻辑格像素）；
// 其他实体无插值，视觉位置就是逻辑格像素。（供 IPC transform 视图使用）
void entity_visual(const Demo& d, const game::Actor& a, float& vx, float& vy,
                   bool& moving) {
    vx = static_cast<float>(a.pos.x * game::kTileSize);
    vy = static_cast<float>(a.pos.y * game::kTileSize);
    moving = false;
    if (a.is_player) {
        vx = d.view_x;
        vy = d.view_y;
        moving = d.tween.alive(d.view_tween);
    }
}

// actor → inspector 式快照（见头部注释）：逻辑字段 + transform 视图 + 扩展点
tg::Json actor_to_json(const Demo& d, const game::Actor& a) {
    tg::Json j = tg::Json::object();
    j["id"] = a.id;
    j["type"] = a.type;
    j["x"] = a.pos.x * game::kTileSize;
    j["y"] = a.pos.y * game::kTileSize;
    j["w"] = game::kTileSize;
    j["h"] = game::kTileSize;
    j["color"] = color_hex(a.color);
    // transform（inspector 视图）：visual 位置 + 是否移动动画中
    {
        float vx = 0, vy = 0;
        bool moving = false;
        entity_visual(d, a, vx, vy, moving);
        tg::Json t = tg::Json::object();
        t["visual"] = {vx, vy};
        t["moving"] = moving;
        j["transform"] = t;
    }
    if (a.z != 0) j["z"] = a.z;
    if (a.sprite.has) {
        tg::Json s = tg::Json::object();
        if (a.sprite.tileset_index >= 0) {
            s["tileset"] = "@" + std::to_string(a.sprite.tileset_index)
                         + ":" + std::to_string(a.sprite.tile);
            s["tile"] = a.sprite.tile;
        } else {
            s["texture"] = a.sprite.texture;
            if (a.sprite.region.w > 0)
                s["region"] = {a.sprite.region.x, a.sprite.region.y,
                               a.sprite.region.w, a.sprite.region.h};
            if (a.sprite.offset.x != 0 || a.sprite.offset.y != 0)
                s["offset"] = {a.sprite.offset.x, a.sprite.offset.y};
        }
        j["sprite"] = s;
    }
    // game 层扩展注入点（默认空；未来 ECS 组件观察在此注册）
    if (d.extra_entity_fields) d.extra_entity_fields(j, a);
    return j;
}

// candidate load → 帧外 swap（失败保留旧 asset/旧 state，失败安全）
void reload_scene(Demo& d, const std::string& path) {
    auto loaded = tg::SceneAsset::load(path);
    if (!loaded) {
        TraceLog(LOG_WARNING, "[demo] 加载失败（保留旧场景）: %s",
                 loaded.error().message.c_str());
        return;
    }
    // 保留玩家运行时位置与回合数（game 策略：reload 后玩家位置/回合不变；
    // 其它 actor 全部按新 descriptor 重建）。旧 C demo 曾有等价行为。
    std::optional<game::TilePos> keep_player;
    if (const game::Actor* p = d.gs.player()) keep_player = p->pos;
    const int keep_turn = d.gs.turn_count;

    d.anim.bind(tg::AnimationSet{});   // 解绑旧播放器（旧 asset 即将析构）
    d.asset = std::make_unique<tg::SceneAsset>(std::move(*loaded));
    game::import_scene(d.gs, *d.asset);
    if (keep_player) {
        if (game::Actor* p = d.gs.player()) p->pos = *keep_player;
    }
    d.gs.turn_count = keep_turn;       // 回合数跨 reload 保留（游戏进度）
    d.tween.cancel_all();              // 打断进行中的移动 tween（旧场景视觉位置失效）
    d.view_init = false;               // 重新追踪玩家视觉位置
    d.view_tween = 0;
    game::input_buffer_flush(d.input); // 清空未决输入（场景已换）
    ++d.reloads;
    d.has_anim = d.asset && d.asset->animation_set_count() > 0;
    if (d.has_anim) {
        d.anim.bind(d.asset->animation_set(0));
        if (!d.anim.play("idle")) d.anim.play("walk");
    }
    TraceLog(LOG_INFO, "[demo] 场景已交换（reloads=%d）", d.reloads);
}

// ── 玩家移动 + 视觉 tween（引擎 TweenManager 驱动，不重复造轮子） ──
// 原版语义（src/systems/input.lua + tween_system.lua）：
//   - 斜向键（q/e/z/c）→ 清缓冲并**立即**走格；
//   - 4 向键 → 塞进 keyBuffer（≤2），打开 0.18s 计时器；第 2 键立刻触发合成；
//   - 计时器到点（单键）→ 走那一格；
//   - 双键合成：dx/dy 各自 clamp 到 [-1,1]（如 w+d → 右上）；合并为 (0,0)
//     （如 w+s 反向）→ 走第一个键方向；
//   - 移动视觉：引擎 add_vec2 + quad_out（0.12s），播完精确落格。
// 以上缓冲逻辑在 game_core（InputBuffer 自由函数，可无窗口单测）；
// 本层只把 raylib 按键喂进缓冲，并把成功移动接到引擎 tween。
void handle_move(Demo& d, game::Dir m) {
    // 原版 handleMove 不查回合门控（缓冲触发无守卫，回合已由按键入口保证）；
    // player_move 内部自处理 invalid/blocked，无副作用风险。
    const auto r = game::player_move(d.gs, m.dx, m.dy);
    if (r != game::ActionResult::Moved || !d.gs.player()) return;

    // 移动成功：用引擎 tween 从当前显示位置滑到新逻辑格。
    // from = 当前 view（若上一个 tween 未播完则从其当前值续滑，保证连续无跳变）；
    // to = 新逻辑格像素（16 的整数倍，tween 终值精确=整数 → 静止严格对齐网格）。
    const float tx = static_cast<float>(d.gs.player()->pos.x * game::kTileSize);
    const float ty = static_cast<float>(d.gs.player()->pos.y * game::kTileSize);
    d.tween.cancel(d.view_tween);  // 打断上一个（无则 no-op）
    d.view_tween = d.tween.add_vec2(
        tg::Vec2{d.view_x, d.view_y}, tg::Vec2{tx, ty},
        tg::TweenSpec{game::kMoveDuration, 0.0, 0, tg::Easing::quad_out},
        [&d](const tg::TweenManager::Sample<tg::Vec2>& s) {
            d.view_x = s.value.x;  // update 回调写显示位置（引擎采样）
            d.view_y = s.value.y;
        },
        [&d, tx, ty]() {  // 完成：精确落格（防御性，与终值一致）
            d.view_x = tx;
            d.view_y = ty;
        });
}

// 每帧：喂按键进缓冲（原版 love.keypressed 语义）
void poll_key_presses(Demo& d) {
    if (d.gs.phase != game::Phase::PlayerTurn || !d.gs.player()) return;

    struct KeyDir { int key; game::Dir dir; };
    static const KeyDir kMoves[] = {
        {KEY_W, {0, -1}},   {KEY_UP, {0, -1}},   {KEY_S, {0, 1}},
        {KEY_DOWN, {0, 1}}, {KEY_A, {-1, 0}},    {KEY_LEFT, {-1, 0}},
        {KEY_D, {1, 0}},    {KEY_RIGHT, {1, 0}}, {KEY_Q, {-1, -1}},
        {KEY_E, {1, -1}},   {KEY_Z, {-1, 1}},    {KEY_C, {1, 1}},
    };
    for (const auto& km : kMoves) {
        if (IsKeyPressed(km.key)) {
            // push 可能立即返回一步（斜向 / 双键合成）；落入空则等超时或第二键
            if (auto step = game::input_buffer_push(d.input, km.dir))
                handle_move(d, *step);
            return;
        }
    }
    if (IsKeyPressed(KEY_SPACE)) {
        game::input_buffer_flush(d.input);
        (void)game::player_wait(d.gs);
    }
}

// 每帧：缓冲计时（对齐原版 update()：窗口耗尽 → 触发单键走格）
void tick_input_buffer(Demo& d, float dt) {
    if (auto step = game::input_buffer_tick(d.input, dt))
        handle_move(d, *step);
}

// 玩家视觉位置：无 tween 时（静止）与逻辑格严格一致；有 tween 时由引擎采样写入
void init_player_view_if_needed(Demo& d) {
    if (d.view_init || !d.gs.player()) return;
    d.view_x = static_cast<float>(d.gs.player()->pos.x * game::kTileSize);
    d.view_y = static_cast<float>(d.gs.player()->pos.y * game::kTileSize);
    d.view_init = true;
}

// ── 回合命令数据 ──

tg::Json turn_json(const Demo& d) {
    tg::Json j = tg::Json::object();
    j["phase"] = d.gs.phase == game::Phase::PlayerTurn ? "player" : "enemy";
    j["turn_count"] = d.gs.turn_count;
    tg::Json en = tg::Json::array();
    const game::Actor* player = nullptr;
    for (const auto& [id, a] : d.gs.actors) {
        if (a.is_player) player = &a;
        else en.push_back(actor_to_json(d, a));
    }
    j["player"] = player ? actor_to_json(d, *player) : tg::Json(nullptr);
    j["enemies"] = en;
    return j;
}

// ── IPC handler（全部命令归属 game；engine 只传 ping） ──

tg::IpcStatus ipc_handler(Demo& d, const std::string& cmd, const tg::Json& req,
                          std::optional<tg::Json>& data, std::string& error) {
    if (cmd == "help") {
        static const char* cmds[] = {
            "ping", "help", "status", "list_entities", "get_entity",
            "query_entities", "set_entity", "spawn", "despawn",
            "layers", "solid_at", "get_tile", "reload", "screenshot",
            "log", "quit", "turn", "move", "wait",
            "subscribe", "unsubscribe", "connections", "events",
        };
        tg::Json arr = tg::Json::array();
        for (const char* c : cmds) arr.push_back(c);
        data = tg::Json::object();
        (*data)["commands"] = arr;
        return tg::IpcStatus::handled;
    }
    if (cmd == "status") {
        data = tg::Json::object();
        (*data)["scene"] = d.asset ? std::string(d.asset->name()) : "";
        (*data)["reloads"] = d.reloads;
        (*data)["entities"] = static_cast<int>(d.gs.actors.size());
        (*data)["fps"] = GetFPS();
        (*data)["uptime_s"] = std::lround(GetTime());
        (*data)["port"] = d.port;
        return tg::IpcStatus::handled;
    }
    if (cmd == "turn") {
        data = turn_json(d);
        return tg::IpcStatus::handled;
    }
    if (cmd == "move") {
        if (d.gs.phase != game::Phase::PlayerTurn || !d.gs.player()) {
            error = "not player turn";
            return tg::IpcStatus::error;
        }
        // 类型安全：dx/dy 必须是整数（-1..1 由 game_core 判定）；float 会被
        // nlohmann get<int> 静默截断，故先拒绝（计划 §3.4：整数值各在 -1..1）。
        if (!req.contains("dx") || !req.contains("dy") ||
            !req["dx"].is_number_integer() || !req["dy"].is_number_integer()) {
            error = "move needs integer dx/dy";
            return tg::IpcStatus::error;
        }
        const int dx = req["dx"].get<int>();
        const int dy = req["dy"].get<int>();
        const auto r = game::player_move(d.gs, dx, dy);
        const char* result = "moved";
        if (r == game::ActionResult::Blocked) result = "blocked";
        else if (r == game::ActionResult::Invalid) result = "invalid";
        else if (r == game::ActionResult::Waited) result = "waited";
        tg::Json out = tg::Json::object();
        out["result"] = result;
        out["turn"] = turn_json(d);
        data = out;
        return tg::IpcStatus::handled;
    }
    if (cmd == "wait") {
        if (d.gs.phase != game::Phase::PlayerTurn || !d.gs.player()) {
            error = "not player turn";
            return tg::IpcStatus::error;
        }
        (void)game::player_wait(d.gs);
        tg::Json out = tg::Json::object();
        out["result"] = "waited";
        out["turn"] = turn_json(d);
        data = out;
        return tg::IpcStatus::handled;
    }
    if (cmd == "list_entities") {
        tg::Json arr = tg::Json::array();
        for (const auto& [id, a] : d.gs.actors) arr.push_back(actor_to_json(d, a));
        data = tg::Json::object();
        (*data)["entities"] = arr;
        (*data)["count"] = static_cast<int>(d.gs.actors.size());
        return tg::IpcStatus::handled;
    }
    if (cmd == "get_entity") {
        auto it = d.gs.actors.find(req.value("id", ""));
        if (it == d.gs.actors.end()) {
            error = "no such entity";
            return tg::IpcStatus::error;
        }
        data = tg::Json::object();
        (*data)["entity"] = actor_to_json(d, it->second);
        return tg::IpcStatus::handled;
    }
    if (cmd == "query_entities") {
        const std::string type_f = req.value("type", "");
        tg::Json arr = tg::Json::array();
        auto push = [&](const game::Actor& a) {
            if (type_f.empty() || a.type == type_f) arr.push_back(actor_to_json(d, a));
        };
        if (req.contains("radius")) {  // 半径模式优先
            if (!req["x"].is_number() || !req["y"].is_number() ||
                !req["radius"].is_number()) {
                error = "radius needs numeric x/y/radius";
                return tg::IpcStatus::error;
            }
            const float qx = req["x"].get<float>(), qy = req["y"].get<float>();
            const float rad = req["radius"].get<float>();
            for (const auto& [id, a] : d.gs.actors) {
                const float cx = a.pos.x * game::kTileSize + game::kTileSize / 2.0f;
                const float cy = a.pos.y * game::kTileSize + game::kTileSize / 2.0f;
                const float dx = cx - qx, dy = cy - qy;
                if (rad >= 0 && dx * dx + dy * dy <= rad * rad) push(a);
            }
        } else if (req.contains("rect")) {
            const auto& r = req["rect"];
            if (!r.is_array() || r.size() != 4 || !r[0].is_number() ||
                !r[1].is_number() || !r[2].is_number() || !r[3].is_number()) {
                error = "rect needs [x,y,w,h] numbers";
                return tg::IpcStatus::error;
            }
            const float rx = r[0].get<float>(), ry = r[1].get<float>();
            const float rw = r[2].get<float>(), rh = r[3].get<float>();
            const float tw = game::kTileSize;
            for (const auto& [id, a] : d.gs.actors) {
                const float ax = a.pos.x * tw, ay = a.pos.y * tw;
                if (ax < rx + rw && ax + tw > rx &&
                    ay < ry + rh && ay + tw > ry)
                    push(a);
            }
        } else {
            error = "query needs x/y/radius or rect";
            return tg::IpcStatus::error;
        }
        data = tg::Json::object();
        (*data)["entities"] = arr;
        (*data)["count"] = static_cast<int>(arr.size());
        return tg::IpcStatus::handled;
    }
    if (cmd == "set_entity") {
        auto it = d.gs.actors.find(req.value("id", ""));
        if (it == d.gs.actors.end()) {
            error = "no such entity";
            return tg::IpcStatus::error;
        }
        game::Actor& a = it->second;
        const bool is_player = a.is_player;
        // 类型安全：字段存在才动；类型不符 → 报错而非 panic。
        for (const char* k : {"x", "y"}) {
            if (req.contains(k)) {
                if (!req[k].is_number()) {
                    error = std::string(k) + " must be number";
                    return tg::IpcStatus::error;
                }
                const int px = static_cast<int>(req[k].get<float>());
                if (std::strcmp(k, "x") == 0) a.pos.x = px / game::kTileSize;
                else a.pos.y = px / game::kTileSize;
            }
        }
        // teleport（不走回合门控）之后，玩家的视觉位置必须同步 snap 到新逻辑格，
        // 否则 transform 视图会暴露"逻辑/视觉错位"（set_entity 是瞬移语义）。
        if (is_player) {
            d.tween.cancel(d.view_tween);
            d.view_x = static_cast<float>(a.pos.x * game::kTileSize);
            d.view_y = static_cast<float>(a.pos.y * game::kTileSize);
            d.view_init = true;
        }
        if (req.contains("color")) {
            if (!req["color"].is_string()) {
                error = "color must be string";
                return tg::IpcStatus::error;
            }
            a.color = parse_color(req["color"].get_ref<const std::string&>());
        }
        data = tg::Json::object();
        (*data)["entity"] = actor_to_json(d, a);
        return tg::IpcStatus::handled;
    }
    if (cmd == "spawn") {
        game::Actor a;
        a.id = req.value("id", "spawned");
        a.type = req.value("type", "actor");
        a.pos.x = req.contains("x") && req["x"].is_number()
                      ? static_cast<int>(req["x"].get<float>()) / game::kTileSize
                      : 0;
        a.pos.y = req.contains("y") && req["y"].is_number()
                      ? static_cast<int>(req["y"].get<float>()) / game::kTileSize
                      : 0;
        a.color = req.contains("color") && req["color"].is_string()
                      ? parse_color(req["color"].get_ref<const std::string&>())
                      : tg::Color{200, 200, 200, 255};
        if (a.id.empty() || d.gs.actors.count(a.id)) {
            error = "invalid or duplicate id";
            return tg::IpcStatus::error;
        }
        const std::string key = a.id;   // 键副本：std::move(a) 会清空 a.id
        d.gs.actors.emplace(key, std::move(a));
        data = tg::Json::object();
        (*data)["entity"] = actor_to_json(d, d.gs.actors.at(key));
        return tg::IpcStatus::handled;
    }
    if (cmd == "despawn") {
        const std::string id = req.value("id", "");
        if (id.empty() || !d.gs.actors.erase(id)) {
            error = "no such entity";
            return tg::IpcStatus::error;
        }
        data = tg::Json::object();
        (*data)["despawned"] = true;
        return tg::IpcStatus::handled;
    }
    if (cmd == "layers") {
        data = tg::Json::object();
        tg::Json arr = tg::Json::array();
        if (d.asset) {
            for (int i = 0; i < d.asset->layer_count(); ++i) {
                const auto& L = d.asset->layer(i);
                tg::Json lj = tg::Json::object();
                lj["name"] = L.name;
                lj["width"] = L.width;
                lj["height"] = L.height;
                lj["solid"] = L.solid;
                lj["origin"] = {L.origin_x, L.origin_y};
                lj["tileset"] = L.tileset_name.empty()
                                    ? tg::Json(nullptr)
                                    : tg::Json(L.tileset_name);
                lj["tiles"] = L.nonempty;
                arr.push_back(lj);
            }
        }
        (*data)["layers"] = arr;
        (*data)["count"] = arr.size();
        return tg::IpcStatus::handled;
    }
    if (cmd == "solid_at") {
        const float x = req.value("x", 0.0f), y = req.value("y", 0.0f);
        bool solid = false;
        if (d.asset)
            solid = tg::is_solid_at(*d.asset, tg::Vec2{x, y}) ==
                    tg::TileQueryResult::solid;
        data = tg::Json::object();
        (*data)["solid"] = solid;
        return tg::IpcStatus::handled;
    }
    if (cmd == "get_tile") {
        const float x = req.value("x", 0.0f), y = req.value("y", 0.0f);
        tg::Json tiles = tg::Json::array();
        bool solid = false;
        if (d.asset) {
            solid = tg::is_solid_at(*d.asset, tg::Vec2{x, y}) ==
                    tg::TileQueryResult::solid;
            for (int li = 0; li < d.asset->layer_count(); ++li) {
                int v = -1;
                const auto r = tg::tile_at(*d.asset, li, tg::Vec2{x, y}, &v);
                if (r == tg::TileLookupResult::occupied) {
                    tg::Json tj = tg::Json::object();
                    tj["layer"] = li;
                    tj["value"] = v;
                    tiles.push_back(tj);
                }
            }
        }
        data = tg::Json::object();
        (*data)["tiles"] = tiles;
        (*data)["solid"] = solid;
        return tg::IpcStatus::handled;
    }
    if (cmd == "reload") {
        reload_scene(d, d.scene_path);
        data = tg::Json::object();
        (*data)["reloaded"] = true;
        (*data)["reloads"] = d.reloads;
        return tg::IpcStatus::handled;
    }
    if (cmd == "screenshot") {
        d.shot_requested = true;
        d.shot_path = req.value("path", "screenshot.png");
        data = tg::Json::object();
        (*data)["path"] = d.shot_path;
        return tg::IpcStatus::handled;
    }
    if (cmd == "events") {
        // 事件目录（plan-7 §3.5）：game 事件注册表——当前无注册事件（传输通道
        // 机制就位）。未来 game 事件在此登记：条目形态 {name, when, data}，并
        // 注明 data 中可被 subscribe filter 过滤的字段（如 entity，建议字符串 id）。
        data = tg::Json{{"events", tg::Json::array()}};
        return tg::IpcStatus::handled;
    }
    if (cmd == "log") {
        TraceLog(LOG_INFO, "[demo] ipc log: %s", req.value("msg", "").c_str());
        data = tg::Json::object();
        (*data)["logged"] = true;
        return tg::IpcStatus::handled;
    }
    if (cmd == "quit") {
        d.quit = true;
        data = tg::Json::object();
        (*data)["bye"] = true;
        return tg::IpcStatus::handled;
    }
    return tg::IpcStatus::not_handled;
}

}  // namespace（游戏模型与 handler）

// ════════════════════ 主循环 ════════════════════

int main(int argc, char** argv) {
    // 启动自检（失败即退出，便于无窗口 CI 覆盖）
    if (skeleton_regression() != 0) {
        std::fprintf(stderr, "[demo] 骨架回归失败，退出\n");
        return 1;
    }
    std::printf("[demo] trogue %s C++ demo 就绪\n", tg::version_string());

    Demo d;
    std::string scene_path = "assets/scenes/demo.json";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc)
            scene_path = argv[++i];
        else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            d.port = std::atoi(argv[++i]);
    }
    d.scene_path = scene_path;

    InitWindow(d.window_w, d.window_h, "[trogue] C++ demo");
    SetTargetFPS(60);

    reload_scene(d, scene_path);

    d.ipc = tg::Ipc::create(static_cast<std::uint16_t>(d.port));
    if (d.ipc.valid()) {
        d.ipc.set_handler([&d](const std::string& cmd, const tg::Json& req,
                               std::optional<tg::Json>& data, std::string& err) {
            return ipc_handler(d, cmd, req, data, err);
        });
        TraceLog(LOG_INFO, "[demo] IPC 监听端口 %d", d.port);
    } else {
        TraceLog(LOG_INFO, "[demo] IPC 不可用（Release 桩/端口占用）");
    }
    d.watcher = tg::Watcher::create("assets/scenes");
    if (!d.watcher.valid())
        TraceLog(LOG_INFO, "[demo] watcher 不可用（非 Debug/非 Linux）");

    while (!WindowShouldClose() && !d.quit) {
        const float dt = GetFrameTime();

        // ── IPC poll（每帧） ──
        d.ipc.poll();

        // ── watcher 热重载 / F5 手动 ──
        if (d.watcher.valid()) {
            if (auto changed = d.watcher.poll()) {
                TraceLog(LOG_INFO, "[demo] watcher: %s → 重载", changed->c_str());
                reload_scene(d, scene_path);
            }
        }
        if (IsKeyPressed(KEY_F5)) {
            TraceLog(LOG_INFO, "[demo] F5 手动重载");
            reload_scene(d, scene_path);
        }

        // ── 回合制输入：按原版缓冲语义（0.18s 窗口双键对角 / 斜向直走 / 空格等待） ──
        poll_key_presses(d);
        tick_input_buffer(d, dt);
        init_player_view_if_needed(d);

        // ── Tween / 动画推进（数据驱动） ──
        d.tween.tick(dt);
        if (d.has_anim && d.anim.valid()) d.anim.advance(dt);

        // ── 渲染（引擎在调用方 BeginMode2D 区间内绘制） ──
        BeginDrawing();
        ClearBackground(BLACK);

        // 相机跟随玩家
        const float cam_px = d.gs.player()
                                 ? d.view_x + game::kTileSize / 2.0f
                                 : d.cam.x;
        const float cam_py = d.gs.player()
                                 ? d.view_y + game::kTileSize / 2.0f
                                 : d.cam.y;
        BeginMode2D(Camera2D{{d.window_w / 2.0f, d.window_h / 2.0f},
                             {cam_px, cam_py}, 0.0f, 2.0f});

        if (d.asset) tg::render_scene(*d.asset);

        // 实体显式绘制（按 y 再 z 排序，稳定；玩家用插值位置）
        std::vector<const game::Actor*> order;
        order.reserve(d.gs.actors.size());
        for (const auto& [id, a] : d.gs.actors) order.push_back(&a);
        std::stable_sort(order.begin(), order.end(),
                         [](const game::Actor* l, const game::Actor* r) {
                             if (l->pos.y != r->pos.y) return l->pos.y < r->pos.y;
                             return l->z < r->z;
                         });
        for (const game::Actor* a : order) {
            float wx = a->pos.x * game::kTileSize;
            float wy = a->pos.y * game::kTileSize;
            if (a->is_player) { wx = d.view_x; wy = d.view_y; }
            const ::Color rc{a->color.r, a->color.g, a->color.b, a->color.a};
            bool drawn = false;
            if (a->sprite.has && d.asset) {
                const auto rr = tg::render_sprite(*d.asset, a->sprite,
                                                  tg::Vec2{wx, wy}, a->color);
                if (rr == tg::RenderResult::Drawn) drawn = true;
            }
            if (!drawn)
                // 浮点矩形绘制（raylib DrawRectanglePro，不做 int 截断）：
                // 与 tile 层同相机变换下严格对齐（引擎 draw_rect 会 int 截断，
                // 移动插值中会造成 ±1px 错位/抖动，故此处直接调 raylib）。
                DrawRectanglePro(
                    ::Rectangle{wx, wy,
                                static_cast<float>(game::kTileSize),
                                static_cast<float>(game::kTileSize)},
                    ::Vector2{0.0f, 0.0f}, 0.0f, rc);
        }

        EndMode2D();

        // HUD
        DrawFPS(10, 10);
        DrawText(TextFormat("scene=%s actors=%d reloads=%d turn=%d phase=%s",
                            d.asset ? std::string(d.asset->name()).c_str() : "-",
                            static_cast<int>(d.gs.actors.size()), d.reloads,
                            d.gs.turn_count,
                            d.gs.phase == game::Phase::PlayerTurn ? "player"
                                                                  : "enemy"),
                 10, 36, 16, WHITE);

        // 帧末截图（game 排队；帧后 ExportImage）
        if (d.shot_requested) {
            d.shot_requested = false;
            // 先强制 flush 渲染批（raylib 的批顶点在 EndDrawing 才真正提交 GL；
            // 不 flush 就 glReadPixels 会拍到未绘制的残缺帧——曾致 soldier 场景
            // 截图全黑、forest 截图丢 HUD 的误诊）。
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            if (!ExportImage(img, d.shot_path.c_str()))
                TraceLog(LOG_WARNING, "[demo] 截图导出失败: %s",
                         d.shot_path.c_str());
            UnloadImage(img);
            TraceLog(LOG_INFO, "[demo] 截图已写出: %s", d.shot_path.c_str());
        }

        EndDrawing();
    }

    d.anim.bind(tg::AnimationSet{});  // 解绑（asset 即将析构）
    d.asset.reset();
    d.gs = game::GameState{};
    d.ipc = tg::Ipc{};
    tg::shutdown_render();
    CloseWindow();
    return 0;
}