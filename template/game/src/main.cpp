// main.cpp —— 起步游戏骨架（trogue 模板）。
//
// 这是新游戏的起点：一个能跑、能被 Agent 迭代的最小闭环——
//   - 场景默认**在内存里构造**（tg::SceneAsset::load_json），零资产文件即可运行；
//     传 `--scene <assets 相对路径>` 则改从磁盘加载，失败回退内置场景；
//   - render_scene 画 tile 层，显式绘制 game 自己的对象（色块/贴图）；
//   - WASD / 方向键 单格移动：地形 solid 用引擎 tile 查询裁决，视觉用引擎
//     TweenManager 驱动、播完精确落格（避免浮点残差与像素抖动）；
//   - 若存在 assets/scenes 目录则监听其 .json（热重载）+ F5 手动重载；
//   - IPC（DEBUG）：status / list_entities / get_entity / move / screenshot /
//     log / quit —— 非视觉 Agent 可据此观测与驱动游戏。
//
// 只经 engine/include/trogue/*.hpp 公共头使用引擎；玩法（对象模型、输入、
// 规则、AI、动画触发）都在本文件/本游戏内实现。把这里当成草稿纸，随游戏
// 设计随意改写；引擎不需要动。
//
// 运行： ./build/bin/trogue [--scene assets/scenes/xxx.json] [--port 48764]
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <vector>

#include <raylib.h>
#include <rlgl.h>  // rlDrawRenderBatchActive（截图前强制 flush 渲染批）

#include "trogue/trogue.hpp"

namespace {

constexpr int kTileSize = 16;           // 场景 tile 尺寸
constexpr float kMoveDuration = 0.12f;  // 单格移动动画时长（秒）

// ── 内置起步场景（palette 模式，四面墙 + 玩家 + 木箱）──
// 用 tg::Json 逐层构造，避免手写超长 tiles 数组；与磁盘 tro-scene 完全同构。
constexpr int kMapW = 20, kMapH = 15;

std::string builtin_scene_json() {
    tg::Json ground = tg::Json::array();
    tg::Json walls = tg::Json::array();
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            ground.push_back(0);
            const bool edge = (x == 0 || y == 0 || x == kMapW - 1 || y == kMapH - 1);
            walls.push_back(edge ? 1 : -1);
        }
    }
    auto make_layer = [](const char* name, bool solid, const tg::Json& tiles) {
        tg::Json l;
        l["name"] = name;
        l["width"] = kMapW;
        l["height"] = kMapH;
        l["solid"] = solid;
        l["tiles"] = tiles;
        return l;
    };

    tg::Json palette = tg::Json::array({"#2a2d3a", "#7f8ca3"});
    tg::Json layers = tg::Json::array();
    layers.push_back(make_layer("ground", false, ground));
    layers.push_back(make_layer("walls", true, walls));

    tg::Json tilemap;
    tilemap["tile_width"] = kTileSize;
    tilemap["tile_height"] = kTileSize;
    tilemap["palette"] = palette;
    tilemap["layers"] = layers;

    auto make_entity = [](const char* id, const char* type, int gx, int gy,
                          const char* color) {
        tg::Json e;
        e["id"] = id;
        e["type"] = type;
        e["x"] = gx * kTileSize;
        e["y"] = gy * kTileSize;
        e["w"] = kTileSize;
        e["h"] = kTileSize;
        e["color"] = color;
        return e;
    };
    tg::Json entities = tg::Json::array();
    entities.push_back(make_entity("player", "player", 10, 7, "#e94560"));
    entities.push_back(make_entity("crate_1", "crate", 14, 7, "#8a6f4c"));

    tg::Json meta;
    meta["name"] = "builtin";
    meta["background"] = "#101018";

    tg::Json scene;
    scene["format"] = "tro-scene";
    scene["version"] = 2;
    scene["meta"] = meta;
    scene["tilemap"] = tilemap;
    scene["entities"] = entities;
    return scene.dump();
}

// ── game 自己的对象模型（引擎不认识它） ──
struct Actor {
    std::string id;
    std::string type;
    int gx = 0, gy = 0;  // 逻辑格坐标
    bool is_player = false;
    tg::Color color{255, 255, 255, 255};
    tg::SpriteDesc sprite;  // 视觉快照（has==false → 画色块）
};

// ── 应用状态 ──
struct Game {
    std::unique_ptr<tg::SceneAsset> asset;  // 只读资产（帧外 swap）
    std::vector<Actor> actors;
    int map_w = 0, map_h = 0;
    int reloads = 0;

    // 玩家视觉位置（引擎 TweenManager 驱动；静止时 == 逻辑格像素）
    float view_x = 0, view_y = 0;
    bool view_init = false;
    tg::TweenManager::Id view_tween = 0;
    tg::TweenManager tween;

    int window_w = 640, window_h = 480;
    bool quit = false;

    tg::Ipc ipc;
    int port = tg::kIpcPortDefault;

    tg::Watcher watcher;
    std::string scene_path;  // 空 = 用内置场景；否则磁盘路径

    bool shot_requested = false;
    std::string shot_path;
};

// ── 从 SceneAsset 导入 actors（descriptor → game 对象，复制语义） ──
void import_scene(Game& g, const tg::SceneAsset& asset) {
    g.actors.clear();
    g.map_w = 0;
    g.map_h = 0;
    if (asset.layer_count() > 0) {
        g.map_w = asset.layer(0).width;
        g.map_h = asset.layer(0).height;
    }
    for (int i = 0; i < asset.entity_count(); ++i) {
        const tg::SceneEntity d = asset.entity(i);  // 值快照
        Actor a;
        a.id = d.id;
        a.type = d.type;
        a.gx = static_cast<int>(d.x) / kTileSize;
        a.gy = static_cast<int>(d.y) / kTileSize;
        a.is_player = (d.type == "player");
        a.color = d.color;
        a.sprite = d.sprite;
        g.actors.push_back(std::move(a));
    }
}

Actor* find_actor(Game& g, const std::string& id) {
    for (auto& a : g.actors)
        if (a.id == id) return &a;
    return nullptr;
}

bool tile_blocked_terrain(const tg::SceneAsset& asset, int gx, int gy) {
    return tg::is_solid_at(
               asset, tg::Vec2{gx * kTileSize + kTileSize / 2.0f,
                               gy * kTileSize + kTileSize / 2.0f}) ==
           tg::TileQueryResult::solid;
}

bool tile_has_other(const Game& g, int gx, int gy, const std::string& self) {
    for (const auto& a : g.actors)
        if (a.id != self && a.gx == gx && a.gy == gy) return true;
    return false;
}

// ── 移动裁决：地形 solid + 实体互斥 + 边界 ──
bool try_move(Game& g, int dx, int dy) {
    Actor* p = nullptr;
    for (auto& a : g.actors)
        if (a.is_player) p = &a;
    if (!p || !g.asset || (dx == 0 && dy == 0)) return false;

    const int nx = p->gx + dx;
    const int ny = p->gy + dy;
    if (nx < 0 || ny < 0 || nx >= g.map_w || ny >= g.map_h) return false;
    if (tile_blocked_terrain(*g.asset, nx, ny)) return false;
    if (tile_has_other(g, nx, ny, p->id)) return false;

    p->gx = nx;
    p->gy = ny;

    // 引擎 TweenManager 驱动视觉：精确落格（终值 = 目标整数像素，无残差）
    const float tx = nx * kTileSize;
    const float ty = ny * kTileSize;
    if (!g.view_init) {
        g.view_x = tx;
        g.view_y = ty;
        g.view_init = true;
    }
    g.tween.cancel(g.view_tween);
    g.view_tween = g.tween.add_vec2(
        tg::Vec2{g.view_x, g.view_y}, tg::Vec2{tx, ty},
        tg::TweenSpec{kMoveDuration, 0.0, 0, tg::Easing::quad_out},
        [&g](const tg::TweenManager::Sample<tg::Vec2>& s) {
            g.view_x = s.value.x;
            g.view_y = s.value.y;
        },
        [&g, tx, ty]() { g.view_x = tx; g.view_y = ty; });
    return true;
}

// 首帧/重载后把玩家视觉位置对齐逻辑格（静止时 visual == 逻辑格像素）
void init_player_view_if_needed(Game& g) {
    if (g.view_init) return;
    for (const auto& a : g.actors) {
        if (a.is_player) {
            g.view_x = a.gx * kTileSize;
            g.view_y = a.gy * kTileSize;
            g.view_init = true;
            return;
        }
    }
}

bool dir_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// ── 加载/换场景：candidate load 成功才 swap（失败保留旧场景） ──
// path 为空（或用磁盘加载失败且当前无场景）→ 回退内置场景。
void load_scene(Game& g, const std::string& path) {
    tg::expected<tg::SceneAsset, tg::Error> loaded{tl::unexpected(
        tg::Error{tg::ErrorCode::kInvalidArgument, "no scene path"})};
    if (!path.empty()) {
        loaded = tg::SceneAsset::load(path);
        if (!loaded)
            TraceLog(LOG_WARNING, "[game] 场景加载失败，回退内置场景: %s",
                     loaded.error().message.c_str());
    }
    if (!loaded) {
        const std::string text = builtin_scene_json();
        loaded = tg::SceneAsset::load_json(text, "<builtin>");
    }
    if (!loaded) {
        TraceLog(LOG_ERROR, "[game] 内置场景解析失败: %s",
                 loaded.error().message.c_str());
        return;  // 保留旧场景
    }
    g.asset = std::make_unique<tg::SceneAsset>(std::move(*loaded));
    import_scene(g, *g.asset);
    // 打断进行中的移动 tween（旧场景目标格已失效；否则回调会把视觉拉回旧目标，
    // 造成逻辑/视觉永久失步）
    g.tween.cancel_all();
    g.view_tween = 0;
    g.view_init = false;  // 玩家视觉位置下一帧按新逻辑位置初始化
    g.reloads += 1;
    TraceLog(LOG_INFO, "[game] 场景已加载: %s (actors=%d)",
             std::string(g.asset->name()).c_str(),
             static_cast<int>(g.actors.size()));
}

// ── JSON 快照 ──
tg::Json actor_to_json(const Game& g, const Actor& a) {
    tg::Json j{
        {"id", a.id}, {"type", a.type},
        {"x", a.gx * kTileSize}, {"y", a.gy * kTileSize},
        {"w", kTileSize}, {"h", kTileSize},
    };
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", a.color.r, a.color.g,
                  a.color.b);
    j["color"] = buf;
    // transform：inspector 视图（静止时 visual == 逻辑格像素）
    const bool player = a.is_player;
    const float vx = player ? g.view_x : a.gx * kTileSize;
    const float vy = player ? g.view_y : a.gy * kTileSize;
    j["transform"] = tg::Json{{"visual", {vx, vy}},
                              {"moving", player && g.tween.alive(g.view_tween)}};
    return j;
}

tg::IpcStatus ipc_handler(Game& g, const std::string& cmd, const tg::Json& req,
                          std::optional<tg::Json>& data, std::string& err) {
    if (cmd == "help") {
        tg::Json arr = tg::Json::array();
        for (const char* c : {"status", "list_entities", "get_entity",
                              "move", "screenshot", "log", "quit"})
            arr.push_back(c);
        data = tg::Json{{"commands", arr}};
        return tg::IpcStatus::handled;
    }
    if (cmd == "status") {
        data = tg::Json{{"scene", g.asset ? std::string(g.asset->name()) : "-"},
                        {"reloads", g.reloads},
                        {"entities", static_cast<int>(g.actors.size())},
                        {"fps", GetFPS()},
                        {"port", g.port}};
        return tg::IpcStatus::handled;
    }
    if (cmd == "list_entities") {
        tg::Json arr = tg::Json::array();
        for (const auto& a : g.actors) arr.push_back(actor_to_json(g, a));
        data = tg::Json{{"entities", arr}, {"count", arr.size()}};
        return tg::IpcStatus::handled;
    }
    if (cmd == "get_entity") {
        if (!req.contains("id") || !req.at("id").is_string()) {
            err = "get_entity needs field: id (string)";
            return tg::IpcStatus::error;
        }
        Actor* a = find_actor(g, req.at("id").get<std::string>());
        if (!a) {
            err = "no such entity: " + req.at("id").get<std::string>();
            return tg::IpcStatus::error;
        }
        data = tg::Json{{"entity", actor_to_json(g, *a)}};
        return tg::IpcStatus::handled;
    }
    if (cmd == "move") {
        // dx/dy 必须为整数（拒绝 1.9 这类浮点静默截断）且 |d|≤1（单格移动；
        // 多格会被 try_move 只校验终点、绕过中间碰撞）。用 int64 判值再收窄，
        // 避免超范围整数经 get<int>() 截断后被误判为合法。
        std::int64_t dx = 0, dy = 0;
        auto int64_of = [](const tg::Json& v, std::int64_t& out) {
            if (!v.is_number_integer()) return false;
            const std::int64_t n = v.get<std::int64_t>();
            if (n < -1 || n > 1) return false;  // 越界即非法（单格约束）
            out = n;
            return true;
        };
        const tg::Json zero = 0;
        if (!int64_of(req.contains("dx") ? req.at("dx") : zero, dx) ||
            !int64_of(req.contains("dy") ? req.at("dy") : zero, dy)) {
            err = "move dx/dy must be integers in [-1,1] (single-cell)";
            return tg::IpcStatus::error;
        }
        const bool ok = try_move(g, static_cast<int>(dx), static_cast<int>(dy));
        data = tg::Json{{"moved", ok}};
        return tg::IpcStatus::handled;
    }
    if (cmd == "screenshot") {
        const std::string def =
            "screenshot_" +
            std::to_string(static_cast<long long>(GetTime())) + ".png";
        g.shot_path = req.value("path", def);
        g.shot_requested = true;
        data = tg::Json{{"path", g.shot_path}};
        return tg::IpcStatus::handled;
    }
    if (cmd == "log") {
        TraceLog(LOG_INFO, "[game] %s", req.value("msg", "").c_str());
        data = tg::Json{{"logged", true}};
        return tg::IpcStatus::handled;
    }
    if (cmd == "quit") {
        g.quit = true;
        data = tg::Json{{"bye", true}};
        return tg::IpcStatus::handled;
    }
    return tg::IpcStatus::not_handled;
}

}  // namespace

int main(int argc, char** argv) {
    Game g;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc)
            g.scene_path = argv[++i];
        else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            g.port = std::atoi(argv[++i]);
    }

    InitWindow(g.window_w, g.window_h, "[trogue] game");
    SetTargetFPS(60);
    load_scene(g, g.scene_path);

    g.ipc = tg::Ipc::create(static_cast<std::uint16_t>(g.port));
    if (g.ipc.valid()) {
        g.ipc.set_handler([&g](const std::string& cmd, const tg::Json& req,
                               std::optional<tg::Json>& data, std::string& err) {
            return ipc_handler(g, cmd, req, data, err);
        });
        TraceLog(LOG_INFO, "[game] IPC 监听端口 %d", g.port);
    } else {
        TraceLog(LOG_INFO, "[game] IPC 不可用（Release 桩/端口占用）");
    }

    // 热重载仅在存在资产目录时启用；无资产项目不建 watcher。
    bool hot_reload = !g.scene_path.empty() && dir_exists("assets/scenes");
    if (hot_reload) {
        g.watcher = tg::Watcher::create("assets/scenes");
        if (!g.watcher.valid())
            TraceLog(LOG_INFO, "[game] watcher 不可用（非 Debug/非 Linux）");
    }

    while (!WindowShouldClose() && !g.quit) {
        const float dt = GetFrameTime();
        // 视觉对齐先于 IPC/输入：任何来源的首帧行动都不会从 (0,0) 起 tween
        init_player_view_if_needed(g);
        g.ipc.poll();

        if (g.watcher.valid()) {
            if (auto changed = g.watcher.poll()) {
                TraceLog(LOG_INFO, "[game] watcher: %s → 重载", changed->c_str());
                load_scene(g, g.scene_path);
            }
        }
        if (IsKeyPressed(KEY_F5)) load_scene(g, g.scene_path);

        // 输入 → 单格移动（4 向）
        int dx = 0, dy = 0;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) dx = 1;
        else if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) dx = -1;
        else if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) dy = 1;
        else if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) dy = -1;
        if (dx || dy) try_move(g, dx, dy);

        g.tween.tick(dt);

        // ── 渲染 ──
        BeginDrawing();
        ClearBackground(BLACK);
        BeginMode2D(Camera2D{{g.window_w / 2.0f, g.window_h / 2.0f},
                             {g.map_w * kTileSize / 2.0f,
                              g.map_h * kTileSize / 2.0f},
                             0.0f, 2.0f});
        if (g.asset) {
            tg::render_scene(*g.asset);
            // 显式绘制 game 对象（按 y 排序，稳定）；引擎不隐式遍历实体
            std::vector<const Actor*> order;
            for (const auto& a : g.actors) order.push_back(&a);
            std::stable_sort(order.begin(), order.end(),
                             [](const Actor* l, const Actor* r) {
                                 return l->gy < r->gy;
                             });
            for (const Actor* a : order) {
                const float wx = a->is_player ? g.view_x : a->gx * kTileSize;
                const float wy = a->is_player ? g.view_y : a->gy * kTileSize;
                if (a->sprite.has &&
                    tg::render_sprite(*g.asset, a->sprite, tg::Vec2{wx, wy},
                                      a->color) != tg::RenderResult::Invalid)
                    continue;
                // 无贴图/贴图缺失 → 色块（引擎 draw_rect 内部为浮点，不截断）
                tg::draw_rect(tg::Rect{wx, wy, kTileSize, kTileSize}, a->color);
            }
        }
        EndMode2D();
        DrawFPS(10, 10);
        DrawText(TextFormat("scene=%s actors=%d reloads=%d",
                            g.asset ? std::string(g.asset->name()).c_str() : "-",
                            static_cast<int>(g.actors.size()), g.reloads),
                 10, 36, 16, WHITE);

        if (g.shot_requested) {
            g.shot_requested = false;
            // 强制 flush 渲染批后读屏导出（raylib TakeScreenshot 会破坏绝对路径；
            // 不 flush 会拍到未绘制的残缺帧）
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            ExportImage(img, g.shot_path.c_str());
            UnloadImage(img);
            TraceLog(LOG_INFO, "[game] 截图已写出: %s", g.shot_path.c_str());
        }
        EndDrawing();
    }

    g.asset.reset();
    g.ipc = tg::Ipc{};
    tg::shutdown_render();
    CloseWindow();
    return 0;
}
