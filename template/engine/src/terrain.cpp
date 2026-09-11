// terrain.cpp —— TerrainTable 加载与 pick_tile 选择器实现。
//
// 加载复用 tro-tileset 共享解析核心（tileset_parse.hpp，与场景侧同一套校验）；
// 选择器为无状态纯函数——同一输入永远同一输出（热重载/重放无视觉抽签）。
#include "trogue/terrain.hpp"

#include <limits>
#include <string>
#include <utility>

#include "trogue/config.hpp"
#include "tileset_parse.hpp"
#include "util/json_check.hpp"   // is_valid_utf8
#include "util/path_check.hpp"   // is_safe_relative_path

namespace tg {

namespace {

Error err(ErrorCode code, std::string msg) { return Error{code, std::move(msg)}; }

}  // namespace

expected<TerrainTable, Error> load_terrain_table(std::string_view path) {
    // 路径校验与 SceneAsset::load 同语义（相对路径 grammar）
    if (path.empty() || path.size() >= static_cast<std::size_t>(kPathMax) ||
        path.find('\0') != std::string_view::npos ||
        !detail::is_valid_utf8(path) || !detail::is_safe_relative_path(path)) {
        return tl::unexpected(
            err(ErrorCode::kInvalidArgument, "tileset 路径非法（须为相对路径）"));
    }
    auto doc = detail::load_tileset_document(path);
    if (!doc) return tl::unexpected(doc.error());
    // v1 限单 terrain_set：场景侧多 set 合法，但匹配表 v1 只收单 set
    if (doc->terrain_sets.size() != 1) {
        return tl::unexpected(err(
            ErrorCode::kSchemaViolation,
            std::string(path) + ": v1 限单 terrain_set（实际 " +
                std::to_string(doc->terrain_sets.size()) + " 组）"));
    }
    TerrainTable table;
    table.set = doc->terrain_sets.front();
    table.tiles = std::move(doc->tile_terrains);
    return table;
}

expected<int, Error> pick_tile(const TerrainTable& table, int terrain_set,
                               int terrain,
                               const std::array<int, kTerrainBitCount>& pattern) {
    // v1 单 set：入参仅接受 0（参数保留作多 set 前向兼容）
    if (terrain_set != 0) {
        return tl::unexpected(
            err(ErrorCode::kInvalidArgument, "terrain_set 仅接受 0（v1 单 terrain_set）"));
    }
    if (terrain < 0 || terrain >= table.set.terrain_count) {
        return tl::unexpected(err(ErrorCode::kInvalidArgument, "terrain 越界"));
    }
    // pattern 校验：mode 合法位 ∈ {-1} ∪ [0, terrain_count)；非法位必须 -1
    //（静默忽略违反「明确损失」原则——game 误传 corner 值到 sides tileset 必须报错）
    for (int b = 0; b < kTerrainBitCount; ++b) {
        const auto bit = static_cast<TerrainBit>(b);
        const int v = pattern[static_cast<std::size_t>(b)];
        if (!terrain_bit_valid(table.set.mode, bit)) {
            if (v != -1) {
                return tl::unexpected(err(ErrorCode::kInvalidArgument,
                                          "pattern 含该 mode 非法方向位"));
            }
        } else if (v != -1 && (v < 0 || v >= table.set.terrain_count)) {
            return tl::unexpected(
                err(ErrorCode::kInvalidArgument, "pattern 方向位 terrain 序号越界"));
        }
    }
    // 评分：Σ_{合法位}[tile_bit != pattern_bit]；严格 < 使同分
    // 保留先见者 = 最小 tile id（tiles 下标即 id，向量序确定性）
    int best_id = -1;
    int best_score = std::numeric_limits<int>::max();
    for (int id = 0; id < static_cast<int>(table.tiles.size()); ++id) {
        const auto& e = table.tiles[static_cast<std::size_t>(id)];
        if (e.terrain_set != 0 || e.terrain != terrain) continue;
        int score = 0;
        for (int b = 0; b < kTerrainBitCount; ++b) {
            if (!terrain_bit_valid(table.set.mode, static_cast<TerrainBit>(b))) continue;
            if (e.bits[static_cast<std::size_t>(b)] !=
                pattern[static_cast<std::size_t>(b)]) {
                ++score;
            }
        }
        if (score < best_score) {
            best_score = score;
            best_id = id;
        }
    }
    if (best_id < 0) {
        return tl::unexpected(err(ErrorCode::kNotFound, "该 terrain 没有任何候选 tile"));
    }
    return best_id;
}

}  // namespace tg
