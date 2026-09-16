// main.cpp —— 平台跳跃范例的**窗口层**：键盘输入、渲染、固定步循环、headless 摘要。
//
// 分工：玩法/碰撞/状态机全在 sim.*（纯逻辑，无 raylib）；本文件只做三件事——
//   ① 把键盘（或 headless 的确定性脚本）翻译成 plat::Input；
//   ② 用固定步 1/60 推进：窗口模式由 tg::StepClock 以真实时间产出整步，
//      --seconds/--shot 直接跑满固定步（不依赖真实帧率，机器再忙结果也一样）；
//   ③ 渲染：render_scene 画 tile 层，game 对象（玩家/敌人/单向平台/终点旗）用
//      tg::draw_rect 显式绘制，相机跟随玩家并夹在关卡内。

#include <algorithm>  // std::min / std::max
#include <cmath>      // std::llround
#include <cstdio>

#include <raylib.h>

#include "trogue/trogue.hpp"

#include "../common/harness.hpp"  // hp::Args / open_window / capture_frame_png
#include "sim.hpp"

namespace {

constexpr int kWinW = 640, kWinH = 480, kShotSteps = 120;  // --shot ≈ 2 秒（120 步）
constexpr double kDefaultSeconds = 5.0;  // --headless 单独用时跑一段（与另一范例口径一致）

// 相机跟随玩家并夹在关卡内：关卡比窗口大才有滚动空间，否则整关居中。
Camera2D make_camera(const plat::Level& lv) {
    const float mw = lv.scene().layer(0).width * plat::kTile, mh = lv.scene().layer(0).height * plat::kTile;
    const tg::Rect p = lv.player().box();
    const float hw = kWinW / 2.0f, hh = kWinH / 2.0f;
    float cx = p.x + p.w / 2.0f, cy = p.y + p.h / 2.0f;
    cx = (mw <= kWinW) ? mw / 2.0f : std::min(std::max(cx, hw), mw - hw);
    cy = (mh <= kWinH) ? mh / 2.0f : std::min(std::max(cy, hh), mh - hh);
    return Camera2D{{cx, cy}, {cx, cy}, 0.0f, 1.0f};
}

// 世界绘制（调用方已设好 2D 变换）：tile 层 + game 对象色块。
// 单向平台不是场景里的 tile，必须由 game 按自己的碰撞矩形画出来，否则会「隐形站台」。
void draw_world(const plat::Level& lv) {
    tg::render_scene(lv.scene());
    for (const tg::Rect& r : lv.one_way()) tg::draw_rect(r, tg::Color{110, 200, 150, 255});
    tg::draw_rect(lv.goal(), tg::Color{240, 200, 80, 255});
    for (const auto& e : lv.enemies()) if (e->alive()) tg::draw_rect(e->box(), e->color());
    const plat::Player& p = lv.player();
    if (!p.invulnerable() || p.blink_visible())  // 无敌帧闪烁：显隐由逻辑层给
        tg::draw_rect(p.box(), tg::Color{235, 90, 90, 255});
}

}  // namespace

int main(int argc, char** argv) {
    const hp::Args args = hp::parse_args(
        argc, argv, "用法: platformer [--headless] [--seconds N] [--shot out.png]");
    if (args.help) return 0;
    if (!args.valid) return 2;
    if (args.headless) SetTraceLogLevel(LOG_ERROR);  // 隐藏窗口：压掉初始化噪声（留错误）
    hp::open_window("[trogue] platformer", kWinW, kWinH, args.headless);

    // 场景在内存里构造（零资产文件）：布局与配色只有 plat 侧一份（见 plat::build_scene），
    // 窗口层不再另写一套颜色/网格，避免两处各画一遍同构场景。
    plat::Level level{plat::build_scene(plat::kTile)};
    long long steps = 0;
    int exit_code = 0;

    if (!args.shot.empty()) {  // ① 截图口径：跑约 2 秒后写整帧 PNG 再退出
        for (; steps < kShotSteps; ++steps) level.step(plat::scripted_input(steps));
        // 截图失败必须非零退出：--shot 是 Agent/无显示环境的视觉验收口径，
        // 静默返回 0 会让「没有产出 PNG」被当成成功。
        if (!hp::capture_frame_png(args.shot, kWinW, kWinH, [&level]() {
                BeginMode2D(make_camera(level));
                draw_world(level);
                EndMode2D();
            }))
            exit_code = 1;
    } else if (args.headless || args.seconds > 0.0) {  // ② 验证口径：固定步跑满
        const double secs = args.seconds > 0.0 ? args.seconds : kDefaultSeconds;
        const long long total = std::llround(secs * (1.0 / plat::kFixedDt));
        std::fprintf(stderr, "[input] headless 确定性脚本：持续按住右方向，每 45 步按住跳 12 步\n");
        for (; steps < total; ++steps) level.step(plat::scripted_input(steps));
        const tg::Rect b = level.player().box();
        std::printf("steps=%lld x=%d y=%d grounded=%d deaths=%d won=%d enemies=%d\n",
                    steps, static_cast<int>(std::lround(b.x)), static_cast<int>(std::lround(b.y)),
                    level.player().grounded() ? 1 : 0, level.deaths(), level.won() ? 1 : 0, level.alive_enemies());
    } else {  // ③ 交互口径：真实键盘 + tg::StepClock（真实时间产出固定步）
        tg::StepClock clock{plat::kFixedDt, 5};
        while (!WindowShouldClose()) {
            plat::Input in;
            in.left = IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT);
            in.right = IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT);
            in.jump = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_W) || IsKeyDown(KEY_UP);
            in.restart = IsKeyPressed(KEY_R);
            const tg::StepClock::Tick t = clock.tick(GetFrameTime());
            for (int s = 0; s < t.steps; ++s) { level.step(in); ++steps; }
            BeginDrawing();
            ClearBackground(BLACK);
            BeginMode2D(make_camera(level));
            draw_world(level);
            EndMode2D();
            DrawText(TextFormat("A/D move  SPACE jump  R restart  |  deaths=%d  won=%d",
                                level.deaths(), level.won() ? 1 : 0), 10, 10, 16, WHITE);
            EndDrawing();
        }
    }

    tg::shutdown_render();
    CloseWindow();
    return exit_code;
}
