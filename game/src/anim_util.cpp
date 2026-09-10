// anim_util.cpp —— 见 anim_util.hpp 头注释（game 层共享工具，非引擎 API）。
#include "anim_util.hpp"

#include <raylib.h>
#include <rlgl.h>  // rlDrawRenderBatchActive（截图前强制 flush 渲染批）

#include "trogue/render.hpp"  // render_sprite / RenderResult

namespace game {

bool draw_entity_sprite(const tg::SceneAsset& asset,
                        const tg::AnimationPlayer* anim,
                        const tg::SpriteDesc& static_sprite, tg::Vec2 pos,
                        tg::Color tint, float fallback_w, float fallback_h) {
    // ① 帧动画采样（plan-10 §3.2 组合公式）：offset = 实体静态锚点 + 帧自身
    // 偏移（士兵 [-50,-50]+[0,0] 居中语义与静态帧一致）。采样成功但绘制失败
    // （贴图缺失 TextureMissing/region 非法 Invalid——asset load 不读纹理，
    // 首次绘制才触发加载）同样回退：旧实现两处一致，实体不可见比色块更糟。
    if (anim != nullptr) {
        tg::SpriteDesc f = anim->current_frame();
        if (f.has) {
            f.offset = tg::Vec2{static_sprite.offset.x + f.offset.x,
                                static_sprite.offset.y + f.offset.y};
            if (tg::render_sprite(asset, f, pos, tint) ==
                tg::RenderResult::Drawn)
                return true;
        }
    }
    // ② 静态 sprite 回退（画面永不空白）。
    if (static_sprite.has && tg::render_sprite(asset, static_sprite, pos,
                                               tint) == tg::RenderResult::Drawn)
        return true;
    // ③ 色块兜底：浮点原语绘制，与 tile 层同相机变换下严格对齐。
    DrawRectanglePro(::Rectangle{pos.x, pos.y, fallback_w, fallback_h},
                     ::Vector2{0.0f, 0.0f}, 0.0f,
                     ::Color{tint.r, tint.g, tint.b, tint.a});
    return false;
}

bool export_screenshot(const std::string& path) {
    // 先强制 flush 渲染批（raylib 的批顶点在 EndDrawing 才真正提交 GL；
    // 不 flush 就 glReadPixels 会拍到未绘制的残缺帧——曾致 soldier 场景
    // 截图全黑、forest 截图丢 HUD 的误诊）。
    rlDrawRenderBatchActive();
    Image img = LoadImageFromScreen();
    const bool ok = ExportImage(img, path.c_str());
    UnloadImage(img);
    if (!ok) TraceLog(LOG_WARNING, "[game] 截图导出失败: %s", path.c_str());
    return ok;
}

}  // namespace game
