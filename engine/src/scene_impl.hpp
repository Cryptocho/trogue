#pragma once
// scene_impl.hpp —— 资产私有数据载体（detail；仅 engine 内部与 trogue_engine_test 可达）。
//
// 公共头只持 std::unique_ptr<detail::SceneImpl> 不透明指针，不暴露本头。
// SceneImpl 同时被 scene_asset.cpp（解析/查询）与 render.cpp（绘制）消费：
// 两大模块经 detail 直接读字段，避免为渲染另造公共查询接口。
//
// 贴图资源生命周期（plan-5.3 §3）：
//   - tileset 图集贴图：随 asset RAII（本结构的 tileset_texture 槽位，析构释放）；
//   - 独立贴图（sprite/动画帧）：进程级共享懒缓存，属 render 模块（shutdown_render）。
// 贴图加载只发生在绘制调用（懒加载），asset load 期不读纹理文件（5.2 §2.3）。

#include <cstdint>  // std::uint64_t
#include <string>
#include <vector>

#include "trogue/animation.hpp"  // AnimationSet（动画集视图）
#include "trogue/scene.hpp"      // LayerInfo/SceneEntity/SpriteDesc（公共值类型）
#include "trogue/types.hpp"      // Color

namespace tg::detail {

// ── 动画集数据（5.4 播放器消费；load 期校验后持有） ──
struct AnimClipFrame {
    int texture = -1;
    Rect region{0, 0, 0, 0};  // w/h==0 = 整图（render 期补齐）
    Vec2 offset{0, 0};
};
struct AnimClip {
    std::string name;
    double fps = 1.0;
    bool loop = false;
    std::vector<AnimClipFrame> frames;
};
struct AnimData {
    std::uint64_t asset_id = 0;          // 归属（= 所属 asset 的 id）
    std::vector<std::string> textures;   // assets-relative 路径
    std::vector<AnimClip> clips;
};

// SceneImpl：资产私有数据载体（detail 公有 struct；解析器/查询/渲染自由函数
// 直接访问字段，公共头只持不透明指针）。
struct SceneImpl {
    std::uint64_t id = 0;
    std::string name;                    // meta.name，缺省空
    Color background{16, 16, 24, 255};   // meta.background，缺省 #101018（对齐 demo）
    int tile_w = 0, tile_h = 0;          // bare → 0

    // 模式：atlas / palette / bare（由解析时判定）
    enum class Mode { atlas, palette, bare };
    Mode mode = Mode::bare;

    // 图集元数据（atlas 模式）：name → (index, tile 尺寸, count, texture 路径)
    struct TilesetMeta {
        // 单个 tile 的视觉描述（plan-8 §3.3）。
        // region = (col*tw, row*th, sw*tw, sh*th)——sw/sh 为 Godot size_in_atlas
        //（多格 tile，缺省 1×1）；texture_origin 为 Godot 纹理原点（绘制偏移 = −origin）；
        // y_sort_origin 为 Godot y-sort 排序键偏移（透传存储，引擎暂不消费——
        // 无逐 tile y-sort，未来按需消费/暴露）。
        struct TileVisual {
            Rect region{0, 0, 0, 0};   // 贴图子矩形（w/h==0 = 无绘制，防御畸形资产）
            Vec2 texture_origin{0, 0};
            int y_sort_origin = 0;
        };
        std::string name;
        std::string path;                 // 原始引用路径（assets-relative）
        int tile_w = 0, tile_h = 0;
        int tile_count = 0;               // tiles 数组长度 = id 值域上限
        std::string texture;              // tro-tileset 的 texture（assets-relative）
        int columns = 0;                  // tro-tileset columns（图集每行 tile 数）
        // id → TileVisual（像素），与 tile_count 对齐；load 期由 tiles[] 建表。
        // 契约：tiles[] 数组顺序即 id，每个 tile 自带 col/row（非顺序排列可能，
        // 不能按 id 推公式）。region 越界不在 load 期校验（load 不读纹理文件）。
        std::vector<TileVisual> tile_visuals;
    };
    std::vector<TilesetMeta> tilesets;
    std::vector<Color> palette;

    std::vector<LayerInfo> layers;
    std::vector<std::vector<int>> layer_tiles;  // 与 layers 对齐（行主序）

    std::vector<SceneEntity> entities;
    std::vector<AnimData> anims;  // 含 animations 的 entity 逐个解析

    // 动画集视图（与 anims 对齐；公共 API 经 SceneAsset::animation_set 访问）
    std::vector<AnimationSet> anim_sets;

    // 图集贴图懒加载槽（与 tilesets 对齐；5.3 §3：asset 级 RAII）。
    // 句柄类型用 void* 承载 raylib Texture2D 的 opaque 表示：render.cpp 负责
    // 加载/释放（UnloadTexture），本结构只保证「随 asset 析构」的生命周期。
    struct AtlasTextureSlot {
        void* texture = nullptr;  // raylib Texture2D*（按值存栈内存，见 render.cpp）
        bool attempted = false;   // 是否尝试过加载（每路径一次失败哨兵）
    };
    std::vector<AtlasTextureSlot> atlas_textures;

    ~SceneImpl();  // 释放图集贴图（render.cpp 提供实现，避免 raylib 头泄漏到本头）
};

}  // namespace tg::detail