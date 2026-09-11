// anim_util.cpp —— 见 anim_util.hpp 头注释（game 层共享工具，非引擎 API）。
#include "anim_util.hpp"

#include <cstdio>  // std::fopen/fseek/ftell（文件字节数）

#include <raylib.h>
#include <rlgl.h>  // rlDrawRenderBatchActive（截图前强制 flush 渲染批）

#include "trogue/render.hpp"  // render_sprite / RenderResult

namespace game {

bool draw_entity_sprite(const tg::SceneAsset& asset,
                        const tg::AnimationPlayer* anim,
                        const tg::SpriteDesc& static_sprite, tg::Vec2 pos,
                        tg::Color tint, float fallback_w, float fallback_h) {
    // ① 帧动画采样（组合公式）：offset = 实体静态锚点 + 帧自身
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

ShotResult capture_offscreen_png(int w, int h, const std::string& path,
                                 const std::function<void()>& draw) {
    ShotResult r;
    r.w = w;
    r.h = h;
    if (w <= 0 || h <= 0) return r;
    RenderTexture rt = LoadRenderTexture(w, h);
    if (rt.id == 0) {
        TraceLog(LOG_WARNING, "[game] 离屏截图 FBO 创建失败 (%dx%d)", w, h);
        return r;
    }
    BeginTextureMode(rt);
    ClearBackground(BLACK);
    draw();  // 调用方绘制完整一帧
    EndTextureMode();
    rlDrawRenderBatchActive();
    Image img = LoadImageFromTexture(rt.texture);
    UnloadRenderTexture(rt);
    if (!img.data) {
        TraceLog(LOG_WARNING, "[game] 离屏截图取像失败: %s", path.c_str());
        return r;
    }
    // 离屏（glGetTexImage）不翻转，屏幕路径翻转——导正使两路朝向一致。
    ImageFlipVertical(&img);
    r.ok = ExportImage(img, path.c_str());
    UnloadImage(img);
    if (!r.ok) {
        TraceLog(LOG_WARNING, "[game] 截图导出失败: %s", path.c_str());
        return r;
    }
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        r.bytes = static_cast<long long>(std::ftell(f));
        std::fclose(f);
    }
    return r;
}

}  // namespace game
