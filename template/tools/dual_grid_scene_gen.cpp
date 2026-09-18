// dual_grid_scene_gen.cpp —— vertex grid → tro-scene v2。
// PixelLab tileset15 的四角语义直接采样，不使用 cell-terrain 多数投票。

#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>
#include "trogue/scene.hpp"
#include "trogue/terrain.hpp"

namespace {
using json = nlohmann::json;

std::string read_text(const char* path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error(std::string("无法打开 ") + path);
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

int value_at(const json& grid, int y, int x, int width, int height) {
    if (x < 0 || y < 0 || x >= width || y >= height) return -1;
    return grid.at(y).at(x).get<int>();
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "用法: trogue_dual_grid_scene_gen <spec.json> <out.json>\n");
        return 2;
    }
    try {
        const json spec = json::parse(read_text(argv[1]));
        if (!spec.is_object() || !spec.contains("tileset") ||
            !spec["tileset"].is_string() || !spec.contains("vertex_grid") ||
            !spec["vertex_grid"].is_array() || spec["vertex_grid"].size() < 2) {
            throw std::runtime_error("spec 须含 string tileset 与 vertex_grid");
        }
        const std::string tileset = spec["tileset"].get<std::string>();
        if (spec.contains("name") && !spec["name"].is_string())
            throw std::runtime_error("name 必须为 string");
        for (const char* key : {"tile_width", "tile_height"}) {
            if (spec.contains(key) && (!spec[key].is_number_integer() || spec[key].get<int>() <= 0))
                throw std::runtime_error(std::string(key) + " 必须为正整数");
        }
        const json& grid = spec["vertex_grid"];
        const int vh = static_cast<int>(grid.size());
        if (!grid[0].is_array() || grid[0].size() < 2) {
            throw std::runtime_error("vertex_grid 每行须为至少 2 项数组");
        }
        const int vw = static_cast<int>(grid[0].size());
        for (const auto& row : grid) {
            if (!row.is_array() || static_cast<int>(row.size()) != vw)
                throw std::runtime_error("vertex_grid 必须为等宽数组");
            for (const auto& value : row) {
                if (!value.is_number_integer() || value.get<int>() < 0 || value.get<int>() > 1)
                    throw std::runtime_error("vertex_grid 只允许 terrain 0/1");
            }
        }

        auto table = tg::load_dual_grid_table(tileset);
        if (!table) throw std::runtime_error(table.error().diagnostics());
        const int cw = vw - 1;
        const int ch = vh - 1;
        json tile_values = json::array();
        for (int y = 0; y < ch; ++y) {
            for (int x = 0; x < cw; ++x) {
                const std::array<int, 4> corners = {
                    value_at(grid, y, x, vw, vh),
                    value_at(grid, y, x + 1, vw, vh),
                    value_at(grid, y + 1, x, vw, vh),
                    value_at(grid, y + 1, x + 1, vw, vh),
                };
                auto tile = tg::pick_dual_grid_tile(*table, corners);
                if (!tile) {
                    throw std::runtime_error("缺少四角组合 at (" + std::to_string(x) + "," +
                                             std::to_string(y) + "): " + tile.error().message);
                }
                tile_values.push_back(*tile);
            }
        }

        json scene = {
            {"format", "tro-scene"}, {"version", 2},
            {"meta", { {"name", spec.value("name", "dual_grid_scene")} }},
            {"tilemap", {
                {"tile_width", spec.value("tile_width", 16)},
                {"tile_height", spec.value("tile_height", 16)},
                {"tilesets", json::array({{{"name", "terrain"}, {"path", tileset}}})},
                {"layers", json::array({{{"name", "visual"}, {"width", cw},
                    {"height", ch}, {"solid", false}, {"tileset", "terrain"},
                    {"tiles", tile_values}}})}
            }},
            {"entities", json::array()}
        };
        auto asset = tg::SceneAsset::load_json(scene.dump(), argv[2]);
        if (!asset) throw std::runtime_error(asset.error().diagnostics());
        std::ofstream output(argv[2]);
        if (!output) throw std::runtime_error(std::string("无法写入 ") + argv[2]);
        output << scene.dump(1) << '\n';
        std::printf("dual_grid_scene_gen: %s (%dx%d, 采样 %d/%d)\n",
                    argv[2], cw, ch, cw * ch, cw * ch);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "dual_grid_scene_gen: %s\n", e.what());
        return 1;
    }
}
