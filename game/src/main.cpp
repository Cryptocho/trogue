// main.cpp —— 里程碑 5.6：C++ game demo（新 API 的最终 consumer）。
//
// 仅使用 trogue/*.hpp 公共 API（不依赖任何历史 C 头）。demo 演示：
//   - 窗口 + 相机 + 渲染（render_scene 画 tile 层；显式 sprite 画实体）
//   - WASD 移动（game 逻辑 + is_solid_at/rect_hits_solid 静态碰撞）
//   - AnimationPlayer（动画集 idle/walk 切换）+ TweenManager（补间示意）
//   - 热重载：Watcher + F5 + IPC reload（candidate load → 帧外 swap）
//   - IPC 命令 handler（**全部命令语义在 game**，engine 只传 ping）
//   - 截图（game 排队，帧后 ExportImage）、log、quit
//
// 命令归属表见 docs/plan-5.6.md §1。
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <raylib.h>

#include "trogue/trogue.hpp"

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
    // 资产回归：demo（palette）+ test（atlas）+ soldier（bare+动画）
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
    tg::AnimationPlayer pp;
    pp.bind(set);
    if (!pp.play("idle") || pp.frame_index() != 0) return 1;
    return 0;
}

// ── 游戏对象（OOP 演示：自建 Actor，非引擎实体） ──

struct Actor {
    std::string id;
    std::string type;
    tg::Rect box;            // 世界 AABB（左上角 + w/h）
    tg::Color color;
    int z = 0;
    tg::SpriteDesc sprite;   // 从 descriptor 复制的快照（含 asset_id）
};

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

// actor → JSON 快照（wire 兼容：id/type/x/y/w/h/color/z/sprite）
tg::Json actor_to_json(const Actor& a) {
    tg::Json j = tg::Json::object();
    j["id"] = a.id;
    j["type"] = a.type;
    j["x"] = a.box.x;
    j["y"] = a.box.y;
    j["w"] = a.box.w;
    j["h"] = a.box.h;
    j["color"] = color_hex(a.color);
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
    return j;
}

// ── demo 应用状态 ──

struct Demo {
    std::unique_ptr<tg::SceneAsset> asset;   // 只读资产（帧外 swap）
    std::map<std::string, Actor> actors;     // game 自有对象（唯一可变权威）
    int reloads = 0;

    tg::TweenManager tween;
    tg::AnimationPlayer anim;                // 绑定首个动画集（示意）
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
};

// 从 scene descriptor 导入 actors（copy-out 快照 → game 自有对象集合）
void import_actors(Demo& d) {
    d.actors.clear();
    if (!d.asset) return;
    const int n = d.asset->entity_count();
    for (int i = 0; i < n; ++i) {
        const tg::SceneEntity e = d.asset->entity(i);  // 值快照
        Actor a;
        a.id = e.id;
        a.type = e.type;
        a.box = tg::Rect{e.x, e.y, e.w, e.h};
        a.color = e.color;
        a.z = e.z;
        a.sprite = e.sprite;
        // 键取 id 的拷贝：std::move(a) 会清空 a.id（Actor 整体移动），
        // 若直接用 std::move(a.id) 作键则 map 值里的 id 字段变空。
        const std::string key = a.id;
        d.actors.emplace(key, std::move(a));
    }
}

// candidate load → 帧外 swap（失败保留旧 asset/旧 state，失败安全）
void reload_scene(Demo& d, const std::string& path) {
    auto loaded = tg::SceneAsset::load(path);
    if (!loaded) {
        TraceLog(LOG_WARNING, "[demo] 加载失败（保留旧场景）: %s",
                 loaded.error().message.c_str());
        return;
    }
    // 解绑旧播放器（旧 asset 即将析构；先解除引用避免悬垂）
    d.anim.bind(tg::AnimationSet{});
    d.asset = std::make_unique<tg::SceneAsset>(std::move(*loaded));
    // 保留 player 运行时位置（game 策略示例：按 id 保留可变对象态；
    // 其它 actor 全部按新 descriptor 重建）。旧 C demo 曾有等价行为，
    // 现属 game 语义（engine 不参与）。
    std::optional<tg::Vec2> keep_player;
    if (auto it = d.actors.find("player"); it != d.actors.end())
        keep_player = tg::Vec2{it->second.box.x, it->second.box.y};
    import_actors(d);
    if (keep_player) {
        if (auto it = d.actors.find("player"); it != d.actors.end()) {
            it->second.box.x = keep_player->x;
            it->second.box.y = keep_player->y;
        }
    }
    ++d.reloads;
    d.has_anim = d.asset && d.asset->animation_set_count() > 0;
    if (d.has_anim) {
        d.anim.bind(d.asset->animation_set(0));
        if (!d.anim.play("idle")) d.anim.play("walk");
    }
    TraceLog(LOG_INFO, "[demo] 场景已交换（reloads=%d）", d.reloads);
}

// ── IPC handler（全部命令归属 game；engine 只传 ping） ──

tg::IpcStatus ipc_handler(Demo& d, const std::string& cmd, const tg::Json& req,
                          std::optional<tg::Json>& data, std::string& error) {
    if (cmd == "help") {
        static const char* cmds[] = {
            "ping", "help", "status", "list_entities", "get_entity",
            "query_entities", "set_entity", "spawn", "despawn",
            "layers", "solid_at", "get_tile", "reload", "screenshot",
            "log", "quit",
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
        (*data)["entities"] = static_cast<int>(d.actors.size());
        (*data)["fps"] = GetFPS();
        (*data)["uptime_s"] = std::lround(GetTime());
        (*data)["port"] = d.port;
        return tg::IpcStatus::handled;
    }
    if (cmd == "list_entities") {
        tg::Json arr = tg::Json::array();
        for (const auto& [id, a] : d.actors) arr.push_back(actor_to_json(a));
        data = tg::Json::object();
        (*data)["entities"] = arr;
        (*data)["count"] = static_cast<int>(d.actors.size());
        return tg::IpcStatus::handled;
    }
    if (cmd == "get_entity") {
        auto it = d.actors.find(req.value("id", ""));
        if (it == d.actors.end()) {
            error = "no such entity";
            return tg::IpcStatus::error;
        }
        data = tg::Json::object();
        (*data)["entity"] = actor_to_json(it->second);
        return tg::IpcStatus::handled;
    }
    if (cmd == "query_entities") {
        const std::string type_f = req.value("type", "");
        tg::Json arr = tg::Json::array();
        auto push = [&](const Actor& a) {
            if (type_f.empty() || a.type == type_f) arr.push_back(actor_to_json(a));
        };
        if (req.contains("radius")) {  // 半径模式优先
            // 类型安全：坏请求不得触发 nlohmann assert/异常杀进程
            // （engine 兜底只接得住异常；assert 必须前置判断避开）。
            if (!req["x"].is_number() || !req["y"].is_number() ||
                !req["radius"].is_number()) {
                error = "radius needs numeric x/y/radius";
                return tg::IpcStatus::error;
            }
            const float qx = req["x"].get<float>(), qy = req["y"].get<float>();
            const float rad = req["radius"].get<float>();
            for (const auto& [id, a] : d.actors) {
                const float cx = a.box.x + a.box.w / 2;
                const float cy = a.box.y + a.box.h / 2;
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
            for (const auto& [id, a] : d.actors) {
                if (a.box.x < rx + rw && a.box.x + a.box.w > rx &&
                    a.box.y < ry + rh && a.box.y + a.box.h > ry)
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
        auto it = d.actors.find(req.value("id", ""));
        if (it == d.actors.end()) {
            error = "no such entity";
            return tg::IpcStatus::error;
        }
        Actor& a = it->second;
        // 类型安全：字段存在才动；类型不符 → 报错而非 panic。
        for (const char* k : {"x", "y"}) {
            if (req.contains(k)) {
                if (!req[k].is_number()) {
                    error = std::string(k) + " must be number";
                    return tg::IpcStatus::error;
                }
                if (std::strcmp(k, "x") == 0) a.box.x = req[k].get<float>();
                else a.box.y = req[k].get<float>();
            }
        }
        if (req.contains("color")) {
            if (!req["color"].is_string()) {
                error = "color must be string";
                return tg::IpcStatus::error;
            }
            a.color = parse_color(req["color"].get_ref<const std::string&>());
        }
        data = tg::Json::object();
        (*data)["entity"] = actor_to_json(a);
        return tg::IpcStatus::handled;
    }
    if (cmd == "spawn") {
        Actor a;
        a.id = req.value("id", "spawned");
        a.type = req.value("type", "actor");
        // 类型安全：只有数值字段才取；缺失/类型错按缺省处理（不 panic）。
        a.box.x = req.contains("x") && req["x"].is_number() ? req["x"].get<float>()
                                                            : 0.0f;
        a.box.y = req.contains("y") && req["y"].is_number() ? req["y"].get<float>()
                                                            : 0.0f;
        a.box.w = req.contains("w") && req["w"].is_number() ? req["w"].get<float>()
                                                            : 16.0f;
        a.box.h = req.contains("h") && req["h"].is_number() ? req["h"].get<float>()
                                                            : 16.0f;
        a.color = req.contains("color") && req["color"].is_string()
                      ? parse_color(req["color"].get_ref<const std::string&>())
                      : tg::Color{200, 200, 200, 255};
        if (a.id.empty() || d.actors.count(a.id)) {
            error = "invalid or duplicate id";
            return tg::IpcStatus::error;
        }
        const std::string key = a.id;   // 键副本：std::move(a) 会清空 a.id
        d.actors.emplace(key, std::move(a));
        data = tg::Json::object();
        (*data)["entity"] = actor_to_json(d.actors.at(key));
        return tg::IpcStatus::handled;
    }
    if (cmd == "despawn") {
        const std::string id = req.value("id", "");
        if (id.empty() || !d.actors.erase(id)) {
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
    // 启动自检：引擎骨架回归（失败即退出，便于无窗口 CI 也覆盖）
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

        // ── WASD 移动（game 逻辑 + 静态碰撞） ──
        const float speed = 160.0f;
        const tg::Vec2 dirs[4] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        const int keys[4] = {KEY_W, KEY_S, KEY_A, KEY_D};
        bool moving = false;
        if (d.asset) {
            auto it = d.actors.find("player");
            if (it != d.actors.end()) {
                Actor& p = it->second;
                for (int i = 0; i < 4; ++i) {
                    if (IsKeyDown(keys[i])) {
                        moving = true;
                        const float nx = p.box.x + dirs[i].x * speed * dt;
                        const float ny = p.box.y + dirs[i].y * speed * dt;
                        const tg::Rect next{nx, ny, p.box.w, p.box.h};
                        if (tg::rect_hits_solid(*d.asset, next) !=
                            tg::TileQueryResult::solid) {
                            p.box.x = nx;
                            p.box.y = ny;
                        }
                    }
                }
            }
        }

        // ── Tween / 动画推进 ──
        d.tween.tick(dt);
        if (d.has_anim && d.anim.valid()) {
            const std::string cur = d.anim.clip_name();
            if (moving && cur != "walk") { d.anim.play("walk"); d.anim.advance(0); }
            else if (!moving && cur != "idle") { d.anim.play("idle"); d.anim.advance(0); }
            d.anim.advance(dt);
        }

        // ── 渲染（引擎在调用方 BeginMode2D 区间内绘制） ──
        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode2D(Camera2D{
            {d.window_w / 2.0f, d.window_h / 2.0f},  // offset
            {d.cam.x, d.cam.y},                     // target
            0.0f, 2.0f,                             // rotation / zoom
        });

        if (d.asset) tg::render_scene(*d.asset);

        // 实体显式绘制：有 sprite → 归属校验通过则画图；否则色块
        for (auto& [id, a] : d.actors) {
            const ::Color rc{a.color.r, a.color.g, a.color.b, a.color.a};
            bool drawn = false;
            if (a.sprite.has && d.asset) {
                const auto rr = tg::render_sprite(*d.asset, a.sprite,
                                                  tg::Vec2{a.box.x, a.box.y},
                                                  a.color);
                if (rr == tg::RenderResult::Drawn) drawn = true;
            }
            if (!drawn)
                DrawRectangle(static_cast<int>(a.box.x), static_cast<int>(a.box.y),
                              static_cast<int>(a.box.w), static_cast<int>(a.box.h), rc);
        }

        EndMode2D();

        // HUD
        DrawFPS(10, 10);
        DrawText(TextFormat("scene=%s actors=%d reloads=%d",
                            d.asset ? std::string(d.asset->name()).c_str() : "-",
                            static_cast<int>(d.actors.size()), d.reloads),
                 10, 36, 16, WHITE);

        // 帧末截图（game 排队；帧后 ExportImage）
        if (d.shot_requested) {
            d.shot_requested = false;
            Image img = LoadImageFromScreen();
            if (!ExportImage(img, d.shot_path.c_str()))
                TraceLog(LOG_WARNING, "[demo] 截图导出失败: %s", d.shot_path.c_str());
            UnloadImage(img);
            TraceLog(LOG_INFO, "[demo] 截图已写出: %s", d.shot_path.c_str());
        }

        EndDrawing();
    }

    d.anim.bind(tg::AnimationSet{});  // 解绑（asset 即将析构）
    d.asset.reset();
    d.ipc = tg::Ipc{};
    tg::shutdown_render();
    CloseWindow();
    return 0;
}