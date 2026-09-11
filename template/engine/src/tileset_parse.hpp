#pragma once
// tileset_parse.hpp —— tro-tileset 文档共享解析核心（plan-12 §4.1）。
// 场景 tileset 元数据（scene_asset.cpp load_tileset_meta）与 TerrainTable 加载
//（terrain.cpp load_terrain_table）共用同一解析实现，杜绝两套解析漂移。
// 本头仅 engine 内部可达（src/ 私有），不进公共面。

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <tl/expected.hpp>

#include "scene_impl.hpp"

namespace tg::detail {

// tro-tileset 文档的共享解析产物：视觉字段 + terrain 字段（两条路径各取所需）。
struct TilesetParsed {
    int tile_w = 0, tile_h = 0;
    int tile_count = 0;
    std::string texture;  // assets-relative
    int columns = 0;
    std::vector<SceneImpl::TilesetMeta::TileVisual> tile_visuals;
    std::vector<TerrainSetInfo> terrain_sets;     // 空 = 无 terrain 数据
    std::vector<TerrainTileEntry> tile_terrains;  // 与 tile_visuals 对齐（下标即 id）
};

// 解析 tro-tileset 文档：format/version/尺寸/texture/tiles/columns/terrain 全量
// 校验（plan-12 §4.1——terrain 字段此前宽容路过，本期起解析 + 校验）。source 仅
// 用于错误消息上下文（引用路径或注入名）。不读纹理文件；「与场景 tile 尺寸一致」
// 检查是场景侧调用方职责（TerrainTable 无场景上下文）。
expected<TilesetParsed, Error> parse_tileset_document(const nlohmann::json& ts,
                                                      std::string_view source);

// 读文件 + 解析 JSON + parse_tileset_document（rel_path 相对 assets/，与场景同
// 约定；读文件/解析错误的诊断与场景侧 tileset 引用一致）。
expected<TilesetParsed, Error> load_tileset_document(std::string_view rel_path);

}  // namespace tg::detail
