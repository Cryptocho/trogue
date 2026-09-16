// main.cpp —— swarm 范例的窗口层：命令行、固定步循环、渲染、截图、摘要。
//
// 逻辑一行都不在这里：本文件只采集键盘（→ swarm::Input）、按固定步推进 sim、
// 把实体画成色块。这样同一份规则既能在窗口里玩，也能在 sim_test 里无窗口重放。
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <raylib.h>

#include "../common/harness.hpp"  // 共用骨架：参数解析 / 窗口 / 整帧截图 / 内存场景
#include "sim.hpp"

namespace {

constexpr int kWinW = swarm::kRoomW * swarm::kTile;  // 窗口 = 房间像素尺寸（相机 1:1）
constexpr int kWinH = swarm::kRoomH * swarm::kTile;
constexpr float kShotSeconds = 2.0f;     // --shot 前先跑一段，让场面有内容
constexpr float kDefaultSeconds = 5.0f;  // --headless 单独用时跑一段（不可交互）

// 键盘意图：WASD/方向键移动，R 重开
swarm::Input read_input() {
    swarm::Input in;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) in.mx -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) in.mx += 1.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) in.my -= 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) in.my += 1.0f;
    in.restart = IsKeyPressed(KEY_R);
    return in;
}

// 实体色块配色（渲染决策，故留在窗口层；sim 只提供 tag 与 AABB）
tg::Color color_of(std::uint8_t tag) {
    if (tag == swarm::kPlayer) return tg::Color{255, 77, 109, 255};
    if (tag == swarm::kEnemy) return tg::Color{240, 160, 80, 255};
    if (tag == swarm::kBullet) return tg::Color{255, 230, 120, 255};
    return tg::Color{90, 220, 220, 255};  // kOrb
}

}  // namespace

int main(int argc, char** argv) {
    const hp::Args args = hp::parse_args(
        argc, argv, "用法: swarm [--headless] [--seconds <N>] [--shot <path>]");
    if (!args.valid || args.help) return args.valid ? 0 : 2;
    if (args.headless) SetTraceLogLevel(LOG_ERROR);  // 隐藏窗口：压掉初始化噪声（留错误）

    // 场景与碰撞掩码同源（同一个 '#' 边界环）：画出来的墙就是挡住实体的墙
    std::vector<std::string> rows(swarm::kRoomH, std::string(swarm::kRoomW, '.'));
    for (int y = 0; y < swarm::kRoomH; ++y) for (int x = 0; x < swarm::kRoomW; ++x)
        if (swarm::room_solid(x, y)) rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = '#';
    tg::SceneAsset scene = hp::make_ascii_scene(
        rows, swarm::kTile, tg::Color{32, 34, 44, 255}, tg::Color{108, 120, 147, 255},
        tg::Color{16, 18, 24, 255}, "swarm");
    // 掩码由引擎按同一谓词构造（RAII 自持 buffer；layer_id = -1 表示非资产来源）
    auto grid_or = tg::SolidGrid::create(swarm::kRoomW, swarm::kRoomH, swarm::kTile,
                                         swarm::kTile, swarm::room_solid);
    if (!grid_or) {  // 入参是编译期常量：失败即程序错误
        std::fprintf(stderr, "[swarm] 掩码构造失败: %s\n", grid_or.error().message.c_str());
        return 1;
    }
    const tg::SolidGrid& grid = *grid_or;
    const tg::SolidGridView& view = grid.view();

    swarm::World world;
    swarm::reset_round(world);
    hp::open_window("swarm", kWinW, kWinH, args.headless);
    SetTargetFPS(60);

    // 一帧绘制：tile 层交给引擎，实体由本层显式画色块；相机固定看房间中心
    auto draw_frame = [&]() {
        const Vector2 c{kWinW * 0.5f, kWinH * 0.5f};
        BeginMode2D(Camera2D{c, c, 0.0f, 1.0f});
        tg::render_scene(scene);
        for (int i = 0; i < swarm::count(world); ++i)
            tg::draw_rect(swarm::rect_of(world, i), color_of(world.tag[i]));
        EndMode2D();
    };

    const float secs = args.seconds > 0.0 ? static_cast<float>(args.seconds) : kDefaultSeconds;
    const long long shot_steps = static_cast<long long>(kShotSeconds / swarm::kDt + 0.5);
    const long long target = static_cast<long long>(secs / swarm::kDt + 0.5);

    if (args.headless) {  // 隐藏窗口：直接跑固定步，不依赖真实时间 → 摘要只与 seed/步数有关
        const long long n = args.shot.empty() ? target : shot_steps;
        for (long long i = 0; i < n; ++i) swarm::step(world, swarm::Input{}, &view, 1, swarm::kDt);
    } else {
        tg::StepClock clock{swarm::kDt, 5};  // 窗口模式：按真实帧时长产出整步
        const long long stop_at = args.shot.empty() ? (args.seconds > 0.0 ? target : -1) : shot_steps;
        while (!WindowShouldClose()) {
            const swarm::Input in = read_input();
            const tg::StepClock::Tick t = clock.tick(GetFrameTime());
            for (int s = 0; s < t.steps; ++s) swarm::step(world, in, &view, 1, swarm::kDt);
            BeginDrawing();
            ClearBackground(BLACK);
            draw_frame();
            DrawText(TextFormat("steps=%lld hp=%d kills=%d lvl=%d deaths=%d", static_cast<long long>(world.steps), swarm::player_hp(world), world.kills, world.level, world.deaths), 6, 6, 16, RAYWHITE);
            EndDrawing();
            if (stop_at >= 0 && static_cast<long long>(world.steps) >= stop_at) break;
        }
    }

    if (!args.shot.empty()) {
        // 截图失败必须非零退出：--shot 是 Agent/无显示环境的视觉验收口径，
        // 静默返回 0 会让「没有产出 PNG」被当成成功。
        if (!hp::capture_frame_png(args.shot, kWinW, kWinH, draw_frame)) return 1;
    } else {
        std::printf("%s\n", swarm::summary(world).c_str());  // 一行 key=value 摘要
    }
    tg::shutdown_render();
    CloseWindow();
    return 0;
}
