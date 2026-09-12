// main.cpp —— 回合制 roguelike demo（最小闭环 + 敌 AI/规则/事件接入）。
//
// 仅使用 trogue/*.hpp 公共 API + game 层模块（game_core/nav/rules/ai/event_bus）。
// demo 演示：
//   - 回合制：玩家移动（8 向 + 斜切约束）→ 敌方回合（AI：三态状态机 +
//     视野 + A*）→ 回合 +1
//   - 键盘：WASD/方向键 4 向、Q/E/Z/C 斜向（对齐原版 input.lua KEY_MOVEMENTS）、
//     空格等待；玩家/敌人视觉移动均由引擎 TweenManager 驱动
//   - 规则管线：EventBus（game 层）→ AbilityUse → 伤害结算 → AbilityUsed/
//     EntityDied（按原版顺序）
//   - 渲染：render_scene 画 tile 层；实体按 (y, z) 排序后显式绘制（色块/sprite）
//   - 热重载：Watcher + F5 + IPC reload（candidate load → 帧外 swap，位置+回合保留）
//   - IPC 命令 handler（全部命令语义在 game；engine 只传 ping）+ 事件桥
//     （6 个对外事件经 tg::Ipc::publish）
//   - IPC 回合命令 turn/move/wait（场景无关，非视觉 Agent 可驱动）
//   - 截图（game 排队，帧后 ExportImage）、log、quit
#include <algorithm>
#include <array>
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
#include <rlgl.h>  // rlDrawRenderBatchActive（离屏截图取像前 flush）

#include "trogue/trogue.hpp"
#include "ai.hpp"
#include "anim_util.hpp"
#include "event_bus.hpp"
#include "game_core.hpp"
#include "rules.hpp"

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
    if (set.name() != "AnimatedSprite2D") return 1;  // 名 = entity id
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

    // ── 事件总线 / 规则引擎 / 敌 AI ──
    game::EventBus bus;        // game 层纯逻辑事件总线（IPC 桥接在 main 完成）
    game::RuleEngine rules;    // 规则最小子集（构造即内置 punch/damage_physical）
    game::AiSystem ai;         // 敌 AI（固定种子 20260909，可复现）
    game::GameSystems sys{&bus, &rules, &ai};

    // 敌人视觉位置（复用引擎 TweenManager；静止时 == 逻辑格像素）
    struct EnemyView {
        float vx = 0, vy = 0;
        tg::TweenManager::Id tween = 0;
        bool init = false;
    };
    std::map<std::string, EnemyView> enemy_view;

    tg::TweenManager tween;
    tg::AnimationPlayer anim;          // 帧动画播放器（绑定首个含动画 actor 的集）
    bool has_anim = false;             // 已绑定可播放动画集
    int bound_anim_set = -1;           // 当前绑定动画集序号（归属校验）

    int window_w = 1920, window_h = 1080;
    bool fullscreen = false;
    tg::Vec2 cam{0, 0};
    bool quit = false;

    tg::Ipc ipc;
    int port = tg::kIpcPortDefault;

    bool headless = false;  // --headless：隐藏窗口（仍建 GL 上下文，供离屏截图）

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
// 敌人同由引擎 tween 驱动（静止时 == 逻辑格像素）；
// 惰性实体无插值，视觉位置就是逻辑格像素。（供 IPC transform 视图使用）
void entity_visual(const Demo& d, const game::Actor& a, float& vx, float& vy,
                   bool& moving) {
    vx = static_cast<float>(a.pos.x * game::kTileSize);
    vy = static_cast<float>(a.pos.y * game::kTileSize);
    moving = false;
    if (a.is_player) {
        vx = d.view_x;
        vy = d.view_y;
        moving = d.tween.alive(d.view_tween);
        return;
    }
    const auto it = d.enemy_view.find(a.id);
    if (it != d.enemy_view.end() && d.tween.alive(it->second.tween)) {
        vx = it->second.vx;
        vy = it->second.vy;
        moving = true;
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

// 场景交换核心（candidate 已成功加载；keep_player + reloads 自增 + 动画重绑。
// reload 与 genmap 共用）
void swap_scene(Demo& d, std::unique_ptr<tg::SceneAsset> loaded) {
    // 保留玩家运行时位置与回合数（game 策略：reload 后玩家位置/回合不变；
    // 其它 actor 全部按新 descriptor 重建）。旧 C demo 曾有等价行为。
    std::optional<game::TilePos> keep_player;
    if (const game::Actor* p = d.gs.player()) keep_player = p->pos;
    const int keep_turn = d.gs.turn_count;

    d.anim.bind(tg::AnimationSet{});   // 解绑旧播放器（旧 asset 即将析构）
    d.asset = std::move(loaded);
    game::import_scene(d.gs, *d.asset);
    if (keep_player) {
        if (game::Actor* p = d.gs.player()) p->pos = *keep_player;
    }
    d.gs.turn_count = keep_turn;       // 回合数跨 reload 保留（游戏进度）
    d.tween.cancel_all();              // 打断进行中的移动 tween（旧场景视觉位置失效）
    d.enemy_view.clear();              // 敌人视觉条目随场景重建
    d.view_init = false;               // 重新追踪玩家视觉位置
    d.view_tween = 0;
    game::input_buffer_flush(d.input); // 清空未决输入（场景已换）
    ++d.reloads;
    // 动画绑定：绑定首个含动画 actor 的动画集；bound_anim_set
    // 供绘制采样与快照注入做归属校验（防多动画实体时张冠李戴）
    d.bound_anim_set = -1;
    if (d.asset) {
        // 注意：actors 为 std::map，「首个」= id 字典序而非场景文件序；
        // 单动画实体场景无影响，多动画实体时的选择策略留待定夺
        for (const auto& [id, a] : d.gs.actors) {
            if (a.anim_set >= 0) { d.bound_anim_set = a.anim_set; break; }
        }
    }
    d.has_anim = d.asset && d.bound_anim_set >= 0;
    if (d.has_anim) {
        d.anim.bind(d.asset->animation_set(d.bound_anim_set));
        if (!d.anim.play("idle")) d.anim.play("walk");
    } else {
        d.anim.bind(tg::AnimationSet{});
    }
    TraceLog(LOG_INFO, "[demo] 场景已交换（reloads=%d）", d.reloads);
}

// candidate load → 帧外 swap（失败保留旧 asset/旧 state，失败安全）
void reload_scene(Demo& d, const std::string& path) {
    auto loaded = tg::SceneAsset::load(path);
    if (!loaded) {
        TraceLog(LOG_WARNING, "[demo] 加载失败（保留旧场景）: %s",
                 loaded.error().message.c_str());
        return;
    }
    swap_scene(d, std::make_unique<tg::SceneAsset>(std::move(*loaded)));
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
game::ActionResult handle_move(Demo& d, game::Dir m) {
    // 原版 handleMove 不查回合门控（缓冲触发无守卫，回合已由按键入口保证）；
    // GameSystems 版内部处理 invalid/blocked（含 phase 守卫），无副作用风险。
    // 完整敌方阶段：移动成功后 AI 敌人行动（事件驱动）。
    // 返回 ActionResult：IPC move 复用本函数——键盘/IPC 同一条「移动→tween」
    // 管线（曾因 IPC 直接调 player_move 跳过 tween，导致逻辑格已动、视觉
    // 停在旧格整整 1 tile 的分叉 bug，transform 视图暴露）。
    const auto r = game::player_move(d.gs, d.sys, m.dx, m.dy);
    if (r != game::ActionResult::Moved || !d.gs.player()) return r;

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
    return r;
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
        (void)game::player_wait(d.gs, d.sys);
    }
}

// 每帧：缓冲计时（对齐原版 update()：窗口耗尽 → 触发单键走格）
void tick_input_buffer(Demo& d, float dt) {
    if (auto step = game::input_buffer_tick(d.input, dt))
        handle_move(d, *step);
}

// ── 帧绘制（相机跟随 + 场景 tile + 实体 + HUD） ──
//
// 抽为独立函数的原因：既要给主循环用（屏幕目标），又要给**同步截图**用
// （离屏 FBO 目标）——两条路径必须绘制同一内容。调用方负责
// BeginDrawing/BeginTextureMode 与 ClearBackground（本函数不触及目标切换）。
void draw_frame(const Demo& d) {
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
        if (a->is_player) {
            wx = d.view_x;
            wy = d.view_y;
        } else {
            // 敌人：tween 插值中的视觉位置（静止 == 逻辑格像素）
            const auto vit = d.enemy_view.find(a->id);
            if (vit != d.enemy_view.end()) {
                wx = vit->second.vx;
                wy = vit->second.vy;
            }
        }
        // 绘制（共享工具 anim_util，采样公式所在处）：该 actor 的
        // 动画集已绑定 → 传播放器采样当前帧；触发/绑定策略在此调用方，
        // 机制（offset 组合 + 静态回退 + 色块兜底）在 game::draw_entity_sprite。
        // *d.asset 解引用依赖不变量：actors 非空 ⟹ 某次加载成功且 asset
        // 存活（初始加载失败则无 actor；reload 失败保留旧 asset）。
        game::draw_entity_sprite(
            *d.asset,
            (a->anim_set >= 0 && d.has_anim && d.bound_anim_set == a->anim_set)
                ? &d.anim
                : nullptr,
            a->sprite, tg::Vec2{wx, wy}, a->color, game::kTileSize,
            game::kTileSize);
    }

    EndMode2D();

    // HUD
    DrawFPS(10, 10);
    DrawText(TextFormat("scene=%s actors=%d reloads=%d turn=%d phase=%s",
                        d.asset ? std::string(d.asset->name()).c_str() : "-",
                        static_cast<int>(d.gs.actors.size()), d.reloads,
                        d.gs.turn_count,
                        d.gs.phase == game::Phase::PlayerTurn ? "player"
                        : d.gs.phase == game::Phase::EnemyTurn ? "enemy"
                                                               : "game_over"),
             10, 36, 16, WHITE);
    if (d.gs.phase == game::Phase::GameOver)
        DrawText("GAME OVER - F5 reload to restart", 10, 60, 20, RED);
}

// ── 同步截图：离屏 FBO 渲染同一 draw_frame 并导出 PNG ──
//
// 关键：**不用** LoadImageFromScreen。窗口模式下帧后读屏会拿到已交换的
// 黑帧（实测）；且 headless（隐藏窗口）下屏幕路径不可靠。改用共享工具
// game::capture_offscreen_png：同一 draw_frame 渲染到离屏 RenderTexture，
// 同步落盘（响应时文件已存在）、含实体/HUD、窗口与无头共用一条绘制路径。
game::ShotResult capture_frame_png(const Demo& d, const std::string& path) {
    return game::capture_offscreen_png(d.window_w, d.window_h, path,
                                       [&d] { draw_frame(d); });
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
    j["phase"] = d.gs.phase == game::Phase::PlayerTurn ? "player"
                 : d.gs.phase == game::Phase::EnemyTurn ? "enemy"
                                                        : "game_over";
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

// ── 程序生成地图：地形指派是玩法决策（本节全部逻辑归 game），
// 引擎只提供 pick_tile 采样、确定性哈希与 load_json 内存加载。生成流程：
//   噪声指派 → pick_tile 填 id → 拼 tro-scene JSON → load_json → swap_scene。
// 噪声为最小确定性 value-noise（粗网格双线性插值）：坐标散列用引擎
// hash_combine（splitmix64 组合，跨平台逐位一致），同 seed 同 w/h 逐位一致。

float gen_value_noise(int x, int y, std::uint64_t seed, int period) {
    const int x0 = x / period, y0 = y / period;
    const float fx = static_cast<float>(x % period) / static_cast<float>(period);
    const float fy = static_cast<float>(y % period) / static_cast<float>(period);
    const auto v = [&](int gx, int gy) {
        return static_cast<float>(
                   tg::hash_combine(seed, static_cast<std::uint64_t>(gx),
                                    static_cast<std::uint64_t>(gy)) >>
                   40) /
               16777216.0f;  // [0,1)
    };
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    return lerp(lerp(v(x0, y0), v(x0 + 1, y0), fx),
                lerp(v(x0, y0 + 1), v(x0 + 1, y0 + 1), fx), fy);
}

// genmap 生成上限（格）：远小于引擎 kLayerDimMax=4096——演示级的 JSON 体积与
// 生成耗时约束（更大需求由调用方分片）
inline constexpr int kGenMapDimMax = 64;

tg::IpcStatus handle_genmap(Demo& d, const tg::Json& req,
                            std::optional<tg::Json>& data, std::string& error) {
    if (!req.contains("seed") || !req["seed"].is_number_integer()) {
        error = "genmap needs integer seed";
        return tg::IpcStatus::error;
    }
    const auto seed =
        static_cast<std::uint64_t>(req["seed"].get<std::int64_t>());
    int w = 40, h = 40;
    for (const auto& kv : {std::pair<const char*, int*>{"w", &w}, {"h", &h}}) {
        if (!req.contains(kv.first)) continue;
        if (!req[kv.first].is_number_integer()) {
            error = "genmap w/h must be integers";
            return tg::IpcStatus::error;
        }
        *kv.second = req[kv.first].get<int>();
    }
    if (w < 1 || w > kGenMapDimMax || h < 1 || h > kGenMapDimMax) {
        error = "genmap w/h out of range (1..64)";
        return tg::IpcStatus::error;
    }
    // 匹配表现读（文件小、保持无状态；失败如实报错，旧场景不受影响）
    auto table = tg::load_terrain_table("tilesets/test_tileset_1.json");
    if (!table) {
        error = "terrain table load failed: " + table.error().message;
        return tg::IpcStatus::error;
    }
    // ① 地形指派（game 决策：噪声阈值 → ground/空）
    std::vector<std::vector<bool>> ground(h, std::vector<bool>(w, false));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            ground[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] =
                gen_value_noise(x, y, seed, 6) >= 0.55f;
    // ② 引擎选择器填 id（8 邻位：同指派才连；corners_and_sides 模式 8 位全参与）
    static constexpr int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};  // t,tr,r,br,b,bl,l,tl
    static constexpr int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    tg::Json tiles = tg::Json::array();
    int nonempty = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!ground[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]) {
                tiles.push_back(-1);
                continue;
            }
            std::array<int, 8> pat{};
            pat.fill(-1);
            for (int k = 0; k < 8; ++k) {
                const int nx = x + kDx[k], ny = y + kDy[k];
                if (nx >= 0 && nx < w && ny >= 0 && ny < h &&
                    ground[static_cast<std::size_t>(ny)][static_cast<std::size_t>(nx)])
                    pat[static_cast<std::size_t>(k)] = 0;
            }
            auto id = tg::pick_tile(*table, 0, 0, pat);
            tiles.push_back(id.value_or(-1));  // 采样失败兜底空格（不该发生）
            if (id) ++nonempty;
        }
    }
    // ③ 拼 tro-scene JSON → ④ load_json → swap（复用 keep_player/reloads 语义）
    tg::Json tileset_ref = tg::Json::object();
    tileset_ref["name"] = "terrain";
    tileset_ref["path"] = "tilesets/test_tileset_1.json";
    tg::Json layer = tg::Json::object();
    layer["name"] = "gen";
    layer["width"] = w;
    layer["height"] = h;
    layer["solid"] = false;  // 生成层纯视觉；solid 语义留玩法接管时再定
    layer["tileset"] = "terrain";
    layer["tiles"] = tiles;
    tg::Json tilemap = tg::Json::object();
    tilemap["tile_width"] = 16;
    tilemap["tile_height"] = 16;
    tilemap["tilesets"] = tg::Json::array({tileset_ref});
    tilemap["layers"] = tg::Json::array({layer});
    tg::Json scene = tg::Json::object();
    scene["format"] = "tro-scene";
    scene["version"] = 2;
    scene["meta"] = tg::Json{{"name", "genmap"}, {"background", "#101018"}};
    scene["tilemap"] = tilemap;
    scene["entities"] = tg::Json::array();
    const std::string name = "genmap(seed=" + std::to_string(seed) + ")";
    auto loaded = tg::SceneAsset::load_json(scene.dump(), name);
    if (!loaded) {
        TraceLog(LOG_WARNING, "[demo] genmap 场景加载失败: %s",
                 loaded.error().message.c_str());
        error = loaded.error().message;
        return tg::IpcStatus::error;
    }
    swap_scene(d, std::make_unique<tg::SceneAsset>(std::move(*loaded)));
    data = tg::Json::object();
    (*data)["generated"] = true;
    (*data)["seed"] = req["seed"];
    (*data)["w"] = w;
    (*data)["h"] = h;
    (*data)["nonempty"] = nonempty;
    (*data)["reloads"] = d.reloads;
    return tg::IpcStatus::handled;
}

// ── IPC handler（全部命令归属 game；engine 只传 ping） ──

tg::IpcStatus ipc_handler(Demo& d, const std::string& cmd, const tg::Json& req,
                          std::optional<tg::Json>& data, std::string& error) {
    if (cmd == "help") {
        static const char* cmds[] = {
            "ping", "help", "status", "list_entities", "get_entity",
            "query_entities", "set_entity", "spawn", "despawn",
            "layers", "solid_at", "get_tile", "probe_collide", "reload", "screenshot",
            "log", "quit", "turn", "move", "wait", "genmap",
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
        (*data)["phase"] = d.gs.phase == game::Phase::PlayerTurn ? "player"
                           : d.gs.phase == game::Phase::EnemyTurn ? "enemy"
                                                                  : "game_over";
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
        // nlohmann get<int> 静默截断，故先拒绝（整数值各在 -1..1）。
        if (!req.contains("dx") || !req.contains("dy") ||
            !req["dx"].is_number_integer() || !req["dy"].is_number_integer()) {
            error = "move needs integer dx/dy";
            return tg::IpcStatus::error;
        }
        const int dx = req["dx"].get<int>();
        const int dy = req["dy"].get<int>();
        // 与键盘同一入口：移动成功时由 handle_move 启动视觉 tween（见其注释）
        const auto r = handle_move(d, game::Dir{dx, dy});
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
        (void)game::player_wait(d.gs, d.sys);
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
        // teleport（不走回合门控）之后，视觉位置必须同步 snap 到新逻辑格，
        // 否则 transform 视图会暴露"逻辑/视觉错位"（set_entity 是瞬移语义）。
        if (is_player) {
            d.tween.cancel(d.view_tween);
            d.view_x = static_cast<float>(a.pos.x * game::kTileSize);
            d.view_y = static_cast<float>(a.pos.y * game::kTileSize);
            d.view_init = true;
        } else if (auto vit = d.enemy_view.find(a.id); vit != d.enemy_view.end()) {
            d.tween.cancel(vit->second.tween);
            vit->second.vx = static_cast<float>(a.pos.x * game::kTileSize);
            vit->second.vy = static_cast<float>(a.pos.y * game::kTileSize);
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
        // 视觉状态与 actor 同生共死（同 EntityDied 分支）：残留 enemy_view
        // 条目会让同 id 重生渲染在旧位置（wire visual 正确、画面错位，
        // transform 视图会暴露矛盾——审查 M9 发现）。
        if (auto vit = d.enemy_view.find(id); vit != d.enemy_view.end()) {
            d.tween.cancel(vit->second.tween);
            d.enemy_view.erase(vit);
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
    if (cmd == "probe_collide") {
        if (!d.asset) {
            error = "no scene loaded";
            return tg::IpcStatus::error;
        }
        tg::Json out = tg::Json::object();
        bool any = false;
        // segment 分区：a + b
        if (req.contains("a") && req.contains("b")) {
            if (!req["a"].is_array() || req["a"].size() != 2 ||
                !req["b"].is_array() || req["b"].size() != 2) {
                error = "probe_collide a/b must be [x,y]";
                return tg::IpcStatus::error;
            }
            const float ax = req["a"][0].get<float>(), ay = req["a"][1].get<float>();
            const float bx = req["b"][0].get<float>(), by = req["b"][1].get<float>();
            const auto h = tg::segment_hits_solid(*d.asset, tg::Vec2{ax, ay},
                                                  tg::Vec2{bx, by});
            tg::Json sj = tg::Json::object();
            sj["result"] = h.result == tg::TileQueryResult::solid  ? "solid"
                           : h.result == tg::TileQueryResult::error ? "error"
                                                                    : "clear";
            sj["layer"] = h.layer;
            sj["tx"] = h.tx;
            sj["ty"] = h.ty;
            sj["t"] = h.t;
            sj["point"] = tg::Json::array({h.point.x, h.point.y});
            out["segment"] = sj;
            any = true;
        }
        // sweep 分区：rect + delta
        if (req.contains("rect") && req.contains("delta")) {
            if (!req["rect"].is_array() || req["rect"].size() != 4 ||
                !req["delta"].is_array() || req["delta"].size() != 2) {
                error = "probe_collide rect must be [x,y,w,h], delta [dx,dy]";
                return tg::IpcStatus::error;
            }
            const tg::Rect box{req["rect"][0].get<float>(), req["rect"][1].get<float>(),
                               req["rect"][2].get<float>(), req["rect"][3].get<float>()};
            const tg::Vec2 delta{req["delta"][0].get<float>(),
                                 req["delta"][1].get<float>()};
            const auto s = tg::sweep_move(*d.asset, box, delta);
            tg::Json wj = tg::Json::object();
            wj["box"] = tg::Json::array({s.box.x, s.box.y, s.box.w, s.box.h});
            wj["blocked_x"] = s.blocked_x;
            wj["blocked_y"] = s.blocked_y;
            wj["result"] = s.result == tg::TileQueryResult::solid  ? "solid"
                           : s.result == tg::TileQueryResult::error ? "error"
                                                                    : "clear";
            out["sweep"] = wj;
            any = true;
        }
        if (!any) {
            error = "probe_collide needs (a,b) and/or (rect,delta)";
            return tg::IpcStatus::error;
        }
        data = out;
        return tg::IpcStatus::handled;
    }
    if (cmd == "genmap") return handle_genmap(d, req, data, error);
    if (cmd == "reload") {
        reload_scene(d, d.scene_path);
        data = tg::Json::object();
        (*data)["reloaded"] = true;
        (*data)["reloads"] = d.reloads;
        return tg::IpcStatus::handled;
    }
    if (cmd == "screenshot") {
        // 同步语义：本 handler 内完成渲染+落盘，响应返回时文件已存在。
        // 走离屏 FBO（capture_frame_png）——不依赖屏幕缓冲，headless 亦可。
        const std::string path = req.value("path", "screenshot.png");
        const game::ShotResult sr = capture_frame_png(d, path);
        data = tg::Json::object();
        (*data)["path"] = path;
        (*data)["ok"] = sr.ok;
        (*data)["w"] = sr.w;
        (*data)["h"] = sr.h;
        (*data)["bytes"] = sr.bytes;
        if (sr.ok)
            TraceLog(LOG_INFO, "[demo] 截图已写出: %s", path.c_str());
        else
            TraceLog(LOG_WARNING, "[demo] 截图失败: %s", path.c_str());
        return tg::IpcStatus::handled;
    }
    if (cmd == "events") {
        // 事件目录。
        // 条目 {name, when, data}；data 顶层 entity/source/target = 字符串 id，
        // 可被 subscribe filter 等值匹配。内部事件（AbilityUse/
        // AbilityUseFailed/DamageRequest）不在注册表——不对外发布。
        auto entry = [](const char* name, const char* when, const char* data,
                        const char* filter) {
            return tg::Json{{"name", name},
                            {"when", when},
                            {"data", data},
                            {"filter", filter}};
        };
        data = tg::Json{{"events", tg::Json::array({
            entry("StateChanged", "敌人 AI 状态迁移时",
                  "{entity, from, to, turn}", "entity"),
            entry("MoveSucceeded", "任意 actor 移动成功时",
                  "{entity, from:[gx,gy], to:[gx,gy]}", "entity"),
            entry("AbilityUsed", "规则层成功释放能力时（伤害结算之后）",
                  "{entity, ability, target:[gx,gy], turn}", "entity"),
            entry("DamageDealt", "伤害结算时",
                  "{source, target, amount, hp, max_hp}", "source target"),
            entry("EntityDied", "实体 hp≤0 时（敌人在回合收尾才真正移除）",
                  "{entity, turn}", "entity"),
            entry("TurnEnded", "敌方阶段完成、回合数 +1 后（GameOver 不发）",
                  "{turn}", "（无实体字段，仅全量订阅可收）"),
        })}};
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
        else if (std::strcmp(argv[i], "--headless") == 0)
            d.headless = true;
        else if (std::strcmp(argv[i], "--fullscreen") == 0)
            d.fullscreen = true;
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc)
            d.window_w = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc)
            d.window_h = std::atoi(argv[++i]);
    }
    d.scene_path = scene_path;

    // ── 应用接线 ──
    // 实体快照扩展注入点：hp（有 hp 的 actor）+ ai（战斗原型）+ anim（
    // 该 actor 的动画集被绑定时上报 clip/frame）；goblin 有 hp/ai，coin 等
    // 惰性实体两者皆无，player 仅 hp，soldier 仅 anim。
    d.extra_entity_fields = [&d](tg::Json& j, const game::Actor& a) {
        if (a.hp) j["hp"] = {a.hp->cur, a.hp->max};
        if (game::is_combat_archetype(a.type)) {
            tg::Json ai = tg::Json{{"state", game::ai_state_name(a.ai.state)}};
            ai["target"] = a.ai.has_target
                               ? tg::Json{a.ai.target.x, a.ai.target.y}
                               : tg::Json(nullptr);
            j["ai"] = ai;
        }
        if (a.anim_set >= 0 && d.has_anim && d.bound_anim_set == a.anim_set) {
            j["anim"] = tg::Json{{"clip", d.anim.clip_name()},
                                 {"frame", d.anim.frame_index()}};
        }
    };
    // 规则引擎订阅管线事件（AbilityUse/DamageRequest/TurnEnded）；gs 对象
    // 生命周期 = Demo（热重载只换 asset，指针恒有效），绑定一次即可。
    d.rules.bind(d.gs, d.bus);

    unsigned int window_flags = FLAG_WINDOW_RESIZABLE;
    if (d.headless) window_flags |= FLAG_WINDOW_HIDDEN;
    if (d.fullscreen && !d.headless) window_flags |= FLAG_FULLSCREEN_MODE;
    if (window_flags != 0) SetConfigFlags(window_flags);
    InitWindow(d.window_w, d.window_h, "[trogue] C++ demo");
    // Camera2D 和离屏绘制统一使用 raylib 当前的逻辑渲染尺寸。
    d.window_w = GetRenderWidth();
    d.window_h = GetRenderHeight();
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

    // ── IPC 事件桥：白名单 6 事件逐个桥到 tg::Ipc::publish。
    // EventBus 保持纯逻辑零 IPC 依赖，桥接（订阅→转发）在 main 层完成。
    {
        static const char* kWireEvents[] = {"StateChanged", "MoveSucceeded",
                                            "AbilityUsed",  "DamageDealt",
                                            "EntityDied",   "TurnEnded"};
        for (const char* ev : kWireEvents) {
            d.bus.on(ev, [&d, ev](const tg::Json& j) {
                if (d.ipc.valid()) d.ipc.publish(ev, j);
            });
        }
    }
    // 敌人移动 → 视觉 tween（复用引擎 add_vec2 + quad_out，与玩家同参数；
    // 播完精确落格，静止时 == 逻辑格像素）。
    d.bus.on("MoveSucceeded", [&d](const tg::Json& j) {
        const auto entity = j.at("entity").get<std::string>();
        const auto act = d.gs.actors.find(entity);
        if (act == d.gs.actors.end() || act->second.is_player) return;
        const auto to = j.at("to");
        const float tx = to.at(0).get<float>() * game::kTileSize;
        const float ty = to.at(1).get<float>() * game::kTileSize;
        auto& v = d.enemy_view[entity];
        if (!v.init) {
            const auto from = j.at("from");
            v.vx = from.at(0).get<float>() * game::kTileSize;
            v.vy = from.at(1).get<float>() * game::kTileSize;
            v.init = true;
        }
        d.tween.cancel(v.tween);  // 打断上一个（连续移动续滑）
        v.tween = d.tween.add_vec2(
            tg::Vec2{v.vx, v.vy}, tg::Vec2{tx, ty},
            tg::TweenSpec{game::kMoveDuration, 0.0, 0, tg::Easing::quad_out},
            [&d, entity](const tg::TweenManager::Sample<tg::Vec2>& s) {
                // find 而非 []：条目被删（despawn/reload）时不静默回插僵尸条目
                if (auto vit = d.enemy_view.find(entity); vit != d.enemy_view.end()) {
                    vit->second.vx = s.value.x;
                    vit->second.vy = s.value.y;
                }
            },
            [&d, entity, tx, ty]() {
                if (auto vit = d.enemy_view.find(entity); vit != d.enemy_view.end()) {
                    vit->second.vx = tx;
                    vit->second.vy = ty;
                }
            });
    });
    // 敌人死亡 → 清理视觉条目（尸体在回合收尾移除，视觉先行收尾）
    d.bus.on("EntityDied", [&d](const tg::Json& j) {
        const auto entity = j.at("entity").get<std::string>();
        const auto vit = d.enemy_view.find(entity);
        if (vit != d.enemy_view.end()) {
            d.tween.cancel(vit->second.tween);
            d.enemy_view.erase(vit);
        }
    });

    d.watcher = tg::Watcher::create("assets/scenes");
    if (!d.watcher.valid())
        TraceLog(LOG_INFO, "[demo] watcher 不可用（非 Debug/非 Linux）");

    while (!WindowShouldClose() && !d.quit) {
        const float dt = GetFrameTime();

        // raylib 通过轮询报告窗口尺寸变化；最大化/拖拽缩放后必须同步相机 offset。
        if (IsWindowResized()) {
            const int render_w = GetRenderWidth();
            const int render_h = GetRenderHeight();
            if (render_w > 0 && render_h > 0) {
                d.window_w = render_w;
                d.window_h = render_h;
            }
        }

        // 视觉追踪先于 IPC/按键：任何来源的首帧行动都不会从 (0,0) 起 tween
        init_player_view_if_needed(d);

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

        // ── Tween / 动画推进（数据驱动） ──
        d.tween.tick(dt);
        if (d.has_anim && d.anim.valid()) d.anim.advance(dt);

        // ── 渲染（引擎在调用方 BeginMode2D 区间内绘制） ──
        BeginDrawing();
        ClearBackground(BLACK);
        draw_frame(d);
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