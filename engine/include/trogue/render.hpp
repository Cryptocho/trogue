#pragma once
// render.hpp —— 显式渲染原语与贴图资源。
//
// 原则：engine 只绘制 tile 层与调用方显式传入的 sprite/色块；无隐式实体遍历、
// 无自动 y-sort、无 descriptor 自动绘制。全部绘制调用在调用方的
// BeginMode2D(camera)...EndMode2D() 区间内执行；engine 不调用这两个函数、
// 不接收 camera（调用方已设置好变换）。资源生命周期：绘制期间 asset 必须
// 存活；asset swap/销毁只在窗口绘制帧外。

#include <cstdint>  // std::uint64_t
#include <string_view>

#include "trogue/scene.hpp"  // SceneAsset / SpriteDesc
#include "trogue/types.hpp"  // RenderResult / Color / Vec2 / Rect

namespace tg {

// 绘制该 asset 的全部 tile 层（层序 = 资产数组序）；不含任何实体。
// 前置：调用方处于 BeginMode2D()...EndMode2D() 区间；asset 存活。
RenderResult render_scene(const SceneAsset& asset);

// 绘制一个显式 sprite 快照。pos = 期望的左上角世界坐标（game 决定来源）。
// 归属校验：sprite.asset_id == asset.asset_id()，否则 Invalid + 错误日志。
// region 缺省(w/h==0)时按贴图尺寸补齐；锚点 = pos + sprite.offset。
// 注：**pos 始终为纹理左上角**（不因 region 缺省而改为居中）；需居中请由 game
// 自行设 offset（引擎不做隐式对齐）。
RenderResult render_sprite(const SceneAsset& asset, const SpriteDesc& sprite,
                           Vec2 pos, Color tint = Color{255, 255, 255, 255});

// 便捷色块（palette/bare/无贴图时 game 可用）；不做任何实体语义。
RenderResult draw_rect(Rect world_rect, Color color);

// 离屏渲染该 asset 的全部 tile 层到指定尺寸并导出为 PNG。
// 语义：临时创建 RenderTexture(w,h) → 在其上绘制（恒等相机，无窗口）→
// 取像（校正垂直翻转）→ ExportImage → 释放。不依赖窗口是否可见，仅要求
// GL 上下文已就绪（IsWindowReady；隐藏窗口满足）。
// 只画 tile 层（与 render_scene 一致，**不含**实体/sprite/HUD）；恒等相机。
// 参数非法（w/h<=0 或路径不安全）→ Invalid；窗口未就绪 → WindowUnavailable；
// FBO/取像/导出失败 → TextureMissing。
RenderResult render_scene_to_png(const SceneAsset& asset, int w, int h,
                                 std::string_view path);

// ── 渲染可观测性（Agent/调试用） ──
//
// 三段计数：每次绘制原语按固定顺序（①参数/归属校验 ②窗口就绪 ③贴图/绘制）
// 累计。供消费方在无截图/无法目视时断言「渲染确实发生」及失败的类别
//（如 Drawn 但 texture_attempts 增长 = 贴图缺失分支）。进程内单调累计，
// 不随调用清零。
struct RenderStats {
    int param_failures = 0;    // 段① 参数/归属校验失败次数
    int window_checks = 0;     // 段② 窗口就绪检查次数
    int texture_attempts = 0;  // 段③ 独立贴图加载尝试次数
};

RenderStats render_stats();
void render_reset_stats();  // 归零（测试/长跑分段观测）

// 进程级共享贴图缓存释放（应在窗口销毁前、所有绘制结束后调用）。
// 生命周期契约：独立贴图（sprite/动画帧）按路径去重进进程级缓存，**无容量
// 上限**、只增不减，生命周期 = 进程（由本函数统一释放）；图集贴图随 asset RAII。
void shutdown_render();

}  // namespace tg