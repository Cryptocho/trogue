// scene_asset.cpp —— SceneAsset 解析/校验/tile 查询实现（plan-5.2 §2/§3/§4）。
//
// schema 校验语义全集对齐 C 版定案（键白名单、三态模式、限额、路径 grammar、
// 空值边界），仅换载体 jansson→nlohmann；禁止因换库放宽任何规则。
// 校验顺序固定（§2.2）：root → 模式判定 → 尺寸 → tilesets/palette/layers →
// entity → sprite/animations → payload 限额。任一失败即整体失败且不产生半成品。
#include "trogue/scene.hpp"

#include <algorithm>  // std::clamp（rect_hits_solid 范围校验）
#include <cmath>     // std::floor / std::fabs
#include <cstdint>
#include <cstdio>    // fprintf（root 未知键 warning 的兜底日志）
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "raylib.h"  // TraceLog（engine 内部日志，公共 API 不暴露）

#include "util/color_check.hpp"
#include "util/json_check.hpp"
#include "util/path_check.hpp"
#include "scene_impl.hpp"  // detail::SceneImpl 等私有数据载体（render.cpp 共享）
#include "tileset_parse.hpp"  // tro-tileset 共享解析核心声明（plan-12 §4.1）

namespace tg {

namespace {

// 匿名 ns 便捷别名（detail 数据载体在 tg::detail）
using detail::AnimClip;
using detail::AnimClipFrame;
using detail::AnimData;
using detail::SceneImpl;

using nlohmann::json;

// role 一段短路径描述（诊断拼接用）
using Error = SceneAsset::AssetError;

Error err(ErrorCode code, std::string msg) { return Error{code, std::move(msg)}; }

// 带路径前缀的拒绝消息
std::string at(std::string_view where, std::string msg) {
    return std::string(where) + ": " + msg;
}

// ---------- asset_id：进程内单调递增，不回绕 ----------
std::uint64_t g_next_asset_id = 1;  // 0 保留给「无归属」

std::uint64_t next_asset_id() {
    if (g_next_asset_id == std::numeric_limits<std::uint64_t>::max()) {
        return 0;  // 耗尽：调用方转 kResourceExhausted
    }
    return g_next_asset_id++;
}

// ---------- 文件读取（外部 scene 路径与 tileset 引用路径共用） ----------
expected<std::string, Error> read_text_file(const std::string& path,
                                            std::string_view role) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return tl::unexpected(err(ErrorCode::kIoError,
                                       at(role, "无法读取文件: " + path)));
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    if (in.bad()) return tl::unexpected(err(ErrorCode::kIoError,
                                            at(role, "读取文件失败: " + path)));
    return text;  // NRVO：无需显式 move（-Wredundant-move）
}

// JSON 解析（不抛；失败返回诊断）。允许 parse 已知失败信息进 message。
expected<json, Error> parse_json_text(const std::string& text,
                                      std::string_view role) {
    try {
        return json::parse(text);  // 解析失败抛 parse_error，捕获转诊断
    } catch (const std::exception& e) {
        return tl::unexpected(err(ErrorCode::kParseError, at(role, e.what())));
    }
}

}  // namespace

// ════════════════════ Impl（私有数据） ════════════════════

// ════════════════════ 资产私有数据载体 ════════════════════
// 定义于私有头 scene_impl.hpp（render.cpp 共享）；SceneImpl::~SceneImpl()
// 的实现位于 render.cpp（图集贴图释放，避免 raylib 类型泄漏到本头/公共面）。

// ════════════════════ 值访问器 ════════════════════

std::uint64_t SceneAsset::asset_id() const { return impl_->id; }
std::string_view SceneAsset::name() const { return impl_->name; }
int SceneAsset::layer_count() const {
    return static_cast<int>(impl_->layers.size());
}
const LayerInfo& SceneAsset::layer(int index) const {
    // 契约：调用方先查 layer_count；越界为程序错误（不静默）
    return impl_->layers[static_cast<std::size_t>(index)];
}
int SceneAsset::entity_count() const {
    return static_cast<int>(impl_->entities.size());
}
SceneEntity SceneAsset::entity(int index) const {
    SceneEntity e = impl_->entities[static_cast<std::size_t>(index)];
    e.sprite.asset_id = impl_->id;  // 取快照时填充归属
    return e;
}
int SceneAsset::palette_count() const {
    return static_cast<int>(impl_->palette.size());
}
Color SceneAsset::palette_color(int index) const {
    // 越界 → 黑色 + 警告（调用方应先查 palette_count；防御性兜底）
    if (index < 0 || index >= static_cast<int>(impl_->palette.size())) {
        TraceLog(LOG_WARNING, "[scene] palette_color(%d) 越界，返回黑色", index);
        return Color{0, 0, 0, 255};
    }
    return impl_->palette[static_cast<std::size_t>(index)];
}
int SceneAsset::tile_width() const { return impl_->tile_w; }
int SceneAsset::tile_height() const { return impl_->tile_h; }

int SceneAsset::animation_set_count() const {
    return static_cast<int>(impl_->anim_sets.size());
}
const AnimationSet& SceneAsset::animation_set(int index) const {
    // 契约：调用方先查 animation_set_count；越界为程序错误（不静默）
    return impl_->anim_sets[static_cast<std::size_t>(index)];
}

SceneAsset::SceneAsset(std::unique_ptr<detail::SceneImpl> impl)
    : impl_(std::move(impl)) {}
SceneAsset::SceneAsset(SceneAsset&&) noexcept = default;
SceneAsset& SceneAsset::operator=(SceneAsset&&) noexcept = default;
SceneAsset::~SceneAsset() = default;

// ════════════════════ 私有解析（detail 效用） ════════════════════

namespace {

// 场景内 tex 路径相对 assets/ 解析后的绝对相对路径
std::string assets_path(std::string_view rel) {
    return std::string(kAssetsDir) + "/" + std::string(rel);
}

// 校验 meta（宽容：未知键忽略+warning）。
expected<void, Error> parse_meta(const json& root, detail::SceneImpl& out) {
    const auto it = root.find("meta");
    if (it == root.end()) return {};  // 缺省
    if (!it->is_object()) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at("meta", "必须是 object")));

    if (auto n = it->find("name"); n != it->end()) {
        if (!n->is_string()) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at("meta.name", "必须是 string")));
        const std::string& s = n->get_ref<const std::string&>();
        if (s.size() >= static_cast<std::size_t>(kNameMax)) return tl::unexpected(
            err(ErrorCode::kSchemaViolation,
                at("meta.name", "超长（≥ kNameMax）")));
        out.name = s;
    }
    if (auto b = it->find("background"); b != it->end()) {
        if (!b->is_string() || b->get_ref<const std::string&>().empty()) {
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      at("meta.background", "必须是 #rrggbb")));
        }
        auto c = detail::parse_hex_color(b->get_ref<const std::string&>());
        if (!c) return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                          at("meta.background", "非法颜色")));
        out.background = *c;
    }
    // meta 内其它未知 key：宽容忽略（对导出器扩展留口）
    return {};
}

}  // namespace（匿名段暂闭：下方共享解析核心属 tg::detail，plan-12 §4.1）

// ── tro-tileset 文档解析（场景与 TerrainTable 共用核心，plan-12 §4.1）──
// 声明见 tileset_parse.hpp。terrain 字段此前宽容路过、零解析，本期起解析 + 校验；
// 文档级未知键宽容策略不变。
namespace detail {

namespace {

// peering_bits 键名 → 方向位（8 名 = Godot CELL_NEIGHBOR_ENUM_TO_TEXT square 合法名）
std::optional<TerrainBit> terrain_bit_from_name(std::string_view name) {
    if (name == "top_side") return TerrainBit::top_side;
    if (name == "top_right_corner") return TerrainBit::top_right_corner;
    if (name == "right_side") return TerrainBit::right_side;
    if (name == "bottom_right_corner") return TerrainBit::bottom_right_corner;
    if (name == "bottom_side") return TerrainBit::bottom_side;
    if (name == "bottom_left_corner") return TerrainBit::bottom_left_corner;
    if (name == "left_side") return TerrainBit::left_side;
    if (name == "top_left_corner") return TerrainBit::top_left_corner;
    return std::nullopt;
}

// terrain_sets：可选键；缺省/空数组 = 无 terrain 数据；≤kTerrainSetsMax 组。
// 元素 {mode, terrains:[{name,color}]}（terrains 1..kTerrainsPerSetMax）。
expected<void, Error> parse_terrain_sets(const json& ts, std::string_view src,
                                         std::vector<TerrainSetInfo>& out) {
    const auto it = ts.find("terrain_sets");
    if (it == ts.end()) return {};  // 缺省 = 无
    if (!it->is_array()) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at(src, "terrain_sets 必须为 array")));
    if (it->empty()) return {};  // 空数组 ≡ 缺省
    if (static_cast<int>(it->size()) > kTerrainSetsMax) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at(src, "terrain_sets 超过 kTerrainSetsMax")));
    for (std::size_t i = 0; i < it->size(); ++i) {
        const std::string where = "terrain_sets[" + std::to_string(i) + "]";
        const json& s = (*it)[i];
        if (!s.is_object()) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(src, where + " 必须为 object")));
        const auto mode_it = s.find("mode");
        if (mode_it == s.end() || !mode_it->is_string())
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      at(src, where + ".mode 必须为 string")));
        TerrainMode mode{};
        const std::string& m = mode_it->get_ref<const std::string&>();
        if (m == "sides") mode = TerrainMode::sides;
        else if (m == "corners") mode = TerrainMode::corners;
        else if (m == "corners_and_sides") mode = TerrainMode::corners_and_sides;
        else return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(src, where + ".mode 非法: " + m)));
        const auto terr = s.find("terrains");
        if (terr == s.end() || !terr->is_array() || terr->empty())
            return tl::unexpected(err(
                ErrorCode::kSchemaViolation,
                at(src, where + ".terrains 必须为 1..kTerrainsPerSetMax 的数组")));
        if (static_cast<int>(terr->size()) > kTerrainsPerSetMax)
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      at(src, where + ".terrains 超过 kTerrainsPerSetMax")));
        for (std::size_t j = 0; j < terr->size(); ++j) {
            const std::string twhere = where + ".terrains[" + std::to_string(j) + "]";
            const json& t = (*terr)[j];
            if (!t.is_object()) return tl::unexpected(
                err(ErrorCode::kSchemaViolation, at(src, twhere + " 必须为 object")));
            const auto name = t.find("name"), color = t.find("color");
            if (name == t.end() || !name->is_string())
                return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                          at(src, twhere + ".name 必须为 string")));
            if (color == t.end() || !color->is_string() ||
                !parse_hex_color(color->get_ref<const std::string&>()))
                return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                          at(src, twhere + ".color 必须为 #rrggbb")));
        }
        out.push_back(TerrainSetInfo{mode, static_cast<int>(terr->size())});
    }
    return {};
}

// per-tile terrain 字段（terrain_set/terrain/peering_bits；键缺省 = -1/空）。
expected<void, Error> parse_tile_terrain(const json& t, std::string_view src,
                                         const std::vector<TerrainSetInfo>& sets,
                                         int index, TerrainTileEntry& out) {
    const std::string where = "tiles[" + std::to_string(index) + "]";
    int tset = -1, terr = -1;
    if (auto it = t.find("terrain_set"); it != t.end()) {
        if (!it->is_number_integer()) return tl::unexpected(err(
            ErrorCode::kSchemaViolation, at(src, where + ".terrain_set 必须为 int")));
        tset = it->get<int>();
    }
    if (auto it = t.find("terrain"); it != t.end()) {
        if (!it->is_number_integer())
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      at(src, where + ".terrain 必须为 int")));
        terr = it->get<int>();
    }
    // 负值防御：仅 -1 表示未归属，< -1 一律拒绝（plan §4.1 表；评审阻断项）
    if (tset < -1 || terr < -1)
        return tl::unexpected(err(
            ErrorCode::kSchemaViolation,
            at(src, where + " terrain_set/terrain 负值非法（仅 -1 表示未归属）")));
    // 归属一致性：两者同 -1 或同 ≥0
    if ((tset == -1) != (terr == -1))
        return tl::unexpected(err(
            ErrorCode::kSchemaViolation,
            at(src, where + " terrain_set/terrain 必须同时为 -1 或同时有效")));
    if (tset >= 0) {
        if (tset >= static_cast<int>(sets.size()))
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      at(src, where + ".terrain_set 越界")));
        if (terr >= sets[static_cast<std::size_t>(tset)].terrain_count)
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      at(src, where + ".terrain 越界")));
    }
    out.terrain_set = tset;
    out.terrain = terr;
    // peering_bits：可选 object；需要有效归属（mode/值域都挂在 set 上）
    if (auto pb = t.find("peering_bits"); pb != t.end()) {
        if (!pb->is_object()) return tl::unexpected(err(
            ErrorCode::kSchemaViolation, at(src, where + ".peering_bits 必须为 object")));
        if (tset < 0) return tl::unexpected(err(
            ErrorCode::kSchemaViolation,
            at(src, where + ".peering_bits 需要有效 terrain_set（当前 -1）")));
        const TerrainMode mode = sets[static_cast<std::size_t>(tset)].mode;
        const int terr_count = sets[static_cast<std::size_t>(tset)].terrain_count;
        for (const auto& kv : pb->items()) {
            const auto bit = terrain_bit_from_name(kv.key());
            if (!bit) return tl::unexpected(err(
                ErrorCode::kSchemaViolation,
                at(src, where + ".peering_bits 未知邻位名 " + kv.key())));
            if (!terrain_bit_valid(mode, *bit)) return tl::unexpected(err(
                ErrorCode::kSchemaViolation,
                at(src, where + ".peering_bits 邻位种类与 mode 不符: " + kv.key())));
            if (!kv.value().is_number_integer())
                return tl::unexpected(err(
                    ErrorCode::kSchemaViolation,
                    at(src, where + ".peering_bits." + kv.key() + " 必须为 int")));
            const int v = kv.value().get<int>();
            if (v < 0 || v >= terr_count)
                return tl::unexpected(err(
                    ErrorCode::kSchemaViolation,
                    at(src, where + ".peering_bits." + kv.key() + " 越界")));
            out.bits[static_cast<std::size_t>(*bit)] = v;
        }
    }
    return {};
}

}  // namespace

// tro-tileset 文档全量解析（共享核心；source 仅用于错误上下文）。
expected<TilesetParsed, Error> parse_tileset_document(const json& ts,
                                                      std::string_view source) {
    // format/version 严格
    const auto fmt = ts.find("format");
    if (fmt == ts.end() || !fmt->is_string() ||
        fmt->get_ref<const std::string&>() != "tro-tileset") {
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  at(source, "format 必须为 tro-tileset")));
    }
    const auto ver = ts.find("version");
    if (ver == ts.end() || !ver->is_number_integer() || ver->get<int>() != 2) {
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  at(source, "version 必须为 2")));
    }
    const auto tw = ts.find("tile_width"), th = ts.find("tile_height");
    if (tw == ts.end() || th == ts.end() || !tw->is_number_integer() ||
        !th->is_number_integer()) {
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  at(source, "tile_width/height 必须为 int")));
    }
    const auto tex = ts.find("texture");
    if (tex == ts.end() || !tex->is_string() ||
        !is_safe_relative_path(tex->get_ref<const std::string&>())) {
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  at(source, "texture 路径非法")));
    }
    const auto tiles = ts.find("tiles");
    if (tiles == ts.end() || !tiles->is_array() || tiles->empty()) {
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  at(source, "tiles 必须为非空数组")));
    }
    const int count = static_cast<int>(tiles->size());
    if (count > 1 << 20) {  // 防御：id 值域不得失控（实际由受限场景宽高约束）
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  at(source, "tiles 数量超限")));
    }
    TilesetParsed out;
    out.tile_w = tw->get<int>();
    out.tile_h = th->get<int>();
    out.tile_count = count;
    out.texture = tex->get_ref<const std::string&>();
    // columns：图集每行 tile 数（tile 矩形布局；render 用）。缺省/非法拒绝。
    const auto cols = ts.find("columns");
    if (cols == ts.end() || !cols->is_number_integer() || cols->get<int>() < 1 ||
        cols->get<int>() > 65536) {
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  at(source, "columns 必须为 1..65536 的 int")));
    }
    out.columns = cols->get<int>();
    // terrain_sets（plan-12 §4.1：此前宽容路过，本期起解析 + 校验）
    if (auto r = parse_terrain_sets(ts, source, out.terrain_sets); !r)
        return tl::unexpected(r.error());
    // 逐一解析 tiles[]：数组顺序即 id；每个 tile 自带 col/row（图集内坐标），
    // 建 id → TileVisual 表供渲染（不允许按 id 推公式——Godot 导出的 col/row
    // 可能非顺序排列）。size_in_atlas/texture_origin/y_sort_origin 为 tro-tileset
    // v2 只增可选字段（plan-8 §3.1），缺省 = 单格 1×1 / 原点 0。
    out.tile_visuals.reserve(static_cast<std::size_t>(count));
    out.tile_terrains.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const json& t = (*tiles)[static_cast<std::size_t>(i)];
        if (!t.is_object()) return tl::unexpected(err(
            ErrorCode::kSchemaViolation,
            at(source, "tiles[" + std::to_string(i) + "] 必须为 object")));
        // id 字段（若有）必须等于数组下标，防错位（顺序即 id 契约）。
        if (auto id_it = t.find("id"); id_it != t.end()) {
            if (!id_it->is_number_integer() || id_it->get<int>() != i)
                return tl::unexpected(err(
                    ErrorCode::kSchemaViolation,
                    at(source, "tiles[" + std::to_string(i)
                                   + "].id 与数组顺序不符")));
        }
        const auto col_it = t.find("col"), row_it = t.find("row");
        if (col_it == t.end() || row_it == t.end() || !col_it->is_number_integer() ||
            !row_it->is_number_integer() || col_it->get<int>() < 0 ||
            row_it->get<int>() < 0) {
            return tl::unexpected(err(
                ErrorCode::kSchemaViolation,
                at(source, "tiles[" + std::to_string(i)
                               + "] 缺少合法的 col/row（非负 int）")));
        }
        const int col = col_it->get<int>(), row = row_it->get<int>();
        // size_in_atlas：可选 [w,h]，各 ∈ [1,4096]——tile 覆盖的图集格子数，缺省 1×1。
        // region 越界（col+sw > columns / 超出贴图）不在 load 期校验：与 col/row 同
        // （load 不读纹理文件），绘制期采样行为由 raylib 兜底（plan-8 §3.1）。
        int sw = 1, sh = 1;
        if (auto sz = t.find("size_in_atlas"); sz != t.end()) {
            const std::string where =
                "tiles[" + std::to_string(i) + "].size_in_atlas";
            if (!sz->is_array() || sz->size() != 2 ||
                !(*sz)[0].is_number_integer() || !(*sz)[1].is_number_integer()) {
                return tl::unexpected(err(
                    ErrorCode::kSchemaViolation,
                    at(source, where + " 必须为 [w,h] int 数组")));
            }
            sw = (*sz)[0].get<int>();
            sh = (*sz)[1].get<int>();
            if (sw < 1 || sw > kTileSizeInAtlasMax || sh < 1 ||
                sh > kTileSizeInAtlasMax) {
                return tl::unexpected(err(
                    ErrorCode::kSchemaViolation,
                    at(source, where + " 元素必须为 1.."
                                       + std::to_string(kTileSizeInAtlasMax))));
            }
        }
        // texture_origin：可选 [x,y] int（可负），|v| ≤ kTileOriginMax——Godot
        // 纹理原点，绘制偏移 = −origin（plan-8 §2.2）；缺省 (0,0)。
        Vec2 t_origin{0.0f, 0.0f};
        if (auto to = t.find("texture_origin"); to != t.end()) {
            const std::string where =
                "tiles[" + std::to_string(i) + "].texture_origin";
            if (!to->is_array() || to->size() != 2 ||
                !(*to)[0].is_number_integer() || !(*to)[1].is_number_integer()) {
                return tl::unexpected(err(
                    ErrorCode::kSchemaViolation,
                    at(source, where + " 必须为 [x,y] int 数组")));
            }
            const int ox = (*to)[0].get<int>(), oy = (*to)[1].get<int>();
            if (ox > kTileOriginMax || ox < -kTileOriginMax ||
                oy > kTileOriginMax || oy < -kTileOriginMax) {
                return tl::unexpected(err(
                    ErrorCode::kSchemaViolation,
                    at(source, where + " 绝对值超限")));
            }
            t_origin = Vec2{static_cast<float>(ox), static_cast<float>(oy)};
        }
        // y_sort_origin：可选 int，|v| ≤ kTileOriginMax——Godot y-sort 排序键偏移
        // 透传存储，引擎暂不消费（无逐 tile y-sort，plan-8 §3.3）；缺省 0。
        int yso = 0;
        if (auto ys = t.find("y_sort_origin"); ys != t.end()) {
            const std::string where =
                "tiles[" + std::to_string(i) + "].y_sort_origin";
            if (!ys->is_number_integer()) {
                return tl::unexpected(err(
                    ErrorCode::kSchemaViolation,
                    at(source, where + " 必须为 int")));
            }
            yso = ys->get<int>();
            if (yso > kTileOriginMax || yso < -kTileOriginMax) {
                return tl::unexpected(err(
                    ErrorCode::kSchemaViolation,
                    at(source, where + " 绝对值超限")));
            }
        }
        // terrain 字段（terrain_set/terrain/peering_bits；plan-12 §4.1）
        TerrainTileEntry entry;
        if (auto r = parse_tile_terrain(t, source, out.terrain_sets, i, entry); !r)
            return tl::unexpected(r.error());
        SceneImpl::TilesetMeta::TileVisual tv;
        tv.region = Rect{static_cast<float>(col * out.tile_w),
                         static_cast<float>(row * out.tile_h),
                         static_cast<float>(sw * out.tile_w),
                         static_cast<float>(sh * out.tile_h)};
        tv.texture_origin = t_origin;
        tv.y_sort_origin = yso;
        out.tile_visuals.push_back(tv);
        out.tile_terrains.push_back(entry);
    }
    return out;
}

// 读文件 + 解析 JSON + parse_tileset_document（诊断与场景侧 tileset 引用一致）。
expected<TilesetParsed, Error> load_tileset_document(std::string_view rel_path) {
    const std::string full = assets_path(rel_path);
    auto text_or = read_text_file(full, "tileset 引用 " + std::string(rel_path));
    if (!text_or) return tl::unexpected(text_or.error());
    auto j_or = parse_json_text(*text_or, full);
    if (!j_or) return tl::unexpected(j_or.error());
    return parse_tileset_document(*j_or, rel_path);
}

}  // namespace detail

namespace {

// 读取并解析 tro-tileset v2 元数据（load 期；不读纹理文件）。解析核心
// detail::parse_tileset_document 与 TerrainTable 加载共用（plan-12 §4.1）。
expected<void, Error> load_tileset_meta(const std::string& rel_path,
                                        int scene_tile_w, int scene_tile_h,
                                        detail::SceneImpl& out) {
    auto doc = detail::load_tileset_document(rel_path);
    if (!doc) return tl::unexpected(doc.error());
    if (doc->tile_w != scene_tile_w || doc->tile_h != scene_tile_h)
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  at(rel_path, "tile 尺寸与场景不一致")));
    detail::SceneImpl::TilesetMeta m;
    m.name = "";  // 由场景端填
    m.path = rel_path;
    m.tile_w = doc->tile_w;
    m.tile_h = doc->tile_h;
    m.tile_count = doc->tile_count;
    m.texture = std::move(doc->texture);
    m.columns = doc->columns;
    m.tile_visuals = std::move(doc->tile_visuals);
    m.terrain_sets = std::move(doc->terrain_sets);
    m.tile_terrains = std::move(doc->tile_terrains);
    out.tilesets.push_back(std::move(m));
    out.atlas_textures.emplace_back();  // 图集贴图懒加载槽与 tilesets 对齐
    return {};
}

// 解析 palette 数组。
expected<void, Error> parse_palette(const json& arr, detail::SceneImpl& out) {
    if (arr.empty()) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at("tilemap.palette", "空数组不合法")));
    if (static_cast<int>(arr.size()) > kPaletteMax) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at("tilemap.palette", "超过 kPaletteMax")));
    for (std::size_t i = 0; i < arr.size(); ++i) {
        const json& item = arr[i];
        if (!item.is_string()) return tl::unexpected(
            err(ErrorCode::kSchemaViolation,
                at("tilemap.palette[" + std::to_string(i) + "]", "必须是 #rrggbb")));
        auto c = detail::parse_hex_color(item.get_ref<const std::string&>());
        if (!c) return tl::unexpected(
            err(ErrorCode::kSchemaViolation,
                at("tilemap.palette[" + std::to_string(i) + "]", "非法颜色")));
        out.palette.push_back(*c);
    }
    return {};
}

// 解析单个 tileset 引用（场景端）：name/path 白名单 + 唯一性 + 路径。
// 返回该引用的合法 name；同时把 tileset JSON 元数据读进 out.tilesets。
expected<void, Error> parse_tileset_entry(const json& item, std::size_t index,
                                          int scene_tile_w, int scene_tile_h,
                                          detail::SceneImpl& out) {
    const std::string where = "tilemap.tilesets[" + std::to_string(index) + "]";
    if (!item.is_object()) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at(where, "必须是 object")));
    // 键白名单
    for (const auto& kv : item.items()) {
        if (kv.key() != "name" && kv.key() != "path") {
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      at(where, "未知键 " + kv.key())));
        }
    }
    const auto name_it = item.find("name");
    if (name_it == item.end() || !name_it->is_string()) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at(where, "name 必填 string")));
    const std::string& name = name_it->get_ref<const std::string&>();
    if (name.empty() || name.size() >= static_cast<std::size_t>(kNameMax))
        return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "name 非法/超长")));
    for (const auto& t : out.tilesets) {
        if (t.name == name) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "name 重复: " + name)));
    }
    const auto path_it = item.find("path");
    if (path_it == item.end() || !path_it->is_string()) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at(where, "path 必填 string")));
    const std::string& path = path_it->get_ref<const std::string&>();
    if (!detail::is_safe_relative_path(path)) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at(where, "path 非法")));

    // 读 tileset 元数据（注入 name 后入表）
    const std::size_t slot = out.tilesets.size();
    if (auto r = load_tileset_meta(path, scene_tile_w, scene_tile_h, out); !r)
        return tl::unexpected(r.error());
    out.tilesets[slot].name = name;  // 引用名
    return {};
}

// 解析 layers（模式确定后调用；palette 模式下 tileset 字段禁用）。
expected<void, Error> parse_tilemap_layers(const json& tm, bool atlas_mode,
                                           detail::SceneImpl& out) {
    const auto it = tm.find("layers");
    if (it == tm.end()) return {};  // 缺省 ≡ []
    if (!it->is_array()) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at("tilemap.layers", "必须是 array")));
    if (static_cast<int>(it->size()) > kLayerMax) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at("tilemap.layers", "超过 kLayerMax")));

    for (std::size_t li = 0; li < it->size(); ++li) {
        const std::string where = "tilemap.layers[" + std::to_string(li) + "]";
        const json& l = (*it)[li];
        if (!l.is_object()) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "必须是 object")));
        for (const auto& kv : l.items()) {
            const std::string& k = kv.key();
            if (k != "name" && k != "width" && k != "height" && k != "origin" &&
                k != "tileset" && k != "tiles" && k != "solid") {
                return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                          at(where, "未知键 " + k)));
            }
        }
        LayerInfo info;
        if (auto n = l.find("name"); n != l.end() && n->is_string()) {
            info.name = n->get_ref<const std::string&>();
            if (info.name.size() >= static_cast<std::size_t>(kNameMax))
                return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                          at(where, "name 超长")));
        } else {
            info.name = "layer";
        }
        const auto w_it = l.find("width"), h_it = l.find("height");
        if (w_it == l.end() || h_it == l.end() || !w_it->is_number_integer() ||
            !h_it->is_number_integer()) {
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      at(where, "width/height 必填 int")));
        }
        const int w = w_it->get<int>(), h = h_it->get<int>();
        if (w < 1 || w > kLayerDimMax || h < 1 || h > kLayerDimMax)
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      at(where, "width/height 越界")));
        // checked 乘法（kLayerDimMax^2 远小于 int 上限，仍显式防溢出）
        if (w > std::numeric_limits<int>::max() / h) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "width*height 溢出")));
        info.width = w;
        info.height = h;

        if (auto o = l.find("origin"); o != l.end()) {
            if (!o->is_array() || o->size() != 2) return tl::unexpected(
                err(ErrorCode::kSchemaViolation, at(where, "origin 必须为 [ox,oy]")));
            auto ox = detail::json_as_int((*o)[0]);
            auto oy = detail::json_as_int((*o)[1]);
            if (!ox || !oy) return tl::unexpected(
                err(ErrorCode::kSchemaViolation, at(where, "origin 必须为 int")));
            info.origin_x = *ox;
            info.origin_y = *oy;
        }
        if (auto s = l.find("solid"); s != l.end()) {
            if (!s->is_boolean()) return tl::unexpected(
                err(ErrorCode::kSchemaViolation, at(where, "solid 必须为 bool")));
            info.solid = s->get<bool>();
        }

        // tileset 引用（键白名单已保证只可能是 "tileset"）
        const auto ts_it = l.find("tileset");
        if (atlas_mode) {
            if (ts_it == l.end() || !ts_it->is_string()) return tl::unexpected(
                err(ErrorCode::kSchemaViolation,
                    at(where, "图集模式每层必填 tileset 引用")));
            const std::string& ref = ts_it->get_ref<const std::string&>();
            int found = -1;
            for (std::size_t t = 0; t < out.tilesets.size(); ++t) {
                if (out.tilesets[t].name == ref) { found = static_cast<int>(t); break; }
            }
            if (found < 0) return tl::unexpected(
                err(ErrorCode::kSchemaViolation,
                    at(where, "tileset 引用不存在: " + ref)));
            info.tileset_index = found;
            info.tileset_name = ref;
        } else {
            if (ts_it != l.end()) return tl::unexpected(
                err(ErrorCode::kSchemaViolation,
                    at(where, "palette/bare 模式层不得携带 tileset")));
        }

        // tiles 数组
        const auto t_it = l.find("tiles");
        if (t_it == l.end() || !t_it->is_array()) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "tiles 必填 array")));
        const std::size_t expect = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        if (t_it->size() != expect) return tl::unexpected(
            err(ErrorCode::kSchemaViolation,
                at(where, "tiles 长度须 = width*height")));
        // 值域：-1 空；否则 [0,count)。count 由模式决定。
        int range = 0;
        if (atlas_mode) {
            range = out.tilesets[static_cast<std::size_t>(info.tileset_index)].tile_count;
        } else {
            range = static_cast<int>(out.palette.size());  // palette 已先解析
        }
        std::vector<int> tiles;
        tiles.reserve(t_it->size());
        int nonempty = 0;
        for (std::size_t i = 0; i < t_it->size(); ++i) {
            const json& tv = (*t_it)[i];
            if (!tv.is_number_integer()) return tl::unexpected(
                err(ErrorCode::kSchemaViolation,
                    at(where, "tiles[" + std::to_string(i) + "] 必须为 int")));
            const int v = tv.get<int>();
            if (v != -1 && (v < 0 || v >= range)) return tl::unexpected(
                err(ErrorCode::kSchemaViolation,
                    at(where, "tiles[" + std::to_string(i) + "] 值域越界")));
            tiles.push_back(v);
            if (v != -1) ++nonempty;
        }
        info.nonempty = nonempty;  // 非空 tile 数随层快照输出
        out.layers.push_back(std::move(info));
        out.layer_tiles.push_back(std::move(tiles));
    }
    return {};
}

// 解析 sprite（两种形态互斥 + 键白名单，plan-5.2 §2.5）。
expected<void, Error> parse_sprite(const json& s, std::string_view where,
                                   const detail::SceneImpl& out, SpriteDesc& dst) {
    if (!s.is_object()) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at(where, "sprite 必须是 object")));
    // 两形态判定
    const bool has_ts = s.contains("tileset"), has_tex = s.contains("texture");
    if (has_ts && has_tex) return tl::unexpected(
        err(ErrorCode::kSchemaViolation,
            at(where, "sprite 图集与独立贴图形态互斥")));
    if (has_ts) {
        for (const auto& kv : s.items()) {
            if (kv.key() != "tileset" && kv.key() != "tile")
                return tl::unexpected(err(ErrorCode::kSchemaViolation, at(
                    where, "图集形态 sprite 未知键 " + kv.key())));
        }
        const auto name_it = s.find("tileset");
        const auto tile_it = s.find("tile");
        if (name_it == s.end() || tile_it == s.end() ||
            !name_it->is_string() || !tile_it->is_number_integer())
            return tl::unexpected(
                err(ErrorCode::kSchemaViolation,
                    at(where, "图集形态须 tileset:string 与 tile:int")));
        const std::string& ref = name_it->get_ref<const std::string&>();
        int idx = -1;
        for (std::size_t t = 0; t < out.tilesets.size(); ++t) {
            if (out.tilesets[t].name == ref) { idx = static_cast<int>(t); break; }
        }
        if (idx < 0) return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                               at(where, "tileset 引用不存在: " + ref)));
        const int tile = tile_it->get<int>();
        if (tile < 0 || tile >= out.tilesets[static_cast<std::size_t>(idx)].tile_count)
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      at(where, "tile id 越界")));
        dst.has = true;
        dst.tileset_index = idx;
        dst.tile = tile;
        return {};
    }
    // 独立贴图形态
    for (const auto& kv : s.items()) {
        const std::string& k = kv.key();
        if (k != "texture" && k != "region" && k != "offset")
            return tl::unexpected(err(ErrorCode::kSchemaViolation, at(
                where, "独立贴图形态 sprite 未知键 " + k)));
    }
    const auto tex_it = s.find("texture");
    if (tex_it == s.end() || !tex_it->is_string() ||
        !detail::is_safe_relative_path(tex_it->get_ref<const std::string&>()))
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  at(where, "sprite.texture 路径非法")));
    dst.has = true;
    dst.texture = tex_it->get_ref<const std::string&>();
    if (auto r = s.find("region"); r != s.end()) {
        if (!r->is_array() || r->size() != 4) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "region 必须为 [x,y,w,h]")));
        auto x = detail::json_as_finite_float((*r)[0]);
        auto y = detail::json_as_finite_float((*r)[1]);
        auto w = detail::json_as_finite_float((*r)[2]);
        auto h = detail::json_as_finite_float((*r)[3]);
        if (!x || !y || !w || !h) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "region 数值非法")));
        if (*x < 0 || *y < 0 || *w <= 0 || *h <= 0) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "region x/y>=0 且 w/h>0")));
        dst.region = Rect{*x, *y, *w, *h};
    }
    if (auto o = s.find("offset"); o != s.end()) {
        if (!o->is_array() || o->size() != 2) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "offset 必须为 [ox,oy]")));
        auto ox = detail::json_as_finite_float((*o)[0]);
        auto oy = detail::json_as_finite_float((*o)[1]);
        if (!ox || !oy) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "offset 数值非法")));
        dst.offset = Vec2{*ox, *oy};
    }
    return {};
}

// 解析内嵌 animations（plan-5.2 §2.6 + §2.6b（规则来自 plan-5.4 §2））。
expected<void, Error> parse_animations(const json& a, std::string_view where,
                                       detail::SceneImpl& out) {
    if (!a.is_object()) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at(where, "必须为 object")));
    for (const auto& kv : a.items()) {
        if (kv.key() != "textures" && kv.key() != "animations")
            return tl::unexpected(err(ErrorCode::kSchemaViolation, at(
                where, "animations 未知键 " + kv.key())));
    }
    const auto tex_it = a.find("textures"), clip_it = a.find("animations");
    if (tex_it == a.end() || clip_it == a.end()) return tl::unexpected(
        err(ErrorCode::kSchemaViolation,
            at(where, "必须同时含 textures 与 animations")));
    if (!tex_it->is_array() || !clip_it->is_array()) return tl::unexpected(
        err(ErrorCode::kSchemaViolation,
            at(where, "textures/animations 必须为 array")));
    if (static_cast<int>(tex_it->size()) > kAnimTexturesPerEntityMax)
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  at(where, "textures 超过 kAnimTexturesPerEntityMax")));
    if (static_cast<int>(clip_it->size()) > kAnimClipsPerEntityMax)
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  at(where, "animations 超过 kAnimClipsPerEntityMax")));

    AnimData data;
    data.asset_id = out.id;
    // textures：可为空（空时 animations 必为空，否则帧引用越界拒绝）
    data.textures.reserve(tex_it->size());
    for (std::size_t i = 0; i < tex_it->size(); ++i) {
        const json& t = (*tex_it)[i];
        if (!t.is_string() ||
            !detail::is_safe_relative_path(t.get_ref<const std::string&>()))
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      at(where, "textures[" + std::to_string(i) +
                                                    "] 路径非法")));
        data.textures.push_back(t.get_ref<const std::string&>());
    }

    int total_frames = 0;
    std::vector<std::string> seen_names;
    for (std::size_t ci = 0; ci < clip_it->size(); ++ci) {
        const std::string cwhere =
            std::string(where) + ".animations[" + std::to_string(ci) + "]";
        const json& c = (*clip_it)[ci];
        if (!c.is_object()) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(cwhere, "必须是 object")));
        for (const auto& kv : c.items()) {
            if (kv.key() != "name" && kv.key() != "fps" && kv.key() != "loop" &&
                kv.key() != "frames")
                return tl::unexpected(err(ErrorCode::kSchemaViolation, at(
                    cwhere, "clip 未知键 " + kv.key())));
        }
        AnimClip clip;
        const auto n_it = c.find("name");
        if (n_it == c.end() || !n_it->is_string()) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(cwhere, "name 必填 string")));
        clip.name = n_it->get_ref<const std::string&>();
        if (clip.name.empty() ||
            clip.name.size() >= static_cast<std::size_t>(kNameMax))
            return tl::unexpected(
                err(ErrorCode::kSchemaViolation, at(cwhere, "name 非法/超长")));
        for (const auto& sn : seen_names) {
            if (sn == clip.name) return tl::unexpected(
                err(ErrorCode::kSchemaViolation, at(cwhere, "clip name 集内重复")));
        }
        seen_names.push_back(clip.name);

        const auto fps_it = c.find("fps");
        if (fps_it == c.end()) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(cwhere, "fps 必填")));
        const auto fps = detail::json_as_finite_float(*fps_it);
        if (!fps || *fps <= 0) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(cwhere, "fps 须为有限正数")));
        clip.fps = *fps;
        if (auto l = c.find("loop"); l != c.end()) {
            if (!l->is_boolean()) return tl::unexpected(
                err(ErrorCode::kSchemaViolation, at(cwhere, "loop 必须为 bool")));
            clip.loop = l->get<bool>();
        }
        const auto f_it = c.find("frames");
        if (f_it == c.end() || !f_it->is_array()) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(cwhere, "frames 必填 array")));
        if (static_cast<int>(f_it->size()) > kAnimFramesPerClipMax)
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      at(cwhere, "frames 超过 kAnimFramesPerClipMax")));
        total_frames += static_cast<int>(f_it->size());
        if (total_frames > kAssetAnimFramesMax) return tl::unexpected(
            err(ErrorCode::kSchemaViolation,
                at(where, "全部 clip 帧合计超过 kAssetAnimFramesMax")));
        clip.frames.reserve(f_it->size());
        for (std::size_t fi = 0; fi < f_it->size(); ++fi) {
            const std::string fwhere = cwhere + ".frames[" + std::to_string(fi) + "]";
            const json& f = (*f_it)[fi];
            if (!f.is_object()) return tl::unexpected(
                err(ErrorCode::kSchemaViolation, at(fwhere, "必须是 object")));
            for (const auto& kv : f.items()) {
                const std::string& k = kv.key();
                if (k != "texture" && k != "region" && k != "offset")
                    return tl::unexpected(err(ErrorCode::kSchemaViolation, at(
                        fwhere, "frame 未知键 " + k)));
            }
            AnimClipFrame frame;
            const auto t_idx = f.find("texture");
            if (t_idx == f.end() || !t_idx->is_number_integer()) return tl::unexpected(
                err(ErrorCode::kSchemaViolation, at(fwhere, "texture 必填 int")));
            const int ti = t_idx->get<int>();
            if (ti < 0 || ti >= static_cast<int>(data.textures.size()))
                return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                          at(fwhere, "texture 索引越界")));
            frame.texture = ti;
            if (auto r = f.find("region"); r != f.end()) {
                if (!r->is_array() || r->size() != 4) return tl::unexpected(
                    err(ErrorCode::kSchemaViolation, at(fwhere, "region 必须为 [x,y,w,h]")));
                auto x = detail::json_as_finite_float((*r)[0]);
                auto y = detail::json_as_finite_float((*r)[1]);
                auto w = detail::json_as_finite_float((*r)[2]);
                auto h = detail::json_as_finite_float((*r)[3]);
                if (!x || !y || !w || !h || *x < 0 || *y < 0 || *w <= 0 || *h <= 0)
                    return tl::unexpected(
                        err(ErrorCode::kSchemaViolation, at(fwhere, "region 数值非法")));
                frame.region = Rect{*x, *y, *w, *h};
            }
            if (auto o = f.find("offset"); o != f.end()) {
                if (!o->is_array() || o->size() != 2) return tl::unexpected(
                    err(ErrorCode::kSchemaViolation, at(fwhere, "offset 必须为 [ox,oy]")));
                auto ox = detail::json_as_finite_float((*o)[0]);
                auto oy = detail::json_as_finite_float((*o)[1]);
                if (!ox || !oy) return tl::unexpected(
                    err(ErrorCode::kSchemaViolation, at(fwhere, "offset 数值非法")));
                frame.offset = Vec2{*ox, *oy};
            }
            clip.frames.push_back(frame);
        }
        data.clips.push_back(std::move(clip));
    }
    // 空 textures + 非空 clips → 帧引用必越界（前面已逐帧拒绝）；防御断言
    if (data.textures.empty() && !data.clips.empty()) return tl::unexpected(
        err(ErrorCode::kSchemaViolation,
            at(where, "textures 为空但 clips 非空")));
    out.anims.push_back(std::move(data));
    return {};
}

// 解析单个 entity（plan-5.2 §2.5/§2.6）。
expected<void, Error> parse_entity(const json& e, std::size_t index,
                                   int default_w, int default_h,
                                   detail::SceneImpl& out) {
    const std::string where = "entities[" + std::to_string(index) + "]";
    if (!e.is_object()) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at(where, "必须是 object")));
    for (const auto& kv : e.items()) {
        const std::string& k = kv.key();
        if (k != "id" && k != "type" && k != "x" && k != "y" && k != "w" &&
            k != "h" && k != "z" && k != "color" && k != "solid" &&
            k != "sprite" && k != "animations" && k != "props")
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      at(where, "未知键 " + k)));
    }
    // props：Godot 侧玩法 metadata 透传（v1.1 起预留字段，导出器持续写入）。引擎只
    // 校验形状、不读取不存储——只携带不解释，语义由 game 导入 descriptor 时决定；
    // 未来消费时再扩展 SceneEntity 快照（需求驱动）。
    if (auto p = e.find("props"); p != e.end() && !p->is_object())
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  at(where, "props 必须为 object")));
    SceneEntity ent;
    // id 必填、唯一
    const auto id_it = e.find("id");
    if (id_it == e.end() || !id_it->is_string()) return tl::unexpected(
        err(ErrorCode::kSchemaViolation, at(where, "id 必填 string")));
    ent.id = id_it->get_ref<const std::string&>();
    if (ent.id.empty() || ent.id.size() >= static_cast<std::size_t>(kNameMax))
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  at(where, "id 非法/超长")));
    for (const auto& other : out.entities) {
        if (other.id == ent.id) return tl::unexpected(
            err(ErrorCode::kSchemaViolation,
                at(where, "id 重复: " + ent.id)));
    }
    // type 缺省 unknown
    if (auto t = e.find("type"); t != e.end()) {
        if (!t->is_string()) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "type 必须为 string")));
        ent.type = t->get_ref<const std::string&>();
        if (ent.type.empty() ||
            ent.type.size() >= static_cast<std::size_t>(kNameMax))
            return tl::unexpected(
                err(ErrorCode::kSchemaViolation, at(where, "type 非法/超长")));
    } else {
        ent.type = "unknown";
    }
    // 坐标：x/y 有限可负；缺省 0
    if (auto x = e.find("x"); x != e.end()) {
        auto v = detail::json_as_finite_float(*x);
        if (!v) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "x 数值非法")));
        ent.x = *v;
    }
    if (auto y = e.find("y"); y != e.end()) {
        auto v = detail::json_as_finite_float(*y);
        if (!v) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "y 数值非法")));
        ent.y = *v;
    }
    // w/h：显式 >0 ≤ FLT_MAX/4；缺省 = tile 尺寸 / bare 16
    const auto w_it = e.find("w"), h_it = e.find("h");
    if (w_it != e.end() || h_it != e.end()) {
        auto wv = w_it == e.end() ? std::optional<float>{}
                                  : detail::json_as_finite_float(*w_it);
        auto hv = h_it == e.end() ? std::optional<float>{}
                                  : detail::json_as_finite_float(*h_it);
        if ((w_it != e.end() && (!wv || *wv <= 0)) ||
            (h_it != e.end() && (!hv || *hv <= 0)))
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      at(where, "w/h 须 >0 且有限")));
        ent.w = wv ? *wv : static_cast<float>(default_w);
        ent.h = hv ? *hv : static_cast<float>(default_h);
    } else {
        ent.w = static_cast<float>(default_w);
        ent.h = static_cast<float>(default_h);
    }
    // z：缺省 0；number 有限且在 int 范围内（向零取整保存，非整数合法）
    if (auto z = e.find("z"); z != e.end()) {
        auto v = detail::json_as_finite_float(*z);
        if (!v) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "z 数值非法")));
        if (*v < static_cast<float>(std::numeric_limits<int>::min()) ||
            *v > static_cast<float>(std::numeric_limits<int>::max()))
            return tl::unexpected(
                err(ErrorCode::kSchemaViolation, at(where, "z 不在 int 范围内")));
        ent.z = static_cast<int>(*v);  // 向零取整（C 版语义）
    }
    // color：缺省白
    if (auto c = e.find("color"); c != e.end()) {
        if (!c->is_string()) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "color 必须为 #rrggbb")));
        auto col = detail::parse_hex_color(c->get_ref<const std::string&>());
        if (!col) return tl::unexpected(
            err(ErrorCode::kSchemaViolation, at(where, "color 非法")));
        ent.color = *col;
    }
    // solid：仅 true 字面量生效，其余一律缺省 false（取值宽容，语义不放宽）
    if (auto s = e.find("solid"); s != e.end() && s->is_boolean()) {
        ent.solid = s->get<bool>();
    }
    // sprite（可选）
    if (auto sp = e.find("sprite"); sp != e.end()) {
        if (auto r = parse_sprite(*sp, where + ".sprite", out, ent.sprite); !r)
            return tl::unexpected(r.error());
    }
    out.entities.push_back(std::move(ent));
    // animations（可选；null 拒绝）；动画集名 = 所属 entity id（plan-10 映射键；
    // ent 已被 move，从 entities 取回 id）
    if (auto a = e.find("animations"); a != e.end()) {
        if (auto r = parse_animations(*a, where + ".animations", out); !r)
            return tl::unexpected(r.error());
        out.anims.back().name = out.entities.back().id;
    }
    return {};
}

}  // namespace

// ════════════════════ 公共加载入口 ════════════════════

// 核心解析：从已解析的 root 构造 SceneAsset（正常路径由 SceneAsset::load 调用）。
// 本类为 SceneAsset 的 friend，因此可访问私有构造器与 Impl；不向公共头泄漏
// nlohmann 类型（friend 仅暴露类名）。
namespace detail {

class SceneLoader {
public:
    using Error = SceneAsset::AssetError;

    // 外层入口：source（文件路径或注入名）进诊断前缀（plan-12 §4.4 接通；
    // 此前 source_path 被 (void) 弃用，schema 错误无来源上下文）。
    static expected<SceneAsset, Error> load(const json& root,
                                            std::string_view source_path) {
        auto r = load_impl(root);
        if (!r) {
            return tl::unexpected(err(
                r.error().code, std::string(source_path) + ": " + r.error().message));
        }
        return r;
    }

private:
    // 核心解析（诊断来自 JSON 内部路径；source 前缀由外层 load 注入）。
    static expected<SceneAsset, Error> load_impl(const json& root) {
    auto impl = std::make_unique<detail::SceneImpl>();
    impl->id = next_asset_id();
    if (impl->id == 0) {
        return tl::unexpected(err(ErrorCode::kResourceExhausted,
                                  "asset_id 已耗尽"));
    }
    // 全局 payload 校验：深度/键数/embedded NUL（root 计 1）
    if (detail::json_max_depth(root, 1, kJsonDepthMax) > kJsonDepthMax)
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  "JSON 嵌套深度超过 kJsonDepthMax"));
    if (auto bad = detail::json_any_object_too_many_keys(root, kPayloadKeysMax))
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  "object 键数超限于 " + *bad));
    if (auto bad = detail::json_any_embedded_nul(root))
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  "文本内嵌 NUL 于 " + *bad));
    // asset 累计字节（紧凑 dump）
    if (root.dump().size() > kAssetPayloadBytesMax) {
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  "资产序列化字节超过 kAssetPayloadBytesMax"));
    }

    // §2.0 root 键策略：根宽容（未知忽略 + warning）
    for (const auto& kv : root.items()) {
        const std::string& k = kv.key();
        if (k != "format" && k != "version" && k != "meta" && k != "tilemap" &&
            k != "entities") {
            TraceLog(LOG_WARNING, "[scene] 忽略未知根键 '%s'", k.c_str());
        }
    }
    if (!root.is_object())
        return tl::unexpected(err(ErrorCode::kSchemaViolation, "root 必须是 object"));
    const auto fmt = root.find("format");
    if (fmt == root.end() || !fmt->is_string() ||
        fmt->get_ref<const std::string&>() != "tro-scene")
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  "format 必须为 tro-scene"));
    const auto ver = root.find("version");
    if (ver == root.end() || !ver->is_number_integer() || ver->get<int>() != 2)
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  "version 必须为 2"));

    // meta（宽容）
    if (auto r = parse_meta(root, *impl); !r) return tl::unexpected(r.error());

    // tilemap 必须存在且为 object
    const auto tm_it = root.find("tilemap");
    if (tm_it == root.end() || !tm_it->is_object())
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  "tilemap 必须存在且为 object"));
    const json& tm = *tm_it;

    // §2.2 模式判定（顺序固定）
    const bool has_tilesets = tm.contains("tilesets");
    const bool has_palette = tm.contains("palette");
    if (has_tilesets && has_palette)
        return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                  "tilesets 与 palette 互斥"));
    // 介面确定模式
    enum class ModeTmp { atlas, palette, bare };
    ModeTmp mode;
    if (has_tilesets) mode = ModeTmp::atlas;
    else if (has_palette) mode = ModeTmp::palette;
    else {
        // bare：layers 缺省或空数组才合法
        const auto ly = tm.find("layers");
        if (ly == tm.end() || (ly->is_array() && ly->empty())) mode = ModeTmp::bare;
        else if (ly->is_array())
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      "无 tilesets/palette 但 layers 非空"));
        else
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      "layers 必须为 array"));
    }
    switch (mode) {
        case ModeTmp::atlas: impl->mode = detail::SceneImpl::Mode::atlas; break;
        case ModeTmp::palette: impl->mode = detail::SceneImpl::Mode::palette; break;
        case ModeTmp::bare: impl->mode = detail::SceneImpl::Mode::bare; break;
    }

    // §2.2.3 尺寸：非 bare 必须同时 int ∈[1,256]；bare 允许缺省 → 0
    const auto tw_it = tm.find("tile_width"), th_it = tm.find("tile_height");
    if (mode != ModeTmp::bare) {
        if (tw_it == tm.end() || th_it == tm.end() || !tw_it->is_number_integer() ||
            !th_it->is_number_integer())
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      "非 bare 必须同时提供 tile_width/height int"));
        const int tw = tw_it->get<int>(), th = th_it->get<int>();
        if (tw < 1 || tw > kTileDimMax || th < 1 || th > kTileDimMax)
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      "tile 尺寸越界"));
        impl->tile_w = tw;
        impl->tile_h = th;
    } else {
        // bare：允许同时缺省 → 0；显式提供则校验（bare + 显式尺寸合法）
        if (tw_it != tm.end() || th_it != tm.end()) {
            if (tw_it == tm.end() || th_it == tm.end() ||
                !tw_it->is_number_integer() || !th_it->is_number_integer())
                return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                          "bare 模式的 tile 尺寸须成对提供"));
            const int tw = tw_it->get<int>(), th = th_it->get<int>();
            if (tw < 1 || tw > kTileDimMax || th < 1 || th > kTileDimMax)
                return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                          "tile 尺寸越界"));
            impl->tile_w = tw;
            impl->tile_h = th;
        }
    }

    // tilesets（atlas）：1..8
    if (has_tilesets) {
        const json& arr = tm["tilesets"];
        if (!arr.is_array() || arr.empty())
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      "tilesets 必须为 1..8 的数组"));
        if (static_cast<int>(arr.size()) > kTilesetMax)
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      "tilesets 超过 kTilesetMax"));
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (auto r = parse_tileset_entry(arr[i], i, impl->tile_w, impl->tile_h,
                                             *impl);
                !r)
                return tl::unexpected(r.error());
        }
    }
    // palette（palette 模式）：1..32；tilesets 已排他
    if (has_palette) {
        const json& arr = tm["palette"];
        if (!arr.is_array())
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      "palette 必须为 array"));
        if (auto r = parse_palette(arr, *impl); !r) return tl::unexpected(r.error());
    }

    // layers（tileset 引用先于层校验）
    if (auto r = parse_tilemap_layers(tm, has_tilesets, *impl); !r)
        return tl::unexpected(r.error());

    // entities（可空数组；bare 纯实体场景）
    if (auto ent_it = root.find("entities"); ent_it != root.end()) {
        if (!ent_it->is_array())
            return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                      "entities 必须为 array"));
        // 每 entity per-payload 字节上限（紧凑 dump 长度）
        for (std::size_t i = 0; i < ent_it->size(); ++i) {
            const json& e = (*ent_it)[i];
            if (e.is_object() && e.dump().size() > kPayloadBytesMax)
                return tl::unexpected(err(ErrorCode::kSchemaViolation,
                                          "entities[" + std::to_string(i) +
                                              "] 序列化字节超过 kPayloadBytesMax"));
            const int dw = impl->tile_w > 0 ? impl->tile_w : 16;
            const int dh = impl->tile_h > 0 ? impl->tile_h : 16;
            if (auto r = parse_entity(e, i, dw, dh, *impl); !r)
                return tl::unexpected(r.error());
        }
    }
    // 动画集视图：anims 已稳定（不再 realloc），建立 AnimationSet 只读视图
    impl->anim_sets.reserve(impl->anims.size());
    for (auto& a : impl->anims) {
        impl->anim_sets.emplace_back();
        impl->anim_sets.back().set_data(a);  // friend：SceneLoader 可访问私有 setter
    }
    return SceneAsset(std::move(impl));
}

};  // class SceneLoader

// 薄转发：从已解析 JSON 构造 SceneAsset（供 SceneAsset::load 与测试 seam 共用；
// SceneLoader 是 friend 才可访问私有构造器，本函数只转发不直接构造）。
expected<SceneAsset, SceneAsset::AssetError> load_scene_asset(
    const nlohmann::json& root, std::string_view source_path) {
    return SceneLoader::load(root, source_path);
}

}  // namespace detail

// 内存加载（plan-12 §4.4）：与 load(path) 同一解析/校验路径，仅文本来源不同。
expected<SceneAsset, SceneAsset::AssetError> SceneAsset::load_json(
    std::string_view text, std::string_view name) {
    auto j_or = parse_json_text(std::string(text), name);
    if (!j_or) return tl::unexpected(j_or.error());
    return detail::load_scene_asset(*j_or, name);
}

// 公共 load：读文件 → parse → detail::load_scene_asset
expected<SceneAsset, SceneAsset::AssetError> SceneAsset::load(std::string_view path) {
    // 外部 scene 路径校验（§2.1）：非空、无 NUL、UTF-8、相对 grammar、长度
    if (path.empty() || path.size() >= static_cast<std::size_t>(kPathMax) ||
        path.find('\0') != std::string_view::npos ||
        !detail::is_valid_utf8(path) || !detail::is_safe_relative_path(path)) {
        return tl::unexpected(err(ErrorCode::kInvalidArgument,
                                  "scene 路径非法（须为相对路径）"));
    }
    auto text_or = read_text_file(std::string(path), "scene");
    if (!text_or) return tl::unexpected(text_or.error());
    auto j_or = parse_json_text(*text_or, std::string(path));
    if (!j_or) return tl::unexpected(j_or.error());
    return detail::load_scene_asset(*j_or, path);
}

// ════════════════════ tile-only 查询（plan-5.2 §4） ════════════════════

namespace {

// 世界坐标（像素）→ 层内 tile 坐标：tile = floor((world - origin) / tile_size)；
// 负坐标同样 floor（如 -0.5 → -1）；越出 [0,width)×[0,height) 返回 nullopt（层外）。
// tile_w/tile_h<=0（bare）→ 无有效网格，返回 nullopt。
std::optional<Vec2> world_to_tile(float wx, float wy, const LayerInfo& li,
                                  int tile_w, int tile_h) {
    if (tile_w <= 0 || tile_h <= 0) return std::nullopt;
    if (!std::isfinite(wx) || !std::isfinite(wy)) return std::nullopt;
    const float tx = std::floor((wx - static_cast<float>(li.origin_x)) /
                                static_cast<float>(tile_w));
    const float ty = std::floor((wy - static_cast<float>(li.origin_y)) /
                                static_cast<float>(tile_h));
    if (tx < 0 || ty < 0 || tx >= static_cast<float>(li.width) ||
        ty >= static_cast<float>(li.height))
        return std::nullopt;  // 层外 = 无数据 = 不阻挡
    return Vec2{tx, ty};
}

}  // namespace

TileQueryResult is_solid_at(const SceneAsset& asset, Vec2 world) {
    if (!std::isfinite(world.x) || !std::isfinite(world.y))
        return TileQueryResult::error;
    const auto& impl = *asset.impl_;
    for (std::size_t li = 0; li < impl.layers.size(); ++li) {
        const LayerInfo& info = impl.layers[li];
        if (!info.solid) continue;
        const auto t = world_to_tile(world.x, world.y, info, impl.tile_w, impl.tile_h);
        if (!t) continue;
        const int v = impl.layer_tiles[li]
            [static_cast<std::size_t>(static_cast<int>(t->y)) *
                 static_cast<std::size_t>(info.width) +
             static_cast<std::size_t>(static_cast<int>(t->x))];
        if (v != -1) return TileQueryResult::solid;  // 短路
    }
    return TileQueryResult::clear;  // 无 solid 层 → clear（排除 error 后）
}

TileQueryResult rect_hits_solid(const SceneAsset& asset, Rect world_rect) {
    if (!std::isfinite(world_rect.x) || !std::isfinite(world_rect.y) ||
        !std::isfinite(world_rect.w) || !std::isfinite(world_rect.h) ||
        world_rect.w <= 0 || world_rect.h <= 0)
        return TileQueryResult::error;
    const auto& impl = *asset.impl_;
    if (impl.tile_w <= 0 || impl.tile_h <= 0) return TileQueryResult::clear;
    const double tw = static_cast<double>(impl.tile_w);
    const double th = static_cast<double>(impl.tile_h);
    const double x0 = world_rect.x, x1 = world_rect.x + world_rect.w;
    const double y0 = world_rect.y, y1 = world_rect.y + world_rect.h;
    for (std::size_t li = 0; li < impl.layers.size(); ++li) {
        const LayerInfo& info = impl.layers[li];
        if (!info.solid) continue;
        // 像素 → tile：floor 下界 / ceil 上界（半开 [x0,x1) 覆盖的 tile 区间；
        // 上界用 ceil 保证右边界像素（如 x1=16.1）纳入 tile1，x1 恰为整边界不越）。
        // 全程 double 计算 + 先做"完全层外"剔除，再 saturating clamp 到
        // [0,width] 范围才转 int —— 「极大但有限」坐标（如 1e38）不得触发
        // float→int 的未定义转换（plan-5.2 §4 范围校验，门禁 M3）。
        const double tx0f = std::floor((x0 - static_cast<double>(info.origin_x)) / tw);
        const double tx1f = std::ceil((x1 - static_cast<double>(info.origin_x)) / tw);
        const double ty0f = std::floor((y0 - static_cast<double>(info.origin_y)) / th);
        const double ty1f = std::ceil((y1 - static_cast<double>(info.origin_y)) / th);
        // 完全在层矩形外（任一轴无交集）→ 该层无命中，跳过
        if (tx1f <= 0.0 || tx0f >= static_cast<double>(info.width) ||
            ty1f <= 0.0 || ty0f >= static_cast<double>(info.height))
            continue;
        const int tx0 = static_cast<int>(std::clamp(tx0f, 0.0,
            static_cast<double>(info.width)));
        const int tx1 = static_cast<int>(std::clamp(tx1f, 0.0,
            static_cast<double>(info.width)));
        const int ty0 = static_cast<int>(std::clamp(ty0f, 0.0,
            static_cast<double>(info.height)));
        const int ty1 = static_cast<int>(std::clamp(ty1f, 0.0,
            static_cast<double>(info.height)));
        for (int ty = ty0; ty < ty1; ++ty) {
            for (int tx = tx0; tx < tx1; ++tx) {
                const int v =
                    impl.layer_tiles[li]
                        [static_cast<std::size_t>(ty) *
                             static_cast<std::size_t>(info.width) +
                         static_cast<std::size_t>(tx)];
                if (v != -1) return TileQueryResult::solid;  // 短路
            }
        }
    }
    return TileQueryResult::clear;
}

TileLookupResult tile_at(const SceneAsset& asset, int layer_index, Vec2 world,
                         int* out_value) {
    if (out_value == nullptr) return TileLookupResult::error;
    if (layer_index < 0 ||
        layer_index >= static_cast<int>(asset.impl_->layers.size()))
        return TileLookupResult::error;
    // 坐标非有限 = 非法参数（区别于合法但层外 → empty)
    if (!std::isfinite(world.x) || !std::isfinite(world.y))
        return TileLookupResult::error;
    const auto& impl = *asset.impl_;
    const LayerInfo& info = impl.layers[static_cast<std::size_t>(layer_index)];
    const auto t = world_to_tile(world.x, world.y, info, impl.tile_w, impl.tile_h);
    if (!t) { *out_value = -1; return TileLookupResult::empty; }
    const int v = impl.layer_tiles[static_cast<std::size_t>(layer_index)]
                      [static_cast<std::size_t>(static_cast<int>(t->y)) *
                           static_cast<std::size_t>(info.width) +
                       static_cast<std::size_t>(static_cast<int>(t->x))];
    if (v == -1) { *out_value = -1; return TileLookupResult::empty; }
    *out_value = v;
    return TileLookupResult::occupied;
}

// ════════════════════ 测试 seam（仅 TROGUE_TEST_SEAMS） ════════════════════

#ifdef TROGUE_TEST_SEAMS

namespace detail {

void asset_test_seed_id(std::uint64_t v) { g_next_asset_id = v; }
std::uint64_t asset_test_current_id() { return g_next_asset_id; }
void asset_test_reset_id() { g_next_asset_id = 1; }

}  // namespace detail

#endif  // TROGUE_TEST_SEAMS

}  // namespace tg