#pragma once
// scene.hpp —— 场景资产（SceneAsset）与 tile-only 查询（plan-5.2 §1/§4）。
//
// 值类型（SceneEntity/LayerInfo/SpriteDesc）= 可复制快照，纯数据、public 字段；
// SceneAsset = RAII 资源类（不可拷贝、可移动），只读、持有 asset 生命周期内的
// 一切数据（tile 层、图集元数据、palette、descriptor、动画帧表、payload）。
//
// 契约：
// - 返回内部 const 引用的访问器（name()/layer()）引用仅在 asset 存活期内有效；
//   跨 reload 请取值快照（entity() 即值快照）。
// - 引擎不提供运行时实体容器/按 id 改位/spawn/despawn；SceneEntity 只是可复制
//   的初始描述（schema 通用 spawn descriptor）。
// - tile 查询只查 solid==true 的 tile 层；descriptor 的 solid 永不参与。

#include <cstdint>  // std::uint64_t
#include <memory>   // std::unique_ptr
#include <string>
#include <string_view>
#include <vector>

#include "trogue/config.hpp"
#include "trogue/types.hpp"
#include <tl/expected.hpp>

namespace tg {

// detail 前向声明（定义于 src/scene_asset.cpp；公共 API 只持不透明指针）：
//   SceneImpl —— 资产私有数据载体（detail 公有 struct，字段公开给解析器/查询）
namespace detail {
class SceneLoader;
struct SceneImpl;
}
class AnimationSet;  // 定义于 animation.hpp；scene.hpp 只返回引用（前向即可）

// ════════════════════ 枚举 ════════════════════

// tile 层查询结果（plan-5.2 §4）。error = 参数非有限/矩形非正/层越界。
enum class TileQueryResult { error, clear, solid };

// tile_at 查询结果。empty = 合法层外或空格（*out=-1）；occupied = 占用。
enum class TileLookupResult { error, empty, occupied };

// ════════════════════ 值快照 ════════════════════

// 实体/动画帧的视觉描述（plan-5.2 §1.2）。
// region.w/h==0 表示整图（render 期按贴图尺寸补齐）。
struct SpriteDesc {
    bool has = false;
    std::uint64_t asset_id = 0;  // 归属校验（取快照时填所属 asset_id）；0=无
    int tileset_index = -1;      // >=0 图集形态；-1 = 独立贴图形态
    int tile = -1;               // 图集 tile id（图集形态）
    std::string texture;         // 独立贴图路径（assets-relative，独立贴图形态）
    Rect region{0, 0, 0, 0};
    Vec2 offset{0, 0};
};

// 通用 spawn descriptor 快照（非运行时实体；plan-5.2 §1.2）。
// animations 不复制进此快照：经 asset 的动画集查询取得（见 plan-5.4 §3.1）。
struct SceneEntity {
    std::string id;
    std::string type;            // 不透明 archetype/spawn 标识，engine 不分支
    float x = 0.0f, y = 0.0f;    // 左上角
    float w = 0.0f, h = 0.0f;    // 缺省见 schema（非 bare=tile 尺寸；bare=16）
    int z = 0;
    Color color{255, 255, 255, 255};
    bool solid = false;          // 仅 game 导入提示；engine 查询不读它
    SpriteDesc sprite;
};

// 每层只读元数据快照（plan-5.2 §1.2）。
struct LayerInfo {
    std::string name;            // 缺省 "layer"
    int width = 0, height = 0;   // 像素 tile 网格尺寸
    int origin_x = 0, origin_y = 0;  // 层左上角世界偏移（可负）
    bool solid = false;
    int tileset_index = -1;      // >=0 图集；-1 = palette/bare
    std::string tileset_name;    // 图集 = 该层 tileset 的 name；palette/bare = 空
    int nonempty = 0;            // 非空（≠-1）tile 数
};

// ════════════════════ SceneAsset ════════════════════

class SceneAsset {
public:
    using AssetError = Error;  // 具名别名（plan-5.2 §1.1）

    // 单一加载路径：失败返回带诊断的错误，不产生半成品、不修改任何既有 asset。
    static expected<SceneAsset, AssetError> load(std::string_view path);

    // 内存加载（plan-12 §4.4）：与 load(path) 同一解析/校验路径，仅文本来源不同
    //（程序生成场景：game 拼tro-scene JSON → 本入口 → render_scene）。
    // name 仅用于错误诊断（kParseError/kSchemaViolation 消息前缀）；tileset/texture
    // 路径仍按 assets/ 约定从磁盘解析。不参与 watcher（内存场景无文件可监听）。
    static expected<SceneAsset, AssetError> load_json(
        std::string_view text, std::string_view name = "<memory>");

    SceneAsset(const SceneAsset&) = delete;
    SceneAsset& operator=(const SceneAsset&) = delete;
    SceneAsset(SceneAsset&&) noexcept;
    SceneAsset& operator=(SceneAsset&&) noexcept;
    ~SceneAsset();

    // 唯一身份：每次成功加载单调递增，不回绕、不暴露可写（seam：asset_test_*）。
    std::uint64_t asset_id() const;
    // 场景名（meta.name 缺省 → 空串）；引用仅在 asset 存活期内有效。
    std::string_view name() const;

    int layer_count() const;
    // 只读引用（存活期）；index 越界 → 断言失败（调用方先查 layer_count）。
    const LayerInfo& layer(int index) const;

    int entity_count() const;
    // 值快照（复制），sprite.asset_id 由本 asset 的 asset_id 填充。
    SceneEntity entity(int index) const;

    int palette_count() const;
    // 调色板颜色；index 越界 → 返回黑色并记 warning（防御性：调用方先查
    // palette_count；越界不崩溃）。
    Color palette_color(int index) const;
    int tile_width() const;   // bare（无 tilesets/palette 且层空）→ 0
    int tile_height() const;

    // ── 动画集访问（plan-5.4 §3.1） ──
    // 每个含 animations 的 entity 对应一个 AnimationSet（0..count-1）；
    // 引用仅在 asset 存活期内有效。无动画集 → count()==0，animations(idx) 断言。
    int animation_set_count() const;
    const AnimationSet& animation_set(int index) const;

private:
    // 构造/解析在 src/scene_asset.cpp 完成：load 是成员函数可直接调用私有构造器。
    // 数据载体 detail::SceneImpl（不透明指针，公共头不暴露资源内部）。
    explicit SceneAsset(std::unique_ptr<detail::SceneImpl> impl);
    std::unique_ptr<detail::SceneImpl> impl_;

    // 解析器 detail::SceneLoader（scene_asset.cpp）需调用私有构造器：friend。
    friend class detail::SceneLoader;

    // tile 查询自由函数（plan-5.2 §4 定案为自由函数）需要读 Impl：friend 声明。
    friend TileQueryResult is_solid_at(const SceneAsset& asset, Vec2 world);
    friend TileQueryResult rect_hits_solid(const SceneAsset& asset, Rect world_rect);
    friend TileLookupResult tile_at(const SceneAsset& asset, int layer_index,
                                    Vec2 world, int* out_value);
    // 渲染自由函数（plan-5.3 §2）需要读 Impl（tile 数据 / 图集槽位）：friend。
    friend RenderResult render_scene(const SceneAsset& asset);
    friend RenderResult render_sprite(const SceneAsset& asset,
                                      const SpriteDesc& sprite, Vec2 pos,
                                      Color tint);
};

// ════════════════════ tile-only 查询（自由函数） ════════════════════

// plan-5.2 §4 固定语义：只查 solid==true 的层；层矩形外 = 无数据 = 不阻挡；
// 像素→tile = 世界坐标减层 origin 后 floor（负坐标同样 floor）；
// 矩形用半开区间 [x,x+w)×[y,y+h)；多 solid 层按层序短路；无 solid 层 → clear。
//
// error 条件（plan-5.2 §4 定案）：坐标/尺寸非有限、矩形 w/h 非正、layer 越界。
// 全部查询为 const 自由函数、无锁，单线程调用方推进。

// 单点查询：返回 error / clear / solid。
TileQueryResult is_solid_at(const SceneAsset& asset, Vec2 world);

// 矩形查询：任一 solid 非空 tile 命中即 solid（短路）；全扫无命中才 clear。
TileQueryResult rect_hits_solid(const SceneAsset& asset, Rect world_rect);

// 取某层 tile：合法层外/空格 → empty 且 *out=-1；占用 → occupied + 值；
// 非法参数（layer 越界 / out==nullptr / 坐标非有限）→ error，不写 out。
TileLookupResult tile_at(const SceneAsset& asset, int layer_index, Vec2 world,
                         int* out_value);

}  // namespace tg