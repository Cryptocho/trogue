// anim_util.hpp —— game 层动画实体绘制与截图共享工具（demo 与 anim_viewer 复用）。
//
// 归属说明：这些是「触发/应用」侧的 game 层组合逻辑，**不下沉 engine**——
// engine 刻意不绑动画播放器到绘制（AGENTS.md「引擎公共 API 边界」：播放器
// 只输出当前帧视觉描述）；offset 组合与回退链是 game 的应用决策。两处消费者
// （main.cpp / anim_viewer.cpp）曾各持一份拷贝，抽此处单点维护。
#pragma once

#include <string>
#include <functional>  // std::function（capture_offscreen_png 的 draw 回调）

#include "trogue/animation.hpp"  // AnimationPlayer（指针透传，可为 null）
#include "trogue/scene.hpp"      // SceneAsset / SpriteDesc
#include "trogue/types.hpp"      // Vec2 / Color

namespace game {

// 绘制一个动画实体：优先采样播放器当前帧（offset 组合 = 实体静态锚点 + 帧
// 自身偏移，tint 用实体颜色）；采样失败**或绘制失败**（anim 为 null、帧无、
// 贴图缺失/参数非法均同）回退静态 sprite；两者皆不可用再画 fallback_w×
// fallback_h 色块兜底（浮点 DrawRectanglePro，不做 int 截断——引擎 draw_rect
// 的 int 截断在移动插值中会 ±1px 错位/抖动）。
// 返回 true = 画出了 sprite；false = 走了色块兜底（调用方一般无需再处理）。
// 调用方决定传不传播放器（触发/绑定策略归调用方，本函数只做机制）。
// rotation（度，绕 pos 顺时针）透传给引擎绘制，帧动画帧与静态 sprite 一并生效
//（朝向是实体级属性）；static_sprite 自带的 flip/offset 随值语义直接生效，
// 帧动画帧的 flip 未定义（tro-animations 无 flip 字段）。
bool draw_entity_sprite(const tg::SceneAsset& asset,
                        const tg::AnimationPlayer* anim,
                        const tg::SpriteDesc& static_sprite, tg::Vec2 pos,
                        tg::Color tint, float fallback_w, float fallback_h,
                        float rotation = 0.0f);

// 帧末截图（调用方排队、本函数执行）：先强制 flush 渲染批再读屏导出。
// 返回是否导出成功（失败已记 warning 日志）。
// 注：读屏路径依赖**屏幕缓冲**（窗口模式帧后读会得黑帧）——需同步/无头
// 截图时用 capture_offscreen_png（离屏 FBO，不依赖屏幕）。
bool export_screenshot(const std::string& path);

// 离屏截图：把 draw 回调（应执行完整一帧绘制，含相机/实体/HUD）渲染到
// w×h 的 RenderTexture 并导出为 PNG。**不依赖屏幕缓冲**，窗口/隐藏窗口皆可
// （需 GL 上下文就绪）。导正垂直翻转（离屏取像不翻转，与屏幕路径相反）。
// 同步：返回时文件已落盘。
// 返回 {ok, w, h, bytes}；ok=false 时其余字段仍为请求尺寸/0。
struct ShotResult {
    bool ok = false;
    int w = 0, h = 0;
    long long bytes = 0;
};
ShotResult capture_offscreen_png(int w, int h, const std::string& path,
                                 const std::function<void()>& draw);

}  // namespace game
