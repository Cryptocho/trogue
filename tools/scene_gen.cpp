// scene_gen.cpp —— 离线场景生成 CLI（plan-13 §5.1，数据流 C 的机制半）。
//
// 输入：tro-tileset JSON（assets 相对路径）+ 顶点 pattern JSON（stdin 或文件，
// 由 pixellab/scene.py 按 mapping.vertex_corners 产出）。
// 输出：tro-scene v2 JSON（tiles 烤死）——落盘前用 tg::SceneAsset::load_json
// 回读自检（可校验原则，plan-13 §5.1）。
//
// 顶点 pattern JSON 格式：
//   {"tileset": "tilesets/pixellab/x.json", "w": W, "h": H,
//    "patterns": [{"tl":0,"tr":1,"br":1,"bl":0,"terrain":1}, ...]  // 行主序 W*H
//   }
// 机制采样归 engine（pick_tile）；地形指派/顶点采样归调用方（pixellab/scene.py）。
//
// 用法：scene_gen <pattern_json> <out_scene_json> [--name <场景名>]
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include "trogue/trogue.hpp"

namespace {
using json = nlohmann::json;

std::string read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "scene_gen: 无法打开 %s\n", path);
        std::exit(2);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "用法: scene_gen <pattern_json> <out_scene_json> [--name <n>]\n"
                     "  (w/h ≤ 256)\n");
        return 2;
    }
    const char* out_path = argv[2];
    std::string name = "pixellab_map";
    for (int i = 3; i < argc; i += 2) {
        if (i + 1 >= argc) {
            std::fprintf(stderr, "scene_gen: 参数 %s 缺值\n", argv[i]);
            return 2;
        }
        if (std::string(argv[i]) == "--name") name = argv[i + 1];
        else {
            std::fprintf(stderr, "scene_gen: 未知参数 %s\n", argv[i]);
            return 2;
        }
    }

    const json pat = json::parse(read_file(argv[1]));
    const std::string tileset_path = pat.at("tileset").get<std::string>();
    const int w = pat.at("w").get<int>();
    const int h = pat.at("h").get<int>();
    const auto& patterns = pat.at("patterns");
    if (!patterns.is_array() ||
        static_cast<int>(patterns.size()) != w * h) {
        std::fprintf(stderr, "scene_gen: patterns 长度 %zu != w*h = %d\n",
                     patterns.size(), w * h);
        return 2;
    }
    if (w < 1 || h < 1 || w > 256 || h > 256) {
        std::fprintf(stderr, "scene_gen: w/h 越界 (1..256)\n");
        return 2;
    }

    // 机制采样：引擎匹配表 + 选择器（plan-12 边界——采样归 engine）
    auto table = tg::load_terrain_table(tileset_path);
    if (!table) {
        std::fprintf(stderr, "scene_gen: terrain table 加载失败: %s\n",
                     table.error().message.c_str());
        return 1;
    }
    // tile 尺寸从 tileset 元数据读（场景 tile_width/height 必须一致）
    const json ts_doc = json::parse(read_file(("assets/" + tileset_path).c_str()));
    const int tw = ts_doc.at("tile_width").get<int>();
    const int th = ts_doc.at("tile_height").get<int>();

    json tiles = json::array();
    for (const auto& p : patterns) {
        std::array<int, 8> bits{};
        bits.fill(-1);
        using B = tg::TerrainBit;
        bits[static_cast<std::size_t>(B::top_left_corner)] = p.at("tl").get<int>();
        bits[static_cast<std::size_t>(B::top_right_corner)] = p.at("tr").get<int>();
        bits[static_cast<std::size_t>(B::bottom_right_corner)] = p.at("br").get<int>();
        bits[static_cast<std::size_t>(B::bottom_left_corner)] = p.at("bl").get<int>();
        auto id = tg::pick_tile(*table, 0, p.at("terrain").get<int>(), bits);
        tiles.push_back(id.value_or(-1));  // 采样失败兜底空格
    }

    json tileset_ref = {{"name", "terrain"}, {"path", tileset_path}};
    json layer = {{"name", "ground"},
                  {"width", w},
                  {"height", h},
                  {"solid", false},
                  {"tileset", "terrain"},
                  {"tiles", tiles}};
    json scene = {
        {"format", "tro-scene"},
        {"version", 2},
        {"meta", {{"name", name}, {"background", "#101018"}}},
        {"tilemap", {{"tile_width", tw},
                     {"tile_height", th},
                     {"tilesets", json::array({tileset_ref})},
                     {"layers", json::array({layer})}}},
        {"entities", json::array()},
    };

    // 回读自检（可校验原则）：同一解析/校验路径，失败不落盘
    auto loaded = tg::SceneAsset::load_json(scene.dump(), name);
    if (!loaded) {
        std::fprintf(stderr, "scene_gen: 场景自检失败: %s\n",
                     loaded.error().message.c_str());
        return 1;
    }
    std::ofstream out(out_path, std::ios::binary);
    out << scene.dump(1) << "\n";
    if (!out) {
        std::fprintf(stderr, "scene_gen: 写 %s 失败\n", out_path);
        return 1;
    }
    std::printf("scene_gen: %s (%dx%d, tileset %s)\n", out_path, w, h,
                tileset_path.c_str());
    return 0;
}
