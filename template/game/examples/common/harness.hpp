#pragma once
// harness.hpp —— 两个范式范例共用的窗口层骨架（模板自有文件，不属于引擎）。
//
// 只做与玩法无关的三件事：
//   ① 命令行解析：`--headless` / `--seconds <N>` / `--shot <path>`；
//   ② 窗口初始化与**离屏整帧截图**；
//   ③ 内存场景构造转发（实现在 scene_make.hpp——那个头不 include raylib，
//      纯逻辑层也能用；本头包含它，窗口层写一个头就够）。
//
// 为什么不直接用引擎的 tg::render_scene_to_png：它只画 tile 层，不含 game 自己的
// 实体/色块，而范例验收要的是「整帧」。故这里由 game 侧渲染到 RenderTexture 再导
// 出——引擎提供的是 tilemap 绘制原语，画什么、何时取帧属 game。
// 平台约束：Raylib 的 LoadImageFromScreen 在 Wayland 下对隐藏窗口/只画了一帧的窗口
// 会拍到黑帧；RenderTexture → LoadImageFromTexture 在两种后端都稳，且隐藏窗口可用。
//
// 用法（范例的 main.cpp）：
//   const hp::Args args = hp::parse_args(argc, argv, "用法: ...");
//   hp::open_window("swarm", 960, 640, args.headless);
//   tg::SceneAsset scene = hp::make_ascii_scene(rows, 16, floor, wall, bg, "swarm");

#include <algorithm>  // std::max（ASCII 网格取最大宽度）
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <raylib.h>
#include <rlgl.h>  // rlDrawRenderBatchActive（取帧前强制 flush 渲染批）

#include "trogue/trogue.hpp"

#include "scene_make.hpp"  // hp::to_hex / hp::make_ascii_scene（不依赖 raylib）

namespace hp {

// ── ① 命令行 ──
struct Args {
    bool headless = false;  // 隐藏窗口（仍建 GL 上下文，Raylib 必须有）
    double seconds = 0.0;   // >0：跑满该模拟时长后自动退出（无显示环境的验证口径）
    std::string shot;       // 非空：窗口就绪后写出一张整帧 PNG 再退出
    bool help = false;
    bool valid = true;      // 参数非法（未知参数/取值坏）→ false，调用方应直接退出
};

inline Args parse_args(int argc, char** argv, const char* usage) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        // 取值失败统一报错：不把坏值静默当 0（否则 --seconds abc 会变成「永不退出」）
        auto take = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "[args] %s 缺少取值\n", what);
                a.valid = false;
                return "";
            }
            return argv[++i];
        };
        if (k == "--headless") {
            a.headless = true;
        } else if (k == "--seconds") {
            const char* v = take("--seconds");
            char* end = nullptr;
            a.seconds = std::strtod(v, &end);
            if (!end || *end != '\0' || a.seconds < 0.0) {
                std::fprintf(stderr, "[args] --seconds 需为非负数值，收到: %s\n", v);
                a.valid = false;
            }
        } else if (k == "--shot") {
            a.shot = take("--shot");
        } else if (k == "-h" || k == "--help") {
            a.help = true;
        } else {
            std::fprintf(stderr, "[args] 未知参数: %s\n", k.c_str());
            a.valid = false;
        }
    }
    if (a.help) std::printf("%s\n", usage);
    if (!a.valid) std::fprintf(stderr, "用法: %s\n", usage);
    return a;
}

// ── ② 窗口 ──
inline void open_window(const char* title, int width, int height, bool headless) {
    if (headless) SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(width, height, title);
}

// 离屏整帧截图：draw 负责画完一帧（tile 层 + game 对象）；返回是否导出成功。
inline bool capture_frame_png(const std::string& path, int width, int height,
                              const std::function<void()>& draw) {
    RenderTexture2D rt = LoadRenderTexture(width, height);
    if (rt.id == 0) {
        std::fprintf(stderr, "[harness] RenderTexture 创建失败\n");
        return false;
    }
    BeginTextureMode(rt);
    ClearBackground(BLACK);
    draw();
    EndTextureMode();
    rlDrawRenderBatchActive();  // 不 flush 会取到未绘制的残缺帧
    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);    // RenderTexture 原点在左下，导出前校正
    const bool ok = ExportImage(img, path.c_str());
    UnloadImage(img);
    UnloadRenderTexture(rt);
    if (!ok) std::fprintf(stderr, "[harness] 导出 PNG 失败: %s\n", path.c_str());
    return ok;
}

}  // namespace hp
