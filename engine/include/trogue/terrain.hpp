#pragma once
// terrain.hpp —— tro-tileset terrain 匹配表（TerrainTable）与 tile 选择器
// （pick_tile，plan-12）。
//
// 边界（复刻 animation.hpp 句式）：engine 只做机制采样——按 tileset 已标注的
// peering_bits 规则把「地形 pattern」映射为 tile id（无状态纯函数）；地形指派、
// 程序生成、触发归 game。engine 不保存地形状态、不做扩散式重排。
//
// 匹配语义对齐 Godot（查证 reference/godot-4.7.2-stable）：
//   - 评分 = Σ_{合法方向位}[tile_bit != pattern_bit]，取最小分（Godot
//     tile_map_layer.cpp _get_best_terrain_pattern_for_constraints 的单格无状态
//     简化：约束全给出、priority 恒 1、无扩散/current 保持项）
//   - 未标注位 = -1 参与比较；对只标部分方向的 tileset，未标注位对全体候选的
//     贡献均匀，排序只由已标注位驱动
//   - 同分取最小 tile id（Godot 在 tile 层随机取一，我们确定性化——热重载/重放
//     无视觉抽签）

#include <array>
#include <string_view>
#include <vector>

#include "trogue/types.hpp"
#include <tl/expected.hpp>

namespace tg {

// terrain set 匹配模式（对齐 Godot TileSet.TerrainMode / tro-tileset mode 串：
// sides→sides、corners→corners、corners_and_sides→corners_and_sides）
enum class TerrainMode { sides, corners, corners_and_sides };

// 8 方向位序（bits 数组下标），与 Godot CELL_NEIGHBOR_ENUM_TO_TEXT 的 square
// 合法名一一对应（tro-tileset peering_bits 键名同）：
//   top_side  top_right_corner  right_side  bottom_right_corner
//   bottom_side  bottom_left_corner  left_side  top_left_corner
enum class TerrainBit : int {
    top_side = 0,
    top_right_corner,
    right_side,
    bottom_right_corner,
    bottom_side,
    bottom_left_corner,
    left_side,
    top_left_corner,
};
inline constexpr int kTerrainBitCount = 8;

// 该 mode 下参与匹配的方向位（sides=4 边 / corners=4 角 / corners_and_sides=8）。
// 与 Godot is_valid_terrain_peering_bit_for_mode 语义一致（tile_set.h:483）。
inline bool terrain_bit_valid(TerrainMode mode, TerrainBit bit) noexcept {
    const bool is_corner =
        bit == TerrainBit::top_left_corner || bit == TerrainBit::top_right_corner ||
        bit == TerrainBit::bottom_right_corner || bit == TerrainBit::bottom_left_corner;
    switch (mode) {
        case TerrainMode::sides: return !is_corner;
        case TerrainMode::corners: return is_corner;
        case TerrainMode::corners_and_sides: return true;
    }
    return false;
}

// 单个 terrain set 元数据（v1 单 set；name/color 不入表——匹配只关心序号）
struct TerrainSetInfo {
    TerrainMode mode = TerrainMode::sides;
    int terrain_count = 0;
};

// 单个 tile 的 terrain 归属与邻位标注（bits 值 = terrain 序号，-1 = 未标注）
struct TerrainTileEntry {
    int terrain_set = -1;
    int terrain = -1;
    std::array<int, kTerrainBitCount> bits = [] {
        std::array<int, kTerrainBitCount> b{};
        b.fill(-1);
        return b;
    }();
};

// tro-tileset 的纯逻辑匹配表（不含纹理/区域字段——选择器只产 id，渲染走场景
// asset）。可复制纯值类型。v1 限单 terrain_set（load_terrain_table 保证）。
struct TerrainTable {
    TerrainSetInfo set;
    std::vector<TerrainTileEntry> tiles;  // 下标即 tile id
};

// 从 tro-tileset 文件加载匹配表（path 相对 assets/，与场景同约定）。
// v1 限制：terrain_sets 必须恰好 1 组（缺失/空/≥2 组 → kSchemaViolation）。
expected<TerrainTable, Error> load_terrain_table(std::string_view path);

// 选择器：按 table 已标注 bits 规则，把 pattern 映射为 tile id。
// - pattern 与 bits 同序同义（值 = 期望 terrain 序号或 -1 无该方向邻居）
// - terrain_set 仅接受 0（v1 单 set，参数保留作多 set 前向兼容）
// - 校验失败 → kInvalidArgument（terrain/pattern 序号越界、非法方向位非 -1）
// - 该 terrain 无任何候选 tile → kNotFound
expected<int, Error> pick_tile(const TerrainTable& table, int terrain_set,
                               int terrain,
                               const std::array<int, kTerrainBitCount>& pattern);

}  // namespace tg
