#pragma once
// scene_make.hpp —— 内存场景构造（模板自有文件，不属于引擎；**不 include raylib**）。
//
// 为什么单独一个头：本文件只把「ASCII 网格 + 配色」变成 `tg::SceneAsset`
//（走引擎的类型化写路径 `tg::SceneSpec` → `SceneAsset::create`），不需要窗口/GL，
// 因此纯逻辑层（sim.*，可在无窗口进程与 ctest 里跑）与窗口层（main.cpp）能共用
// 同一份场景构造，而不必各写一份同构的 JSON 拼装。
//
// 用法：`hp::make_ascii_scene(rows, tile, floor, wall, bg, "name")`
//   rows = ASCII 网格，'#' = 墙（solid 层），其余字符 = 地面。
//   产出 palette 模式场景：层 0 "ground"（非 solid）、层 1 "walls"（solid，
//   '#' 处 tiles=1）——与其他范例、与模板约定一致（solid 层索引恒为 1）。

#include <algorithm>  // std::max（取网格最大宽度）
#include <cstdio>     // std::snprintf / std::fprintf
#include <cstdlib>    // std::abort
#include <string>
#include <vector>

#include "trogue/trogue.hpp"

namespace hp {

inline std::string to_hex(tg::Color c) {
    char buf[10];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}

// ASCII 网格 → tg::SceneAsset（内存构造，零资产文件：不打开 Godot 也能开发）。
// 走引擎的类型化写路径（SceneSpec → create），不再手拼 tro-scene JSON。
// 构造失败直接 abort：这些是实例代码里的编译期常量，失败即程序错误。
inline tg::SceneAsset make_ascii_scene(const std::vector<std::string>& rows,
                                      int tile_size, tg::Color floor_color,
                                      tg::Color wall_color, tg::Color background,
                                      const char* name) {
    const int height = static_cast<int>(rows.size());
    int width = 0;
    for (const auto& r : rows) width = std::max<int>(width, static_cast<int>(r.size()));

    std::vector<int> ground_tiles, wall_tiles;
    ground_tiles.reserve(static_cast<std::size_t>(width) * height);
    wall_tiles.reserve(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const char c = (x < static_cast<int>(rows[y].size())) ? rows[y][x] : '.';
            const bool solid = (c == '#');
            ground_tiles.push_back(solid ? -1 : 0);
            wall_tiles.push_back(solid ? 1 : -1);
        }
    }

    tg::SceneSpec spec;
    spec.tile_width = tile_size;
    spec.tile_height = tile_size;
    spec.palette = {floor_color, wall_color};
    tg::SceneLayerSpec ground;
    ground.name = "ground";
    ground.width = width;
    ground.height = height;
    ground.tiles = std::move(ground_tiles);
    tg::SceneLayerSpec walls;
    walls.name = "walls";
    walls.solid = true;
    walls.width = width;
    walls.height = height;
    walls.tiles = std::move(wall_tiles);
    spec.layers = {std::move(ground), std::move(walls)};
    spec.name = name;
    spec.has_background = true;
    spec.background = to_hex(background);

    auto loaded = tg::SceneAsset::create(spec, name);
    if (!loaded) {
        std::fprintf(stderr, "[scene] 内存场景构造失败: %s\n",
                     loaded.error().message.c_str());
        std::abort();
    }
    return std::move(*loaded);
}

}  // namespace hp
