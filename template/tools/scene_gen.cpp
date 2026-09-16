// scene_gen.cpp —— 离线占位地图生成 CLI。
//
// 输入：地图 spec JSON（CWD = 项目根，tileset 路径相对 assets/）：
//   {
//     "name": "placeholder_map",                 // 可选；缺省不写 meta.name（--name 优先）
//     "tileset": "tilesets/placeholder.json",    // 必填：corners 模式、≥2 地形
//     "background": "#101018",                   // 可选（meta.background）
//     "grid": ["....####", "....#..#"],          // 必填：等宽行，'.'=terrain 0 / '#'=terrain 1
//     "entities": [ ... ]                        // 可选：原样透传进 tro-scene.entities
//   }
// 输出：tro-scene v2，两层——"ground"（非 solid，terrain 0 格）与
//   "walls"（solid，terrain 1 格）；输出父目录不存在时自动创建；**先**经引擎的
//   tg::SceneAsset::create（SceneSpec 值类型 → 资产，校验与 load_json 同一条路径）
//   自检、**再**落盘，故任何失败都不会留下半成品文件。
//
// 失败语义（不做半成品）：spec/网格/字段类型不合法 → 退出码 2，未做任何写入；
//   tileset 加载或校验失败、场景自检失败、**任一格 pick_tile 取不到 tile** →
//   退出码 1 且不落盘。取不到 tile 意味着 tileset 对应地形池缺该角组合，产物会
//   在 solid 层留下空洞（地图出现无碰撞缺口），故宁可失败也不静默填空。
//
// 分工：terrain 指派与顶点采样（4 邻格多数投票，平分取自身优先序）归本工具；
//   bits→tile id 归 engine 的 tg::pick_tile（按 tileset 已标注的 peering_bits
//   匹配，同 pattern 恒同 id）。每格只在**自身 terrain 池**内取 tile，故需要
//   tileset 每个地形池都覆盖全部 4 角组合，否则 pick_tile 会按最近匹配降级
//   （tools/placeholder_tileset.py 生成的占位集即按此构造）。
//
// 用法：scene_gen <spec_json> <out_scene_json> [--name <场景名>]
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include "trogue/trogue.hpp"

namespace {

using json = nlohmann::json;

constexpr int kDimMax = 256;  // 生成上限（JSON 体积约束；引擎层上限更高）
// 本工具的地形数 = 占位集的 2 地形（'.' → 0 / '#' → 1）；层名与 solid 归属
// 也按此展开（见 layer_names / solid 字段）。
constexpr int kTerrainCount = 2;

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

// 顶点值 = 四邻格（含自身）多数投票；2/2 平分取优先序首个有效格
//（self, top, left, top_left —— 越界邻格以 -1 表示、不计入）。
int vote(std::initializer_list<int> cells) {
    int up = 0, lo = 0;
    for (int v : cells) {
        if (v == 1) ++up;
        else if (v == 0) ++lo;
    }
    if (up > lo) return 1;
    if (lo > up) return 0;
    for (int v : cells)
        if (v == 0 || v == 1) return v;
    return 0;  // 不可达：自身恒有效
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "用法: scene_gen <spec_json> <out_scene_json> [--name <n>]\n"
                     "  spec: {\"tileset\": <assets 相对路径>, \"grid\": [\"..#\", ...],\n"
                     "         \"name\"?: str, \"background\"?: \"#rrggbb\", \"entities\"?: []}\n");
        return 2;
    }
    std::string name;
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

    json spec;
    try {
        spec = json::parse(read_file(argv[1]));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "scene_gen: spec 解析失败: %s\n", e.what());
        return 2;
    }
    if (!spec.is_object() || !spec.contains("tileset") ||
        !spec["tileset"].is_string() || !spec.contains("grid") ||
        !spec["grid"].is_array() || spec["grid"].empty()) {
        std::fprintf(stderr, "scene_gen: spec 须含 string tileset 与非空 grid 数组\n");
        return 2;
    }
    const std::string tileset_path = spec["tileset"].get<std::string>();
    // spec 字段类型严格校验：存在即须为声明类型（宽严一致，避免非法值被静默丢弃）
    for (const auto& f : {std::pair<const char*, const char*>{"name", "string"},
                          {"background", "string"},
                          {"entities", "array"}}) {
        if (!spec.contains(f.first)) continue;
        const bool ok = std::string(f.second) == "string" ? spec[f.first].is_string()
                                                         : spec[f.first].is_array();
        if (!ok) {
            std::fprintf(stderr, "scene_gen: spec 字段 %s 须为 %s\n", f.first, f.second);
            return 2;
        }
    }
    if (name.empty() && spec.contains("name"))
        name = spec["name"].get<std::string>();

    // 网格：等宽、字符合法（'.' = terrain 0，'#' = terrain 1）
    std::vector<std::string> rows;
    for (const auto& r : spec["grid"]) {
        if (!r.is_string()) {
            std::fprintf(stderr, "scene_gen: grid 每行须为 string\n");
            return 2;
        }
        rows.push_back(r.get<std::string>());
    }
    const int h = static_cast<int>(rows.size());
    const int w = static_cast<int>(rows[0].size());
    for (int y = 0; y < h; ++y) {
        if (static_cast<int>(rows[static_cast<std::size_t>(y)].size()) != w) {
            std::fprintf(stderr, "scene_gen: 第 %d 行宽度 != %d（网格须等宽）\n", y, w);
            return 2;
        }
    }
    if (w < 1 || w > kDimMax || h < 1 || h > kDimMax) {
        std::fprintf(stderr, "scene_gen: 网格 %dx%d 越界（每边须在 %d..%d）\n", w, h, 1,
                     kDimMax);
        return 2;
    }

    std::vector<std::vector<int>> terrain(static_cast<std::size_t>(h),
                                         std::vector<int>(static_cast<std::size_t>(w), 0));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const char ch = rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
            if (ch == '.') terrain[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = 0;
            else if (ch == '#') terrain[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = 1;
            else {
                std::fprintf(stderr,
                             "scene_gen: grid[%d][%d] 字符 %c 非法（可用 '.'=terrain 0 / '#'=terrain 1）\n",
                             y, x, ch);
                return 2;
            }
        }
    }

    // 匹配表 + tile 尺寸（场景 tile_width/height 必须与 tileset 一致）
    auto table = tg::load_terrain_table(tileset_path);
    if (!table) {
        std::fprintf(stderr, "scene_gen: terrain table 加载失败: %s\n",
                     table.error().message.c_str());
        return 1;
    }
    if (table->set.mode != tg::TerrainMode::corners) {
        std::fprintf(stderr, "scene_gen: tileset mode 须为 corners（本工具只做 4 角采样）\n");
        return 1;
    }
    if (table->set.terrain_count < kTerrainCount) {
        std::fprintf(stderr, "scene_gen: tileset 地形数 %d < %d（占位图需两层地形）\n",
                     table->set.terrain_count, kTerrainCount);
        return 1;
    }
    json ts_doc;
    try {
        ts_doc = json::parse(read_file(("assets/" + tileset_path).c_str()));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "scene_gen: tileset 读取失败: %s\n", e.what());
        return 1;
    }
    const int tw = ts_doc.at("tile_width").get<int>();
    const int th = ts_doc.at("tile_height").get<int>();

    // 逐格：顶点采样 → 本格 terrain 池内选 id → 归入该 terrain 的层
    const auto at = [&](int x, int y) -> int {
        return (x >= 0 && x < w && y >= 0 && y < h)
                   ? terrain[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]
                   : -1;
    };
    std::vector<int> tiles[kTerrainCount];
    int sampled = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int self_v = at(x, y);
            std::array<int, 8> pattern{};
            pattern.fill(-1);
            using B = tg::TerrainBit;
            const auto corner = [&](B bit, int v) {
                pattern[static_cast<std::size_t>(bit)] = v;
            };
            corner(B::top_left_corner,
                   vote({self_v, at(x, y - 1), at(x - 1, y), at(x - 1, y - 1)}));
            corner(B::top_right_corner,
                   vote({self_v, at(x, y - 1), at(x + 1, y), at(x + 1, y - 1)}));
            corner(B::bottom_right_corner,
                   vote({self_v, at(x, y + 1), at(x + 1, y), at(x + 1, y + 1)}));
            corner(B::bottom_left_corner,
                   vote({self_v, at(x, y + 1), at(x - 1, y), at(x - 1, y + 1)}));
            auto id = tg::pick_tile(*table, 0, self_v, pattern);
            if (!id) {
                // 该地形池缺此角组合：产物会在 solid 层留空洞（无碰撞缺口），
                // 拒绝写出而不是静默填空。
                std::fprintf(stderr,
                             "scene_gen: (%d,%d) terrain %d 取不到 tile（tileset 该地形池"
                             "缺此角组合）——拒绝写出，请补齐标注或换 tileset\n",
                             x, y, self_v);
                return 1;
            }
            ++sampled;
            tiles[self_v].push_back(*id);
            tiles[1 - self_v].push_back(-1);
        }
    }

    // 组装构造描述（引擎侧类型化写路径）：层与 tileset 是本工具的产出主体；
    // entities 原样搬运（工具不解释实体字段）。
    const char* layer_names[kTerrainCount] = {"ground", "walls"};
    tg::SceneSpec scene_spec;
    scene_spec.tile_width = tw;
    scene_spec.tile_height = th;
    scene_spec.tilesets = {tg::TilesetRef{"terrain", tileset_path}};
    for (int t = 0; t < kTerrainCount; ++t) {
        tg::SceneLayerSpec layer;
        layer.name = layer_names[t];
        layer.solid = (t == 1);
        layer.width = w;
        layer.height = h;
        layer.tileset = "terrain";
        layer.tiles = std::move(tiles[t]);
        scene_spec.layers.push_back(std::move(layer));
    }
    scene_spec.name = name;
    if (spec.contains("background")) {
        scene_spec.has_background = true;
        scene_spec.background = spec["background"].get<std::string>();
    }
    scene_spec.entities = spec.contains("entities") ? spec["entities"] : json::array();

    // 先自检（与落盘用同一份文本，校验路径同 load_json）、后落盘：任何失败都不产生半成品
    const std::string diag_name = name.empty() ? "scene_gen" : name;
    auto text = tg::scene_spec_to_json(scene_spec);
    if (!text) {
        std::fprintf(stderr, "scene_gen: 场景序列化失败: %s\n",
                     text.error().message.c_str());
        return 1;
    }
    auto loaded = tg::SceneAsset::load_json(*text, diag_name);
    if (!loaded) {
        std::fprintf(stderr, "scene_gen: 场景自检失败: %s\n",
                     loaded.error().message.c_str());
        return 1;
    }
    const std::string out_path = argv[2];
    // 父目录按需创建（模板项目的 assets/ 可能还没有 scenes/，不应以此为由失败）
    std::error_code ec;
    const auto parent = std::filesystem::path(out_path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    std::ofstream out(out_path, std::ios::binary);
    // 产物保持既有可读形态（缩进 1）：先解析回 JSON 再缩进输出
    try {
        out << json::parse(*text).dump(1) << "\n";
    } catch (const std::exception& e) {
        std::fprintf(stderr, "scene_gen: 输出格式化失败: %s\n", e.what());
        return 1;
    }
    out.close();
    if (!out) {
        std::fprintf(stderr, "scene_gen: 写 %s 失败（目录不可写或磁盘空间不足）\n",
                     out_path.c_str());
        return 1;
    }
    std::printf("scene_gen: %s (%dx%d, tileset %s, 采样 %d/%d)\n", out_path.c_str(), w, h,
                tileset_path.c_str(), sampled, w * h);
    return 0;
}
