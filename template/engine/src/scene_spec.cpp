// scene_spec.cpp —— SceneSpec（构造描述）→ tro-scene JSON 的序列化实现。
//
// 分工：本文件只做「结构 → JSON 文本」，**不做任何 schema 校验**——限额、三态组合、
// 引用存在性、实体 id 唯一性、tiles 长度与值域等全部由 load_json 判定一次。这样
// SceneSpec 只是 schema 的构造子集镜像，规则不会在 C++ 侧出现第二份实现。
//
// 本层只判两类无法委托给 JSON 校验的失败：
//   ① 非有限数值（NaN/Inf）：nlohmann 序列化时会静默写成 null，等于悄悄改写数据；
//   ② 非法 UTF-8 字符串：dump() 会抛 json::exception，不能让它跨公共 API 抛出。
// 文本路径本身（load_json）对这些输入是宽容/可报错的，故这两类是新引入的失败面。

#include "trogue/scene.hpp"

#include <cmath>    // std::isfinite
#include <cstdio>   // std::snprintf
#include <string>
#include <utility>

namespace tg {

namespace {

// Color → `#rrggbb`（不透明）/ `#rrggbbaa`（带 alpha）；与解析端对偶。
std::string color_hex(const Color& c) {
    char buf[10];
    if (c.a == 255)
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
    else
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
    return std::string(buf);
}

// 递归检查 JSON 树里是否含非有限浮点（对象键与数组元素都要看）。
bool has_nonfinite(const nlohmann::json& j) {
    switch (j.type()) {
        case nlohmann::json::value_t::number_float:
            return !std::isfinite(j.get<double>());
        case nlohmann::json::value_t::object:
            for (auto it = j.begin(); it != j.end(); ++it)
                if (has_nonfinite(it.value())) return true;
            return false;
        case nlohmann::json::value_t::array:
            for (const auto& e : j) {
                if (has_nonfinite(e)) return true;
            }
            return false;
        default:
            return false;  // 其余类型不可能承载非有限值
    }
}

// 只省略「等于 schema 缺省」的字段：其余（含看似非法的值）一律照写，交给 load_json 判。
bool is_default_props(const nlohmann::json& props) {
    return props.is_object() && props.empty();
}

nlohmann::json build_document(const SceneSpec& spec) {
    // bare = 无 palette、无 tilesets 且无图层：此时不写 tile 尺寸（既有契约规定
    // bare 的 tile_width()/tile_height() 返回 0）。palette/图集模式下 layers 可为空，
    // 那种情况不是 bare，仍须写出尺寸（否则会被 load_json 拒）。
    const bool bare = spec.palette.empty() && spec.tilesets.empty() &&
                      spec.layers.empty();

    nlohmann::json tilemap = nlohmann::json::object();
    if (!bare) {
        tilemap["tile_width"] = spec.tile_width;
        tilemap["tile_height"] = spec.tile_height;
    }
    if (!spec.palette.empty()) {
        nlohmann::json palette = nlohmann::json::array();
        for (const Color& c : spec.palette) palette.push_back(color_hex(c));
        tilemap["palette"] = std::move(palette);
    }
    if (!spec.tilesets.empty()) {
        nlohmann::json tilesets = nlohmann::json::array();
        for (const TilesetRef& ts : spec.tilesets) {
            nlohmann::json o = nlohmann::json::object();
            o["name"] = ts.name;
            o["path"] = ts.path;
            tilesets.push_back(std::move(o));
        }
        tilemap["tilesets"] = std::move(tilesets);
    }
    nlohmann::json layers = nlohmann::json::array();
    for (const SceneLayerSpec& l : spec.layers) {
        nlohmann::json o = nlohmann::json::object();
        o["name"] = l.name;
        o["width"] = l.width;
        o["height"] = l.height;
        o["solid"] = l.solid;
        if (l.origin_x != 0 || l.origin_y != 0)
            o["origin"] = nlohmann::json::array({l.origin_x, l.origin_y});
        if (!l.tileset.empty()) o["tileset"] = l.tileset;  // palette 模式不得携带
        o["tiles"] = l.tiles;
        layers.push_back(std::move(o));
    }
    tilemap["layers"] = std::move(layers);

    nlohmann::json meta = nlohmann::json::object();  // 恒写（缺省即空 object）
    if (!spec.name.empty()) meta["name"] = spec.name;
    if (spec.has_background) meta["background"] = spec.background;
    if (!is_default_props(spec.props)) meta["props"] = spec.props;

    nlohmann::json doc = nlohmann::json::object();
    doc["format"] = "tro-scene";
    doc["version"] = 2;
    doc["meta"] = std::move(meta);
    doc["tilemap"] = std::move(tilemap);
    doc["entities"] = spec.entities;  // 恒写；原样携带（不镜像、不改造）
    return doc;
}

}  // namespace

ErrorOr<std::string> scene_spec_to_json(const SceneSpec& spec) {
    if (has_nonfinite(spec.props) || has_nonfinite(spec.entities)) {
        return tl::make_unexpected(Error{
            ErrorCode::kInvalidArgument,
            "scene_spec_to_json: props/entities 含非有限数值（JSON 无法表达，"
            "序列化会静默变成 null）"});
    }
    try {
        return build_document(spec).dump();
    } catch (const nlohmann::json::exception& e) {
        // 典型是非法 UTF-8（type_error.316）：收成错误码，不跨公共 API 抛异常
        return tl::make_unexpected(Error{
            ErrorCode::kInvalidArgument,
            std::string("scene_spec_to_json: 序列化失败: ") + e.what()});
    }
}

ErrorOr<SceneAsset> SceneAsset::create(const SceneSpec& spec,
                                       std::string_view name) {
    auto text = scene_spec_to_json(spec);
    if (!text) return tl::make_unexpected(std::move(text).error());
    return load_json(*text, name);  // 校验与建资产全在既有单一路径里
}

}  // namespace tg
