#pragma once
// scene_make.hpp —— 内存场景构造（模板自有文件，不属于引擎；**不 include raylib**）。
//
// 为什么单独一个头：本文件只产出 tro-scene JSON 并交给引擎解析，不需要窗口/GL，
// 因此纯逻辑层（sim.*，可在无窗口进程与 ctest 里跑）与窗口层（main.cpp）能共用
// 同一份场景构造，而不必各写一份同构的 JSON 拼装。
//
// 用法：`hp::make_ascii_scene(rows, tile, floor, wall, bg, "name")`
//   rows = ASCII 网格，'#' = 墙（solid 层），其余字符 = 地面。
//   产出 palette 模式 tro-scene：层 0 "ground"（非 solid）、层 1 "walls"（solid，
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
// 构造失败直接 abort：这些是实例代码里的编译期常量，失败即程序错误。
inline tg::SceneAsset make_ascii_scene(const std::vector<std::string>& rows,
                                      int tile_size, tg::Color floor_color,
                                      tg::Color wall_color, tg::Color background,
                                      const char* name) {
    const int height = static_cast<int>(rows.size());
    int width = 0;
    for (const auto& r : rows) width = std::max<int>(width, static_cast<int>(r.size()));

    tg::Json ground = tg::Json::array();
    tg::Json walls = tg::Json::array();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const char c = (x < static_cast<int>(rows[y].size())) ? rows[y][x] : '.';
            const bool solid = (c == '#');
            ground.push_back(solid ? -1 : 0);
            walls.push_back(solid ? 1 : -1);
        }
    }
    auto make_layer = [&](const char* lname, bool solid, const tg::Json& tiles) {
        tg::Json l;
        l["name"] = lname;
        l["width"] = width;
        l["height"] = height;
        l["solid"] = solid;
        l["tiles"] = tiles;
        return l;
    };
    tg::Json layers = tg::Json::array();
    layers.push_back(make_layer("ground", false, ground));
    layers.push_back(make_layer("walls", true, walls));

    tg::Json tilemap;
    tilemap["tile_width"] = tile_size;
    tilemap["tile_height"] = tile_size;
    tilemap["palette"] = tg::Json::array({to_hex(floor_color), to_hex(wall_color)});
    tilemap["layers"] = layers;

    tg::Json meta;
    meta["name"] = name;
    meta["background"] = to_hex(background);

    tg::Json scene;
    scene["format"] = "tro-scene";
    scene["version"] = 2;
    scene["meta"] = meta;
    scene["tilemap"] = tilemap;
    scene["entities"] = tg::Json::array();

    const std::string text = scene.dump();
    auto loaded = tg::SceneAsset::load_json(text, name);
    if (!loaded) {
        std::fprintf(stderr, "[scene] 内存场景构造失败: %s\n",
                     loaded.error().message.c_str());
        std::abort();
    }
    return std::move(*loaded);
}

}  // namespace hp
