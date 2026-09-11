#pragma once
// render.hpp —— 显式渲染原语与贴图资源（plan-5.3 §2/§3）。
//
// 原则：engine 只绘制 tile 层与调用方显式传入的 sprite/色块；无隐式实体遍历、
// 无自动 y-sort、无 descriptor 自动绘制。全部绘制调用在调用方的
// BeginMode2D(camera)...EndMode2D() 区间内执行；engine 不调用这两个函数、
// 不接收 camera（调用方已设置好变换）。资源生命周期：绘制期间 asset 必须
// 存活；asset swap/销毁只在窗口绘制帧外（plan-5.3 §6）。

#include <cstdint>  // std::uint64_t

#include "trogue/scene.hpp"  // SceneAsset / SpriteDesc
#include "trogue/types.hpp"  // RenderResult / Color / Vec2 / Rect

namespace tg {

// 绘制该 asset 的全部 tile 层（层序 = 资产数组序）；不含任何实体。
// 前置：调用方处于 BeginMode2D()...EndMode2D() 区间；asset 存活。
RenderResult render_scene(const SceneAsset& asset);

// 绘制一个显式 sprite 快照。pos = 期望的左上角世界坐标（game 决定来源）。
// 归属校验：sprite.asset_id == asset.asset_id()，否则 Invalid + 错误日志。
// region 缺省(w/h==0)时按贴图尺寸补齐；锚点 = pos + sprite.offset。
RenderResult render_sprite(const SceneAsset& asset, const SpriteDesc& sprite,
                           Vec2 pos, Color tint = Color{255, 255, 255, 255});

// 便捷色块（palette/bare/无贴图时 game 可用）；不做任何实体语义。
RenderResult draw_rect(Rect world_rect, Color color);

// 进程级共享贴图缓存释放（应在窗口销毁前、所有绘制结束后调用）。
void shutdown_render();

}  // namespace tg