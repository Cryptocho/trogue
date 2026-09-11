// anim_viewer.cpp —— 动画查看器：引擎帧动画触发/切换的最小交互验证台。
//
// 边界：engine 只做动画「执行原语」，触发/切换决策归 game 层——本工具即
// game 层消费者：
//   - 任意键 → 播放/暂停切换（同帧多键只切一次，防奇偶抵消；ESC 为 raylib
//     默认退出键，循环条件先行退出，不参与切换）
//   - 鼠标左键 → clip 按资产枚举序轮转（暂停中点击 = 切换并恢复播放——
//     play() 清暂停的引擎语义，如实呈现）
//   - IPC 与键鼠共用同一组动作函数（toggle_pause/next_clip/play_clip）：
//     E2E 走 IPC 即覆盖逻辑本体，原始键鼠映射为薄 if 层
// 不引入 game_core（无回合/战斗/AI）；实体数据取 SceneEntity 值快照（只读），
// 引擎不保存可变实体状态。单场景运行，无热重载。
//
// 用法：./build/bin/anim_viewer [--scene assets/scenes/xxx.json]
//                               [--port 48765] [--zoom 3]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

#include <raylib.h>

#include "trogue/trogue.hpp"
#include "anim_util.hpp"

namespace {

// ── 缺省值 ──
constexpr int kDefaultPort = 48765;  // 与 demo 48764 可并存
constexpr int kDefaultZoom = 3;
constexpr int kWindowW = 960, kWindowH = 540;
constexpr const char* kDefaultScene =
    "assets/scenes/soldier_animated_sprite_2d.json";

struct Viewer {
    std::unique_ptr<tg::SceneAsset> asset;
    tg::SceneEntity ent;        // 首个实体快照（只读；x/y/color/sprite）
    tg::AnimationSet set;       // 动画集视图（存活期 = asset）
    tg::AnimationPlayer anim;
    int clip_index = -1;        // 当前 clip 序号（播放器只暴露名字，轮转序号自持）
    int zoom = kDefaultZoom;
    int port = kDefaultPort;
    bool quit = false;
    tg::Ipc ipc;
};

// ── 动作函数（键鼠与 IPC 的唯一逻辑入口） ──

void toggle_pause(Viewer& v) {
    if (v.anim.paused()) {
        v.anim.resume();
        TraceLog(LOG_INFO, "[viewer] resume");
    } else {
        v.anim.pause();
        TraceLog(LOG_INFO, "[viewer] pause");
    }
}

void play_clip(Viewer& v, int index) {
    const auto name = v.set.clip_name(index);
    if (!name || !v.anim.play(*name)) return;
    v.clip_index = index;
    TraceLog(LOG_INFO, "[viewer] play clip[%d]=%.*s", index,
             static_cast<int>(name->size()), name->data());
}

void next_clip(Viewer& v) {
    const int n = v.set.clip_count();
    if (n <= 0) return;  // 无动画场景防模零（不崩目标）
    play_clip(v, (v.clip_index + 1 + n) % n);
}

// ── 帧绘制（屏幕与离屏截图共用；调用方负责 BeginDrawing/BeginTextureMode） ──
void draw_viewer(const Viewer& v) {
    // 相机：target 世界原点 + zoom N（已知假设：士兵视觉中心
    // 恰为原点；POINT 过滤整数倍 = 最近邻无损放大）
    BeginMode2D(Camera2D{{kWindowW / 2.0f, kWindowH / 2.0f},
                         {0.0f, 0.0f},
                         0.0f,
                         static_cast<float>(v.zoom)});

    if (v.asset) tg::render_scene(*v.asset);

    // 士兵绘制（共享工具 anim_util）：动画集已绑定 → 采样当前帧；
    // 机制（offset 组合 + 静态回退 + 色块兜底）在 game::draw_entity_sprite
    if (v.asset)
        game::draw_entity_sprite(*v.asset,
                                 v.set.clip_count() > 0 ? &v.anim : nullptr,
                                 v.ent.sprite, tg::Vec2{v.ent.x, v.ent.y},
                                 v.ent.color, v.ent.w, v.ent.h);

    EndMode2D();

    // HUD（屏幕空间；DrawFPS 20px + 3 行 16px 全部 y<96 <120——不侵入
    // 士兵 3x 比对区 screen (330,120)..(630,420)，像素比对不受 HUD 干扰）
    DrawFPS(10, 10);
    DrawText(TextFormat("clip=%s [%d/%d] %s", v.anim.clip_name().c_str(),
                        v.clip_index + 1, v.set.clip_count(),
                        v.anim.paused() ? "PAUSED" : "PLAYING"),
             10, 30, 16, v.anim.paused() ? YELLOW : WHITE);
    DrawText(TextFormat("zoom=%d scene=%s", v.zoom,
                        v.asset ? std::string(v.asset->name()).c_str() : "-"),
             10, 50, 16, WHITE);
    DrawText("any key: play/pause | click: next clip | ESC: quit",
             10, 70, 16, GRAY);
}

// status 数据（IPC 返回与调试一致）
tg::Json status_json(const Viewer& v) {
    tg::Json j = tg::Json::object();
    j["scene"] = v.asset ? std::string(v.asset->name()) : "-";
    j["clip"] = v.anim.clip_name();
    j["clip_index"] = v.clip_index;
    j["frame"] = v.anim.frame_index();
    j["paused"] = v.anim.paused();
    j["playing"] = v.anim.playing();
    j["zoom"] = v.zoom;
    j["fps"] = GetFPS();
    return j;
}

// ── IPC handler（命令语义在 game 层；engine 只传 ping——viewer 是与 demo
//    平级的独立端点，端口 48765，命令表不与 demo 混淆） ──
tg::IpcStatus ipc_handler(Viewer& v, const std::string& cmd,
                          const tg::Json& req, std::optional<tg::Json>& data,
                          std::string& err) {
    if (cmd == "status") {
        data = status_json(v);
        return tg::IpcStatus::handled;
    }
    if (cmd == "help") {
        tg::Json arr = tg::Json::array();
        arr.push_back("status");
        arr.push_back("anim (op: toggle_pause|next_clip|play [clip:<名>])");
        arr.push_back("screenshot (path?)");
        arr.push_back("quit");
        data = tg::Json{{"commands", arr}};
        return tg::IpcStatus::handled;
    }
    if (cmd == "anim") {
        const std::string op = req.value("op", "");
        if (op == "toggle_pause") {
            toggle_pause(v);
        } else if (op == "next_clip") {
            next_clip(v);
        } else if (op == "play") {
            if (!req.contains("clip") || !req.at("clip").is_string()) {
                err = "anim play needs field: clip (string)";  // wire 消息英文，对齐 demo
                return tg::IpcStatus::error;
            }
            const std::string clip = req.at("clip").get<std::string>();
            bool found = false;
            for (int i = 0; i < v.set.clip_count(); ++i) {
                if (const auto name = v.set.clip_name(i);
                    name && *name == clip) {
                    play_clip(v, i);
                    found = true;
                    break;
                }
            }
            if (!found) {
                err = "unknown clip: " + clip;
                return tg::IpcStatus::error;
            }
        } else {
            err = "unknown op: " + op + " (toggle_pause | next_clip | play)";
            return tg::IpcStatus::error;
        }
        data = status_json(v);  // 操作后状态同构返回（调用方免二次查询）
        return tg::IpcStatus::handled;
    }
    if (cmd == "screenshot") {
        const std::string def =
            "anim_view_" + std::to_string(static_cast<long long>(GetTime())) +
            ".png";
        const std::string path = req.value("path", def);
        // 同步离屏截图（复用同一 draw_viewer；不依赖屏幕缓冲，可无头）
        const game::ShotResult sr = game::capture_offscreen_png(
            kWindowW, kWindowH, path, [&v] { draw_viewer(v); });
        data = tg::Json{{"path", path},
                        {"ok", sr.ok},
                        {"w", sr.w},
                        {"h", sr.h},
                        {"bytes", sr.bytes}};
        if (sr.ok)
            TraceLog(LOG_INFO, "[viewer] 截图已写出: %s", path.c_str());
        else
            TraceLog(LOG_WARNING, "[viewer] 截图失败: %s", path.c_str());
        return tg::IpcStatus::handled;
    }
    if (cmd == "quit") {
        v.quit = true;
        data = tg::Json{{"bye", true}};
        return tg::IpcStatus::handled;
    }
    return tg::IpcStatus::not_handled;
}

}  // namespace

int main(int argc, char** argv) {
    Viewer v;
    std::string scene_path = kDefaultScene;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc)
            scene_path = argv[++i];
        else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            v.port = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--zoom") == 0 && i + 1 < argc)
            v.zoom = std::atoi(argv[++i]);
    }
    if (v.zoom < 1) {
        TraceLog(LOG_WARNING, "[viewer] --zoom 非法（须整数>=1），回退缺省 3");
        v.zoom = kDefaultZoom;
    }
    std::printf("[viewer] trogue %s 动画查看器就绪\n", tg::version_string());

    InitWindow(kWindowW, kWindowH, "[trogue] anim viewer");
    SetTargetFPS(60);

    // ── 场景加载（单场景，无热重载） ──
    auto loaded = tg::SceneAsset::load(scene_path);
    if (!loaded) {
        TraceLog(LOG_ERROR, "[viewer] 场景加载失败: %s",
                 loaded.error().message.c_str());
        CloseWindow();
        return 1;
    }
    v.asset = std::make_unique<tg::SceneAsset>(std::move(*loaded));
    if (v.asset->entity_count() > 0) v.ent = v.asset->entity(0);

    // 动画绑定：set 0，play("idle") fallback 第一个 clip
    // （与 demo reload 语义一致）；无动画集 → 仅静态展示，交互 no-op 不崩
    if (v.asset->animation_set_count() > 0) {
        v.set = v.asset->animation_set(0);
        v.anim.bind(v.set);
        int idx0 = 0;
        for (int i = 0; i < v.set.clip_count(); ++i) {
            if (const auto name = v.set.clip_name(i); name && *name == "idle") {
                idx0 = i;
                break;
            }
        }
        play_clip(v, idx0);
    } else {
        TraceLog(LOG_WARNING, "[viewer] 场景无动画集：仅静态展示");
    }

    v.ipc = tg::Ipc::create(static_cast<std::uint16_t>(v.port));
    if (v.ipc.valid()) {
        v.ipc.set_handler([&v](const std::string& cmd, const tg::Json& req,
                               std::optional<tg::Json>& data,
                               std::string& err) {
            return ipc_handler(v, cmd, req, data, err);
        });
        TraceLog(LOG_INFO, "[viewer] IPC 监听端口 %d", v.port);
    } else {
        TraceLog(LOG_INFO, "[viewer] IPC 不可用（Release 桩/端口占用）");
    }

    while (!WindowShouldClose() && !v.quit) {
        const float dt = GetFrameTime();
        v.ipc.poll();

        // ── 输入 → 动作：任意键 toggle 一次（drain 全队列，
        // 同帧多键防奇偶抵消）；ESC 由 raylib 默认退出键走 WindowShouldClose，
        // 不参与切换 ──
        bool any_key = false;
        for (int k; (k = GetKeyPressed()) != KEY_NULL;) {
            if (k != KEY_ESCAPE) any_key = true;
        }
        if (any_key) toggle_pause(v);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) next_clip(v);

        // ── 推进：暂停时 advance 挂起时间（返回 true），current_frame 冻结 ──
        v.anim.advance(dt);

        BeginDrawing();
        ClearBackground(BLACK);
        draw_viewer(v);
        EndDrawing();
    }

    v.anim.bind(tg::AnimationSet{});  // 解绑（asset 即将析构）
    v.asset.reset();
    v.ipc = tg::Ipc{};
    tg::shutdown_render();
    CloseWindow();
    return 0;
}
