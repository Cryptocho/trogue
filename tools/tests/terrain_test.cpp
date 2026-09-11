// terrain_test.cpp —— TerrainTable 加载与 pick_tile 选择器单测（plan-12 §7）。
//
// 覆盖：共享解析核心的 terrain 字段校验全集（经 load_terrain_table 公共入口；
// 场景加载共用同一核心，正向回归由 scene_schema_test 的真实资产用例承担）、
// 16-blob 精确命中 / 残缺降级（手算期望表）/ 同分 tie / 均匀 +1（未标注位不改
// 排序）/ kNotFound 与 kInvalidArgument 分层 / 真实资产回归。
// 临时 tileset 写 assets/ 根（CWD=项目根），用后即删。
#include <array>
#include <cstdio>
#include <string>

#include "trogue/trogue.hpp"
#include "test_util.hpp"

namespace {

using tg::TerrainBit;
using tg::TerrainMode;

int g_seq = 0;

// 写临时 tileset（物理路径 assets/<rel>），返回 API 用的 assets 相对路径
std::string write_tileset(const std::string& content) {
    const std::string rel = "__tmp_terrain_" + std::to_string(g_seq++) + ".json";
    const std::string full = "assets/" + rel;
    std::FILE* f = std::fopen(full.c_str(), "wb");
    std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
    return rel;
}
void remove_tileset(const std::string& rel) { std::remove(("assets/" + rel).c_str()); }

// 合成 tileset：terrain_sets 串 + tiles 串
std::string tileset_json(const std::string& terrain_sets, const std::string& tiles) {
    return R"({"format":"tro-tileset","version":2,)"
           R"("texture":"textures/floor.png","tile_width":16,"tile_height":16,)"
           R"("columns":16,"rows":16,)" + terrain_sets +
           R"("tiles":[)" + tiles + "]}";
}

const char* kCornersOneTerrain =
    R"("terrain_sets":[{"mode":"corners","terrains":[{"name":"ground","color":"#ffdb00"}]}],)";const char* kOneTerrain =
    R"("terrain_sets":[{"mode":"corners_and_sides","terrains":[{"name":"ground","color":"#ffdb00"}]}],)";

// mask = t*8 + r*4 + b*2 + l；生成带四边 peering_bits 的 tile JSON（id 显式）
std::string mask_tile(int id, int mask, int terrain) {
    std::string s = "{\"id\":" + std::to_string(id) + ",\"col\":" +
                    std::to_string(id % 16) + ",\"row\":" + std::to_string(id / 16) +
                    ",\"terrain_set\":0,\"terrain\":" + std::to_string(terrain);
    std::string bits;
    auto add = [&](const char* name, int v) {
        if (!bits.empty()) bits += ",";
        bits += std::string("\"") + name + "\":" + std::to_string(v);
    };
    if (mask & 8) add("top_side", 0);
    if (mask & 4) add("right_side", 0);
    if (mask & 2) add("bottom_side", 0);
    if (mask & 1) add("left_side", 0);
    if (!bits.empty()) s += ",\"peering_bits\":{" + bits + "}";
    return s + "}";
}

// corners mode：4 角组合编号 c = tl*8 + tr*4 + br*2 + bl（1 = 该角标 terrain 0）
std::string corner_tile(int id, int c, int terrain) {
    std::string s = "{\"id\":" + std::to_string(id) + ",\"col\":" +
                    std::to_string(id % 16) + ",\"row\":" + std::to_string(id / 16) +
                    ",\"terrain_set\":0,\"terrain\":" + std::to_string(terrain);
    std::string bits;
    auto add = [&](const char* name, int v) {
        if (!bits.empty()) bits += ",";
        bits += std::string("\"") + name + "\":" + std::to_string(v);
    };
    if (c & 8) add("top_left_corner", 0);
    if (c & 4) add("top_right_corner", 0);
    if (c & 2) add("bottom_right_corner", 0);
    if (c & 1) add("bottom_left_corner", 0);
    if (!bits.empty()) s += ",\"peering_bits\":{" + bits + "}";
    return s + "}";
}
std::string corner_tiles(int terrain) {
    std::string out;
    for (int c = 0; c < 16; ++c) {
        if (c > 0) out += ",";
        out += corner_tile(c, c, terrain);
    }
    return out;
}

// 角组合 c → 4 角 pattern（边位恒 -1）
std::array<int, 8> corner_pattern(int c) {
    std::array<int, 8> p{};
    p.fill(-1);
    p[static_cast<std::size_t>(TerrainBit::top_left_corner)] = (c & 8) ? 0 : -1;
    p[static_cast<std::size_t>(TerrainBit::top_right_corner)] = (c & 4) ? 0 : -1;
    p[static_cast<std::size_t>(TerrainBit::bottom_right_corner)] = (c & 2) ? 0 : -1;
    p[static_cast<std::size_t>(TerrainBit::bottom_left_corner)] = (c & 1) ? 0 : -1;
    return p;
}

std::string mask_tiles(int terrain, bool skip_single_sides) {
    std::string out;
    int id = 0;
    for (int mask = 0; mask < 16; ++mask) {
        const int ones = (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1) +
                         ((mask >> 3) & 1);
        if (skip_single_sides && ones == 1) continue;  // 残缺集：删 4 个单边 mask
        if (id > 0) out += ",";
        out += mask_tile(id, mask, terrain);
        ++id;
    }
    return out;
}

// mask → 8 方向 pattern（四边按 mask，四角 unconstrained=-1）
std::array<int, 8> pattern_from_mask(int mask, int corner_value = -1) {
    std::array<int, 8> p{};
    p.fill(corner_value);
    p[static_cast<std::size_t>(TerrainBit::top_side)] = (mask & 8) ? 0 : -1;
    p[static_cast<std::size_t>(TerrainBit::right_side)] = (mask & 4) ? 0 : -1;
    p[static_cast<std::size_t>(TerrainBit::bottom_side)] = (mask & 2) ? 0 : -1;
    p[static_cast<std::size_t>(TerrainBit::left_side)] = (mask & 1) ? 0 : -1;
    return p;
}

// ── 1. 合法加载 + 键缺省语义 ──
bool test_load_valid() {
    bool ok = true;
    const std::string rel = write_tileset(tileset_json(kOneTerrain, mask_tiles(0, false)));
    auto r = tg::load_terrain_table(rel);
    remove_tileset(rel);
    CHECK(r.has_value());
    if (!r) return false;
    CHECK(r->set.mode == TerrainMode::corners_and_sides);
    CHECK(r->set.terrain_count == 1);
    CHECK(static_cast<int>(r->tiles.size()) == 16);
    // 键缺省语义：无 terrain 键的 tile = {-1,-1,全 -1}（plan-12 §4.1 表）
    {
        const std::string rel2 = write_tileset(tileset_json(
            kOneTerrain,
            mask_tile(0, 0, 0) + ",{\"id\":1,\"col\":1,\"row\":0}"));
        auto r2 = tg::load_terrain_table(rel2);
        remove_tileset(rel2);
        CHECK(r2.has_value());
        if (r2) {
            CHECK(r2->tiles[1].terrain_set == -1);
            CHECK(r2->tiles[1].terrain == -1);
            CHECK(r2->tiles[1].bits[static_cast<std::size_t>(TerrainBit::top_side)] == -1);
        }
    }
    return ok;
}

// ── 2. 16-blob 全集：逐 mask 精确命中（对照手写表 id = mask） ──
bool test_blob_exact() {
    bool ok = true;
    const std::string rel = write_tileset(tileset_json(kOneTerrain, mask_tiles(0, false)));
    auto t = tg::load_terrain_table(rel);
    remove_tileset(rel);
    REQUIRE(t.has_value());
    for (int mask = 0; mask < 16; ++mask) {
        auto id = tg::pick_tile(*t, 0, 0, pattern_from_mask(mask));
        CHECK(id.has_value() && *id == mask);
    }
    // 均匀 +1（plan-12 §7）：四角位给 0（候选全不标四角）与四角全 -1 结果一致
    for (int mask = 0; mask < 16; ++mask) {
        auto id = tg::pick_tile(*t, 0, 0, pattern_from_mask(mask, 0));
        CHECK(id.has_value() && *id == mask);
    }
    return ok;
}

// ── 3. 残缺集（删 4 个单边 mask）：降级命中手算期望表 ──
// 删 mask 1/2/4/8（单边），剩余 12 tile 按升序重编号 id 0..11。
// 单边 pattern 的最低分 = 全 -1 tile（score 1，id 0）；mask0/15/3/6 精确在场。
bool test_blob_degraded() {
    bool ok = true;
    const std::string rel =
        write_tileset(tileset_json(kOneTerrain, mask_tiles(0, true)));
    auto t = tg::load_terrain_table(rel);
    remove_tileset(rel);
    REQUIRE(t.has_value());
    CHECK(static_cast<int>(t->tiles.size()) == 12);
    struct Case { int mask; int want; };
    const Case cases[] = {
        {0, 0},   // 精确在场
        {15, 11}, // 精确在场（最大 mask）
        {3, 1},   // 精确在场（跳号后 id=1）
        {6, 3},   // 精确在场（跳号后 id=3）
        {1, 0},   // 被删：全 -1 tile 最近（score 1）
        {2, 0},   // 被删：同上
        {4, 0},   // 被删：同上
        {8, 0},   // 被删：同上
    };
    for (const auto& c : cases) {
        auto id = tg::pick_tile(*t, 0, 0, pattern_from_mask(c.mask));
        CHECK(id.has_value() && *id == c.want);
    }
    return ok;
}

// ── 4. 同 bits 双 tile tie → 最小 id ──
bool test_tie_lowest_id() {
    bool ok = true;
    const std::string rel = write_tileset(
        tileset_json(kOneTerrain, mask_tile(0, 0, 0) + "," + mask_tile(1, 0, 0)));
    auto t = tg::load_terrain_table(rel);
    remove_tileset(rel);
    REQUIRE(t.has_value());
    auto id = tg::pick_tile(*t, 0, 0, pattern_from_mask(0));
    CHECK(id.has_value() && *id == 0);
    return ok;
}

// ── 5. 错误分层：kNotFound（terrain 无候选）与 kInvalidArgument（参数非法） ──
bool test_error_contract() {
    bool ok = true;
    // terrain_sets 声明 2 terrains，但只有 terrain 0 的 tile → terrain 1 = kNotFound
    {
        const std::string sets =
            R"("terrain_sets":[{"mode":"corners_and_sides","terrains":[)"
            R"({"name":"ground","color":"#ffdb00"},{"name":"water","color":"#0000ff"}]}],)";
        const std::string rel = write_tileset(tileset_json(sets, mask_tile(0, 0, 0)));
        auto t = tg::load_terrain_table(rel);
        remove_tileset(rel);
        REQUIRE(t.has_value());
        auto miss = tg::pick_tile(*t, 0, 1, pattern_from_mask(0));
        CHECK(!miss.has_value() && miss.error().code == tg::ErrorCode::kNotFound);
        // 参数非法
        auto bad_set = tg::pick_tile(*t, 1, 0, pattern_from_mask(0));
        CHECK(!bad_set.has_value() &&
              bad_set.error().code == tg::ErrorCode::kInvalidArgument);
        auto bad_terrain = tg::pick_tile(*t, 0, 2, pattern_from_mask(0));
        CHECK(!bad_terrain.has_value() &&
              bad_terrain.error().code == tg::ErrorCode::kInvalidArgument);
        auto neg_terrain = tg::pick_tile(*t, 0, -1, pattern_from_mask(0));
        CHECK(!neg_terrain.has_value() &&
              neg_terrain.error().code == tg::ErrorCode::kInvalidArgument);
        auto p = pattern_from_mask(0);
        p[static_cast<std::size_t>(TerrainBit::top_side)] = 5;  // ≥ terrain_count
        auto bad_pattern = tg::pick_tile(*t, 0, 0, p);
        CHECK(!bad_pattern.has_value() &&
              bad_pattern.error().code == tg::ErrorCode::kInvalidArgument);
        // pattern 负非 -1（plan §7 点名；评审非阻断 2）
        p[static_cast<std::size_t>(TerrainBit::top_side)] = -2;
        bad_pattern = tg::pick_tile(*t, 0, 0, p);
        CHECK(!bad_pattern.has_value() &&
              bad_pattern.error().code == tg::ErrorCode::kInvalidArgument);
    }
    // sides 模式：pattern 带 corner 位（非法方向）→ kInvalidArgument
    {
        const std::string sets =
            R"("terrain_sets":[{"mode":"sides","terrains":[{"name":"g","color":"#000000"}]}],)";
        const std::string rel =
            write_tileset(tileset_json(sets, mask_tile(0, 0, 0)));
        auto t = tg::load_terrain_table(rel);
        remove_tileset(rel);
        REQUIRE(t.has_value());
        auto p = pattern_from_mask(0);
        p[static_cast<std::size_t>(TerrainBit::top_left_corner)] = 0;
        auto bad = tg::pick_tile(*t, 0, 0, p);
        CHECK(!bad.has_value() && bad.error().code == tg::ErrorCode::kInvalidArgument);
    }
    return ok;
}

// ── 6. 加载拒绝（共享解析核心的 terrain 校验反例） ──
bool test_load_rejections() {
    bool ok = true;
    auto expect_schema = [&](const std::string& body, const char* label) {
        const std::string rel = write_tileset(body);
        auto r = tg::load_terrain_table(rel);
        remove_tileset(rel);
        CHECK(!r.has_value() && r.error().code == tg::ErrorCode::kSchemaViolation);
        if (r.has_value()) std::printf("  [debug] %s 意外成功\n", label);
    };
    const std::string one_tile = mask_tile(0, 0, 0);
    // mode 非法串
    expect_schema(tileset_json(
        R"("terrain_sets":[{"mode":"matches","terrains":[{"name":"g","color":"#000000"}]}],)",
        one_tile), "mode 非法串");
    // bit 键名非法
    expect_schema(tileset_json(kOneTerrain,
                               mask_tile(0, 0, 0).insert(mask_tile(0, 0, 0).size() - 1,
                                                         ",\"peering_bits\":{\"diagonal\":0}")),
                  "bit 键名非法");
    // bit 种类与 mode 不符（sides 集合出现 corner 位）
    expect_schema(tileset_json(
        R"("terrain_sets":[{"mode":"sides","terrains":[{"name":"g","color":"#000000"}]}],)",
        "{\"id\":0,\"col\":0,\"row\":0,\"terrain_set\":0,\"terrain\":0,"
        "\"peering_bits\":{\"top_left_corner\":0}}"), "bit 种类不符");
    // terrain 序号越界（1 terrain 写 terrain:1）
    expect_schema(tileset_json(kOneTerrain, mask_tile(0, 0, 1)), "terrain 越界");
    // terrain_set 越界
    expect_schema(tileset_json(kOneTerrain,
                               "{\"id\":0,\"col\":0,\"row\":0,\"terrain_set\":1,\"terrain\":0}"),
                  "terrain_set 越界");
    // 只写 terrain 不写 terrain_set
    expect_schema(tileset_json(kOneTerrain,
                               "{\"id\":0,\"col\":0,\"row\":0,\"terrain\":0}"),
                  "只写 terrain");
    // peering_bits 非 object
    expect_schema(tileset_json(kOneTerrain,
                               "{\"id\":0,\"col\":0,\"row\":0,\"terrain_set\":0,\"terrain\":0,\"peering_bits\":[]}"),
                  "peering_bits 非 object");
    // peering_bits 值负 / 超 terrains 数
    expect_schema(tileset_json(kOneTerrain,
                               "{\"id\":0,\"col\":0,\"row\":0,\"terrain_set\":0,\"terrain\":0,\"peering_bits\":{\"top_side\":-1}}"),
                  "peering_bits 值负");
    expect_schema(tileset_json(kOneTerrain,
                               "{\"id\":0,\"col\":0,\"row\":0,\"terrain_set\":0,\"terrain\":0,\"peering_bits\":{\"top_side\":1}}"),
                  "peering_bits 值超范围");
    // terrains 元素缺 name / 缺 color / color 非法
    expect_schema(tileset_json(
        R"("terrain_sets":[{"mode":"sides","terrains":[{"color":"#000000"}]}],)",
        one_tile), "terrains 缺 name");
    expect_schema(tileset_json(
        R"("terrain_sets":[{"mode":"sides","terrains":[{"name":"g"}]}],)",
        one_tile), "terrains 缺 color");
    expect_schema(tileset_json(
        R"("terrain_sets":[{"mode":"sides","terrains":[{"name":"g","color":"red"}]}],)",
        one_tile), "color 非法");
    // 负非 -1（plan §7 点名；评审阻断 1）
    expect_schema(tileset_json(kOneTerrain,
                               "{\"id\":0,\"col\":0,\"row\":0,\"terrain_set\":0,\"terrain\":-2}"),
                  "terrain 负非 -1");
    expect_schema(tileset_json(kOneTerrain,
                               "{\"id\":0,\"col\":0,\"row\":0,\"terrain_set\":-2,\"terrain\":-2}"),
                  "terrain_set 负非 -1");
    // TerrainTable v1 限单 set：缺失 / 空数组 / 2 组
    expect_schema(tileset_json("", one_tile), "terrain_sets 缺失");
    expect_schema(tileset_json(R"("terrain_sets":[],)", one_tile), "terrain_sets 空");
    expect_schema(tileset_json(
        R"("terrain_sets":[{"mode":"sides","terrains":[{"name":"a","color":"#000000"}]},)"
        R"({"mode":"corners","terrains":[{"name":"b","color":"#111111"}]}],)",
        one_tile), "terrain_sets 2 组");
    // 路径非法 / 不存在
    {
        auto bad = tg::load_terrain_table("../etc/passwd");
        CHECK(!bad.has_value() && bad.error().code == tg::ErrorCode::kInvalidArgument);
        auto missing = tg::load_terrain_table("tilesets/__no_such__.json");
        CHECK(!missing.has_value() && missing.error().code == tg::ErrorCode::kIoError);
    }
    return ok;
}

// ── 7. 真实资产回归（test_tileset_1.json：corners_and_sides 1 terrain 16 tile） ──
bool test_real_asset() {
    bool ok = true;
    auto t = tg::load_terrain_table("tilesets/test_tileset_1.json");
    CHECK(t.has_value());
    if (!t) return false;
    CHECK(t->set.mode == TerrainMode::corners_and_sides);
    CHECK(t->set.terrain_count == 1);
    CHECK(static_cast<int>(t->tiles.size()) == 16);
    const auto b = [&](int id, TerrainBit bit) {
        return t->tiles[static_cast<std::size_t>(id)].bits[static_cast<std::size_t>(bit)];
    };
    // 导出标注抽查：id3=仅 right_side；id5=四边；id15=全未标注；四角恒 -1
    CHECK(b(3, TerrainBit::right_side) == 0);
    CHECK(b(3, TerrainBit::top_side) == -1);
    CHECK(b(5, TerrainBit::top_side) == 0 && b(5, TerrainBit::bottom_side) == 0 &&
          b(5, TerrainBit::left_side) == 0 && b(5, TerrainBit::right_side) == 0);
    CHECK(b(15, TerrainBit::top_side) == -1 &&
          b(15, TerrainBit::bottom_right_corner) == -1);
    return ok;
}

// ── 8. corners mode 全组合：16 角组合逐 pattern 精确命中 + 残缺降级 ──
bool test_corners_mode() {
    bool ok = true;
    // 全集：tile id = 角组合 c，逐 pattern 精确命中
    {
        const std::string rel =
            write_tileset(tileset_json(kCornersOneTerrain, corner_tiles(0)));
        auto t = tg::load_terrain_table(rel);
        remove_tileset(rel);
        REQUIRE(t.has_value());
        CHECK(t->set.mode == TerrainMode::corners);
        CHECK(static_cast<int>(t->tiles.size()) == 16);
        for (int c = 0; c < 16; ++c) {
            auto id = tg::pick_tile(*t, 0, 0, corner_pattern(c));
            CHECK(id.has_value() && *id == c);
        }
        // 边位非 -1 在 corners mode = 非法方向位 → kInvalidArgument
        //（pick_tile 契约：mode 非法位必须 -1，静默忽略违反「明确损失」原则）
        auto p = corner_pattern(5);
        p[static_cast<std::size_t>(TerrainBit::top_side)] = 0;
        auto bad = tg::pick_tile(*t, 0, 0, p);
        CHECK(!bad.has_value() && bad.error().code == tg::ErrorCode::kInvalidArgument);
    }
    // 残缺集：删 4 个单角组合（c=1/2/4/8），13 tile 按升序重编号
    {
        std::string tiles;
        int id = 0;
        for (int c = 0; c < 16; ++c) {
            const int ones = (c & 1) + ((c >> 1) & 1) + ((c >> 2) & 1) + ((c >> 3) & 1);
            if (ones == 1) continue;  // 删单角
            if (id > 0) tiles += ",";
            tiles += corner_tile(id, c, 0);
            ++id;
        }
        const std::string rel =
            write_tileset(tileset_json(kCornersOneTerrain, tiles));
        auto t = tg::load_terrain_table(rel);
        remove_tileset(rel);
        REQUIRE(t.has_value());
        CHECK(static_cast<int>(t->tiles.size()) == 12);
        // 期望表：单角 pattern 最低分 = 全 -1 tile（score 1，id 0）；在场组合精确命中
        struct Case { int c; int want; };
        const Case cases[] = {
            {0, 0},   // 精确在场
            {15, 11}, // 精确在场（最大组合）
            {3, 1},   // 精确在场（跳号后 id=1）
            {5, 2},   // 精确在场（跳号后 id=2）
            {1, 0},   // 被删：全 -1 tile 最近（score 1）
            {2, 0},   // 被删：同上
            {4, 0},   // 被删：同上
            {8, 0},   // 被删：同上
        };
        for (const auto& cs : cases) {
            auto id2 = tg::pick_tile(*t, 0, 0, corner_pattern(cs.c));
            CHECK(id2.has_value() && *id2 == cs.want);
        }
    }
    return ok;
}

}  // namespace

int main() {
    test_load_valid();
    test_blob_exact();
    test_blob_degraded();
    test_tie_lowest_id();
    test_error_contract();
    test_load_rejections();
    test_real_asset();
    test_corners_mode();
    std::printf("[terrain test] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}
