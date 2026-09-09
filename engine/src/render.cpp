// render.cpp —— 显式渲染原语实现（plan-5.3 §2/§3/§4/§5）。
//
// 三段固定顺序（§4）：
//   ① 纯 CPU 参数/归属校验：失败 → Invalid + 每次记录日志，不触碰窗口/缓存；
//   ② 一次 IsWindowReady()：false → WindowUnavailable，整体 no-op 不记日志；
//   ③ 纹理缓存查找/加载 + 绘制：缺失/失败 → 该次 TextureMissing 跳过，
//      每路径一次错误日志（失败哨兵，成功加载后清除并复用）。
//
// 贴图生命周期（§3）：tileset 图集贴图随 asset RAII（SceneImpl::atlas_textures
// 槽位，本文件实现 SceneImpl::~SceneImpl 释放）；独立贴图（sprite/动画帧）
// 进程级共享懒缓存（path → shared_ptr），shutdown_render() 统一释放。
#include "trogue/render.hpp"

#include <cmath>     // std::floor
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "raylib.h"  // TraceLog / Texture2D / LoadTexture 等（engine 内部）

#include "scene_impl.hpp"       // detail::SceneImpl（含图集槽位）
#include "util/path_check.hpp"  // is_safe_relative_path

namespace tg {

// 三段计数 seam 数据（进程内单调累计，不随调用清零；类型与 seam 头一致）
namespace detail {
struct RenderStats {
    int param_failures = 0;
    int window_checks = 0;
    int texture_attempts = 0;
};
}  // namespace detail

namespace {

// 场景内相对路径 → 实际文件路径（assets/ 前缀，与 scene_asset.cpp 约定一致）
std::string assets_path(std::string_view rel) {
    return std::string(kAssetsDir) + "/" + std::string(rel);
}

// ── 独立贴图共享懒缓存（path → shared_ptr<Texture2D> 包装） ──
struct SharedTexture {
    Texture2D tex;
    ~SharedTexture() { if (tex.id != 0) UnloadTexture(tex); }
};

std::unordered_map<std::string, std::shared_ptr<const SharedTexture>> g_texture_cache;
// 失败哨兵：path → true，缓存生命周期内每路径只记一次错误日志。
std::unordered_set<std::string> g_texture_failed;

// 加载（或取缓存）独立贴图。返回空 shared_ptr 表示失败（哨兵已置）。
// 返回的 shared_ptr 为空但不代表路径已记录失败 —— 调用方统一走 log 逻辑。
std::shared_ptr<const SharedTexture> get_or_load_texture(const std::string& path,
                                                         bool& failed_once) {
    failed_once = false;
    if (auto it = g_texture_cache.find(path); it != g_texture_cache.end())
        return it->second;
    if (g_texture_failed.count(path)) { failed_once = true; return {}; }
    const std::string full = assets_path(path);
    Texture2D tex = LoadTexture(full.c_str());
    if (tex.id == 0) {
        g_texture_failed.insert(path);      // 哨兵：后续不再尝试
        failed_once = true;
        return {};
    }
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);  // 像素风（沿用 C 版）
    auto sp = std::make_shared<const SharedTexture>(SharedTexture{tex});
    g_texture_cache.emplace(path, sp);
    return sp;
}

// raylib Color ↔ tg::Color
::Color to_raylib(tg::Color c) {
    return ::Color{c.r, c.g, c.b, c.a};
}

// 图集 tile 视觉描述（tileset 元数据 + tile id）。
// 查 load 期建的 id → TileVisual 表（region 含 size_in_atlas 多格扩展、texture_origin
// 为 Godot 纹理原点，见 scene_impl.hpp / plan-8 §3.3）；越界/表缺失 → 空描述
// （region w/h==0 = 无绘制，防御畸形资产，契约与原 tile_rects 一致）。
detail::SceneImpl::TilesetMeta::TileVisual atlas_tile_visual(
    const detail::SceneImpl::TilesetMeta& ts, int tile_id) {
    if (tile_id < 0 ||
        static_cast<std::size_t>(tile_id) >= ts.tile_visuals.size())
        return {};
    return ts.tile_visuals[static_cast<std::size_t>(tile_id)];
}

detail::RenderStats g_render_stats;

// 图集贴图装载（asset 槽位，懒）：成功返回非空 void*；失败置哨兵。
void* load_atlas_texture(detail::SceneImpl& impl, std::size_t ts_index,
                         bool& failed_once) {
    auto& slot = impl.atlas_textures[ts_index];
    if (slot.texture) return slot.texture;              // 已装
    if (slot.attempted) { failed_once = true; return nullptr; }  // 失败哨兵
    slot.attempted = true;
    const std::string full = assets_path(impl.tilesets[ts_index].texture);
    Texture2D* t = new Texture2D(LoadTexture(full.c_str()));
    if (t->id == 0) {
        delete t;
        failed_once = true;
        return nullptr;
    }
    SetTextureFilter(*t, TEXTURE_FILTER_POINT);
    slot.texture = t;
    return t;
}

}  // namespace

// SceneImpl 析构：释放图集贴图（asset 级 RAII，plan-5.3 §3）。
// 槽位持有 new 出的 Texture2D*；释放顺序 = UnloadTexture（GPU）→ delete（堆）。
detail::SceneImpl::~SceneImpl() {
    for (auto& slot : atlas_textures) {
        if (slot.texture) {
            Texture2D* t = static_cast<Texture2D*>(slot.texture);
            UnloadTexture(*t);
            delete t;
            slot.texture = nullptr;
        }
    }
}

RenderResult render_scene(const SceneAsset& asset) {
    // ① 参数校验：asset 引用即保证存活；无显式参数可失效。
    auto& impl = *asset.impl_;

    // ② 窗口就绪
    ++g_render_stats.window_checks;
    if (!IsWindowReady()) return RenderResult::WindowUnavailable;

    // ③ 逐层绘制
    for (std::size_t li = 0; li < impl.layers.size(); ++li) {
        const LayerInfo& info = impl.layers[li];
        const auto& tiles = impl.layer_tiles[li];
        if (info.tileset_index >= 0) {
            // 图集模式
            const std::size_t ts = static_cast<std::size_t>(info.tileset_index);
            bool failed_once = false;
            void* tp = load_atlas_texture(impl, ts, failed_once);
            if (!tp) {
                if (failed_once)
                    TraceLog(LOG_ERROR, "[render] 图集贴图缺失: %s",
                             impl.tilesets[ts].texture.c_str());
                continue;  // 该层跳过（TextureMissing 由首失败记忆）
            }
            Texture2D& tex = *static_cast<Texture2D*>(tp);
            const auto& tsm = impl.tilesets[ts];
            for (int ty = 0; ty < info.height; ++ty) {
                for (int tx = 0; tx < info.width; ++tx) {
                    const int v =
                        tiles[static_cast<std::size_t>(ty) * info.width + tx];
                    if (v < 0) continue;
                    const auto tv = atlas_tile_visual(tsm, v);
                    const Rect r = tv.region;
                    // Godot tile 绘制语义（plan-8 §2.2）：
                    //   dest 左上 = cell 中心 − region.size/2 − texture_origin。
                    // 1×1 且 origin=0 时精确退化为「格子左上角」（旧公式），既有
                    // 资产零回归（整数量 + 二进制精确半值，float 逐位还原）。
                    // y_sort_origin 透传不消费（引擎无逐 tile y-sort）。
                    const float wx = static_cast<float>(info.origin_x + tx * impl.tile_w)
                                   + impl.tile_w * 0.5f - r.w * 0.5f
                                   - tv.texture_origin.x;
                    const float wy = static_cast<float>(info.origin_y + ty * impl.tile_h)
                                   + impl.tile_h * 0.5f - r.h * 0.5f
                                   - tv.texture_origin.y;
                    DrawTextureRec(tex, ::Rectangle{r.x, r.y, r.w, r.h},
                                   ::Vector2{wx, wy}, ::Color{255, 255, 255, 255});
                }
            }
        } else if (info.tileset_index == -1 && impl.mode != detail::SceneImpl::Mode::bare) {
            // palette 模式：色块
            for (int ty = 0; ty < info.height; ++ty) {
                for (int tx = 0; tx < info.width; ++tx) {
                    const int v =
                        tiles[static_cast<std::size_t>(ty) * info.width + tx];
                    if (v < 0) continue;
                    if (v >= static_cast<int>(impl.palette.size())) continue;  // 防御
                    const tg::Color c = impl.palette[static_cast<std::size_t>(v)];
                    const float wx = static_cast<float>(info.origin_x + tx * impl.tile_w);
                    const float wy = static_cast<float>(info.origin_y + ty * impl.tile_h);
                    DrawRectangle(static_cast<int>(wx), static_cast<int>(wy),
                                  impl.tile_w, impl.tile_h, to_raylib(c));
                }
            }
        }
        // bare：零层，no-op（前面直接返回或空循环）
    }
    return RenderResult::Drawn;
}

RenderResult render_sprite(const SceneAsset& asset, const SpriteDesc& sprite,
                           Vec2 pos, Color tint) {
    auto& impl = *asset.impl_;
    // ① 参数/归属校验（失败才计入 param_failures）
    const auto param_fail = [&](const char* why) {
        ++g_render_stats.param_failures;
        TraceLog(LOG_ERROR, "[render] %s", why);
        return RenderResult::Invalid;
    };
    if (!sprite.has) return param_fail("sprite 为空快照 (has=false)");
    if (sprite.asset_id != 0 && sprite.asset_id != impl.id) {
        return param_fail("sprite 归属不匹配（asset_id 与 asset 不一致）");
    }
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y))
        return param_fail("sprite 位置非有限");
    if (sprite.tileset_index >= 0) {
        const std::size_t ts = static_cast<std::size_t>(sprite.tileset_index);
        if (ts >= impl.tilesets.size()) return param_fail("tileset_index 越界");
        const auto& tsm = impl.tilesets[ts];
        if (sprite.tile < 0 || sprite.tile >= tsm.tile_count)
            return param_fail("tile id 越界");
    } else if (!sprite.texture.empty() &&
               !detail::is_safe_relative_path(sprite.texture)) {
        return param_fail("texture 路径非法");
    }
    if (sprite.region.w < 0 || sprite.region.h < 0 || sprite.region.x < 0 ||
        sprite.region.y < 0)
        return param_fail("region 数值非法（x/y>=0）");

    // ② 窗口就绪
    ++g_render_stats.window_checks;
    if (!IsWindowReady()) return RenderResult::WindowUnavailable;

    // ③ 绘制
    if (sprite.tileset_index >= 0) {
        // 图集形态
        const std::size_t ts = static_cast<std::size_t>(sprite.tileset_index);
        const auto& tsm = impl.tilesets[ts];
        bool failed_once = false;
        void* tp = load_atlas_texture(impl, ts, failed_once);
        if (!tp) {
            if (failed_once)
                TraceLog(LOG_ERROR, "[render] 图集贴图缺失: %s",
                         tsm.texture.c_str());
            return RenderResult::TextureMissing;
        }
        Texture2D& tex = *static_cast<Texture2D*>(tp);
        const auto tv = atlas_tile_visual(tsm, sprite.tile);
        const Rect r = tv.region;
        // 图集形态锚点语义不变：pos + offset = 纹理左上（region 自动含多格尺寸）。
        // Godot 的 cell 中心对齐/texture_origin 是 tile 层语义；实体 sprite 的
        // Godot 等效摆放由 game 经 SpriteDesc.offset 自行表达（plan-8 §3.3）。
        DrawTextureRec(tex, ::Rectangle{r.x, r.y, r.w, r.h},
                       ::Vector2{pos.x + sprite.offset.x, pos.y + sprite.offset.y},
                       to_raylib(tint));
        return RenderResult::Drawn;
    }
    // 独立贴图形态
    bool failed_once = false;
    ++g_render_stats.texture_attempts;
    auto tex_sp = get_or_load_texture(sprite.texture, failed_once);
    if (!tex_sp) {
        if (failed_once)
            TraceLog(LOG_ERROR, "[render] 独立贴图缺失: %s", sprite.texture.c_str());
        return RenderResult::TextureMissing;
    }
    // region 缺省（w/h==0）→ 整图
    float rw = sprite.region.w, rh = sprite.region.h;
    if (rw == 0.0f || rh == 0.0f) {
        rw = static_cast<float>(tex_sp->tex.width);
        rh = static_cast<float>(tex_sp->tex.height);
    }
    const ::Rectangle rec{sprite.region.x, sprite.region.y, rw, rh};
    DrawTextureRec(tex_sp->tex, rec,
                   ::Vector2{pos.x + sprite.offset.x, pos.y + sprite.offset.y},
                   to_raylib(tint));
    return RenderResult::Drawn;
}

RenderResult draw_rect(Rect world_rect, Color color) {
    // ① 参数校验
    if (!std::isfinite(world_rect.x) || !std::isfinite(world_rect.y) ||
        !std::isfinite(world_rect.w) || !std::isfinite(world_rect.h) ||
        world_rect.w <= 0 || world_rect.h <= 0) {
        ++g_render_stats.param_failures;
        TraceLog(LOG_ERROR, "[render] draw_rect 矩形非法");
        return RenderResult::Invalid;
    }
    // ② 窗口就绪
    ++g_render_stats.window_checks;
    if (!IsWindowReady()) return RenderResult::WindowUnavailable;
    // ③ 绘制（color 即色块填充色；门禁 M4 修复：此前参数被忽略恒画白）
    DrawRectangle(static_cast<int>(world_rect.x), static_cast<int>(world_rect.y),
                  static_cast<int>(world_rect.w), static_cast<int>(world_rect.h),
                  to_raylib(color));
    return RenderResult::Drawn;
}

void shutdown_render() {
    g_texture_cache.clear();  // shared_ptr 析构 → UnloadTexture
    g_texture_failed.clear();
}

// ════════════════════ 测试 seam（仅 TROGUE_TEST_SEAMS） ════════════════════

#ifdef TROGUE_TEST_SEAMS

namespace detail {

RenderStats render_test_stats() {
    return g_render_stats;
}
void render_test_reset_stats() { g_render_stats = RenderStats{}; }

}  // namespace detail

#endif  // TROGUE_TEST_SEAMS

}  // namespace tg