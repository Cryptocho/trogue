// scene_spec_test.cpp —— SceneSpec / SceneAsset::create 单测（纯公共 API，无窗口）。
//
// 覆盖：palette / 图集 / bare 三态的构造与回读、实体原样透传（含 animations）、
//「校验单一来源」（非法 spec 一律由 load_json 拒）、`scene_spec_to_json` 与 `create`
// 等价，以及序列化层自有的两个失败面（非有限数值、非法 UTF-8）。
//
// 边界：这些用例只钉「已列举的限额与组合规则」；schema 将来新增字段/形态时镜像仍能
// 编译、用例仍会全绿——故本文件不宣称覆盖 schema 全集（构造子集镜像 + 已列举规则）。
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>  // std::numeric_limits（Inf 构造）
#include <string>
#include <vector>

#include "trogue/trogue.hpp"
#include "test_util.hpp"

namespace {

using tg::Color;
using tg::ErrorCode;
using tg::SceneAsset;
using tg::SceneLayerSpec;
using tg::SceneSpec;
using tg::TilesetRef;

// 图集 fixture：与 scene_schema 测试同款写法（自写自删，不依赖随仓库提交的资产）。
constexpr const char* kAtlasFile = "__tmp_spec_atlas.json";

void write_tileset_fixture() {
    std::string tiles;
    for (int i = 0; i < 16; ++i) {
        if (i) tiles += ",";
        tiles += "{\"id\":" + std::to_string(i) + ",\"col\":" + std::to_string(i % 16) +
                 ",\"row\":" + std::to_string(i / 16) + "}";
    }
    std::ofstream f(std::string("assets/") + kAtlasFile, std::ios::binary);
    f << R"({"format":"tro-tileset","version":2,"texture":"textures/floor.png",)"
         R"("tile_width":16,"tile_height":16,"columns":16,"rows":16,"tiles":[)" +
             tiles + "]}";
}

void remove_tileset_fixture() {
    std::remove((std::string("assets/") + kAtlasFile).c_str());
}

// palette 场景：两层（0 非 solid、1 solid 且 origin 非 0），带 meta.name/background
SceneSpec palette_spec() {
    SceneSpec s;
    s.tile_width = 16;
    s.tile_height = 16;
    s.palette = {Color{42, 45, 58, 255}, Color{127, 140, 163, 255}};
    SceneLayerSpec ground;
    ground.name = "ground";
    ground.width = 4;
    ground.height = 3;
    ground.tiles.assign(12, 0);
    SceneLayerSpec walls;
    walls.name = "walls";
    walls.solid = true;
    walls.width = 4;
    walls.height = 3;
    walls.origin_x = 8;
    walls.origin_y = -4;
    walls.tiles.assign(12, -1);
    walls.tiles[5] = 1;
    s.layers = {ground, walls};
    s.name = "spec-test";
    s.has_background = true;
    s.background = "#101018";
    return s;
}

// ── palette 三态之一：构造 → 回读 ──
bool test_palette_roundtrip() {
    bool ok = true;
    const SceneSpec spec = palette_spec();
    auto r = SceneAsset::create(spec);
    REQUIRE(r.has_value());
    CHECK(r->layer_count() == 2);
    CHECK(r->tile_width() == 16 && r->tile_height() == 16);
    CHECK(r->layer(0).name == "ground");
    CHECK(r->layer(0).solid == false);
    CHECK(r->layer(0).tileset_index == -1);  // palette 模式
    CHECK(r->layer(1).name == "walls");
    CHECK(r->layer(1).solid == true);
    CHECK(r->layer(1).origin_x == 8 && r->layer(1).origin_y == -4);
    CHECK(r->layer(1).nonempty == 1);
    CHECK(r->solid_layer_indices() == std::vector<int>{1});
    // palette 模式：tile 值域 = 调色板索引，回读 tile_at 抽查
    {
        int v = -999;
        CHECK(tg::tile_at(*r, 0, {0, 0}, &v) == tg::TileLookupResult::occupied);
        CHECK(v == 0);
        const tg::Vec2 walls_cell{8 + 16 + 8, -4 + 16 + 8};  // 层 1 的 (1,1)
        CHECK(tg::tile_at(*r, 1, walls_cell, &v) == tg::TileLookupResult::occupied);
        CHECK(v == 1);
    }
    // meta.name/background 经序列化文本可见（公共 API 无 meta.name 读取口）
    auto text = tg::scene_spec_to_json(spec);
    REQUIRE(text.has_value());
    CHECK(text->find("\"name\":\"spec-test\"") != std::string::npos);
    CHECK(text->find("\"background\":\"#101018\"") != std::string::npos);
    return ok;
}

// ── 图集模式：tilesets + 层引用 ──
bool test_atlas_mode() {
    bool ok = true;
    SceneSpec spec;
    spec.tile_width = 16;
    spec.tile_height = 16;
    spec.tilesets = {TilesetRef{"ts", kAtlasFile}};
    SceneLayerSpec l;
    l.name = "atlas_layer";
    l.width = 2;
    l.height = 2;
    l.tileset = "ts";
    l.tiles = {0, 1, -1, 2};
    spec.layers = {l};
    auto r = SceneAsset::create(spec);
    REQUIRE(r.has_value());
    CHECK(r->layer_count() == 1);
    CHECK(r->layer(0).tileset_name == "ts");
    CHECK(r->layer(0).tileset_index == 0);
    CHECK(r->layer(0).nonempty == 3);
    return ok;
}

// ── bare：空 spec → 0 层且 tile 尺寸为 0（既有契约） ──
bool test_bare() {
    bool ok = true;
    SceneSpec spec;  // 三者皆空
    auto r = SceneAsset::create(spec);
    REQUIRE(r.has_value());
    CHECK(r->layer_count() == 0);
    CHECK(r->tile_width() == 0 && r->tile_height() == 0);  // bare = 0（不写尺寸）

    // schema 层面的 bare 变体：显式尺寸的 bare 合法、单边不成对被拒——这两条无法经
    // SceneSpec 表达（镜像里没有「显式尺寸」标记），故直接用文本入口断言。
    auto with_size = SceneAsset::load_json(
        R"({"format":"tro-scene","version":2,"tilemap":)"
        R"({"tile_width":24,"tile_height":32,"layers":[]},"entities":[]})");
    REQUIRE(with_size.has_value());
    CHECK(with_size->tile_width() == 24 && with_size->tile_height() == 32);

    auto one_sided = SceneAsset::load_json(
        R"({"format":"tro-scene","version":2,"tilemap":)"
        R"({"tile_width":24,"layers":[]},"entities":[]})");
    CHECK(!one_sided.has_value());

    // palette 模式但 layers 为空：不是 bare → 仍须写尺寸（否则文本会被拒）
    SceneSpec palette_only;
    palette_only.palette = {Color{1, 2, 3, 255}};
    auto pal = SceneAsset::create(palette_only);
    REQUIRE(pal.has_value());
    CHECK(pal->layer_count() == 0);
    CHECK(pal->tile_width() == 16);  // palette 模式写了尺寸
    return ok;
}

// ── 实体原样透传：animations/props/sprite 不因镜像而丢 ──
bool test_entities_passthrough() {
    bool ok = true;
    SceneSpec spec = palette_spec();
    spec.entities = tg::Json::parse(R"([
        {"id":"hero","type":"player","x":16.0,"y":32.0,"w":12,"h":16,
         "z":2,"rotation":45.5,"solid":true,"props":{"hp":7,"tags":["a","b"]},
         "sprite":{"texture":"textures/hero.png","region":[0,0,8,8],"flip_x":true},
         "animations":{"textures":["a.png"],"animations":[
            {"name":"idle","fps":4.0,"loop":true,"frames":[{"texture":0}]}]}}
    ])");
    auto r = SceneAsset::create(spec);
    REQUIRE(r.has_value());
    CHECK(r->entity_count() == 1);
    const tg::SceneEntity e = r->entity(0);
    CHECK(e.id == "hero" && e.type == "player");
    CHECK(e.x == 16.0f && e.y == 32.0f && e.w == 12.0f && e.h == 16.0f);
    CHECK(e.z == 2 && e.solid == true);
    CHECK(e.rotation == 45.5f);
    CHECK(e.props["hp"] == 7);
    CHECK(e.sprite.has && e.sprite.texture == "textures/hero.png");
    CHECK(e.sprite.region.w == 8.0f && e.sprite.flip_x);
    // animations 不在快照里，但**必须**活着进资产（这正是实体不镜像的原因）
    CHECK(r->animation_set_count() == 1);
    CHECK(r->animation_set(0).has_clip("idle"));
    return ok;
}

// ── 校验单一来源：非法 spec 一律失败（错误来自 load_json） ──
bool test_error_paths() {
    bool ok = true;
    {  // tiles 长度 ≠ width*height
        SceneSpec s = palette_spec();
        s.layers[0].tiles.pop_back();
        auto r = SceneAsset::create(s);
        REQUIRE(!r.has_value());
        CHECK(r.error().code == ErrorCode::kSchemaViolation);
    }
    {  // palette 与 tilesets 同时给（三态互斥）
        SceneSpec s = palette_spec();
        s.tilesets = {TilesetRef{"ts", kAtlasFile}};
        s.layers[0].tileset = "ts";
        s.layers[1].tileset = "ts";
        CHECK(!SceneAsset::create(s).has_value());
    }
    {  // 层引用的 tileset 名不存在
        SceneSpec s;
        s.tilesets = {TilesetRef{"ts", kAtlasFile}};
        SceneLayerSpec l;
        l.width = 1; l.height = 1; l.tiles = {0}; l.tileset = "nope";
        s.layers = {l};
        CHECK(!SceneAsset::create(s).has_value());
    }
    {  // 层尺寸超 kLayerDimMax
        SceneSpec s = palette_spec();
        s.layers[0].width = tg::kLayerDimMax + 1;
        CHECK(!SceneAsset::create(s).has_value());
    }
    {  // tile 尺寸同样有上界（与网格尺寸同一常量）
        SceneSpec s = palette_spec();
        s.tile_width = tg::kLayerDimMax + 1;
        CHECK(!SceneAsset::create(s).has_value());
        SceneSpec ok_spec = palette_spec();
        ok_spec.tile_width = tg::kTileDimMax;  // 引擎上界内，且是 schema 允许的最大 tile 尺寸
        CHECK(SceneAsset::create(ok_spec).has_value());
    }
    {  // 图集模式允许没有层（唯 tilesets 与 palette 不可同时给）
        SceneSpec s = palette_spec();
        s.palette.clear();
        for (auto& l : s.layers) l.tileset = kAtlasFile;
        s.layers.clear();
        CHECK(SceneAsset::create(s).has_value());
    }
    {  // has_background=false：background 串不落地（该键省略 → meta.props 为空）
        SceneSpec s = palette_spec();
        s.has_background = false;
        auto text = tg::scene_spec_to_json(s);
        REQUIRE(text.has_value());
        CHECK(text->find("background") == std::string::npos);
        auto loaded = SceneAsset::create(s);
        REQUIRE(loaded.has_value());
        CHECK(loaded->meta_props().empty());
    }
    {  // layers > 4
        SceneSpec s = palette_spec();
        for (int i = 0; i < 5; ++i) {
            SceneLayerSpec l;
            l.name = "l" + std::to_string(i);
            l.width = 1; l.height = 1; l.tiles = {0};
            s.layers.push_back(l);
        }
        CHECK(!SceneAsset::create(s).has_value());
    }
    {  // palette > 32
        SceneSpec s = palette_spec();
        s.palette.assign(33, Color{1, 2, 3, 255});
        CHECK(!SceneAsset::create(s).has_value());
    }
    {  // 重复实体 id
        SceneSpec s = palette_spec();
        s.entities = tg::Json::parse(R"([
            {"id":"dup","x":0,"y":0},{"id":"dup","x":1,"y":1}])");
        CHECK(!SceneAsset::create(s).has_value());
    }
    {  // 实体含未知键（schema 拒绝，不经快照静默丢弃）
        SceneSpec s = palette_spec();
        s.entities = tg::Json::parse(R"([{"id":"a","x":0,"y":0,"future_key":1}])");
        CHECK(!SceneAsset::create(s).has_value());
    }
    return ok;
}

// ── scene_spec_to_json 与 create 等价 ──
bool test_json_equivalence() {
    bool ok = true;
    SceneSpec spec = palette_spec();
    spec.entities = tg::Json::parse(R"([{"id":"hero","type":"player","x":8,"y":8}])");
    auto text = tg::scene_spec_to_json(spec);
    REQUIRE(text.has_value());
    auto via_text = SceneAsset::load_json(*text, "text");
    auto via_create = SceneAsset::create(spec);
    REQUIRE(via_text.has_value());
    REQUIRE(via_create.has_value());
    CHECK(via_text->layer_count() == via_create->layer_count());
    CHECK(via_text->tile_width() == via_create->tile_width());
    CHECK(via_text->layer(1).solid == via_create->layer(1).solid);
    CHECK(via_text->layer(1).origin_x == via_create->layer(1).origin_x);
    CHECK(via_text->layer(1).nonempty == via_create->layer(1).nonempty);
    CHECK(via_text->entity_count() == via_create->entity_count());
    CHECK(via_text->entity(0).id == via_create->entity(0).id);
    return ok;
}

// ── 序列化层自有的两个失败面 ──
bool test_serializer_failure_faces() {
    bool ok = true;
    {  // 非有限数值会被 JSON 序列化成 null：拒绝而不是静默改写
        SceneSpec s = palette_spec();
        tg::Json props = tg::Json::object();
        props["bad"] = std::nan("");
        s.props = props;
        const auto r = SceneAsset::create(s);
        REQUIRE(!r.has_value());
        CHECK(r.error().code == ErrorCode::kInvalidArgument);
        const auto t = tg::scene_spec_to_json(s);
        REQUIRE(!t.has_value());
        CHECK(t.error().code == ErrorCode::kInvalidArgument);
    }
    {  // 嵌套在 entities 数组/对象里的 NaN 同样被拒
        SceneSpec s = palette_spec();
        s.entities = tg::Json::parse(R"([{"id":"a","x":0,"y":0,"props":{"v":1}}])");
        s.entities[0]["props"]["v"] = std::numeric_limits<double>::infinity();
        CHECK(!tg::scene_spec_to_json(s).has_value());
    }
    {  // 非法 UTF-8：dump 会抛异常 → 收成 kInvalidArgument（不跨 API 抛）
        SceneSpec s = palette_spec();
        s.name = std::string("bad\xc3\x28utf8");
        const auto t = tg::scene_spec_to_json(s);
        REQUIRE(!t.has_value());
        CHECK(t.error().code == ErrorCode::kInvalidArgument);
        const auto r = SceneAsset::create(s);
        REQUIRE(!r.has_value());
        CHECK(r.error().code == ErrorCode::kInvalidArgument);
    }
    return ok;
}

}  // namespace

int main() {
    std::filesystem::create_directories("assets");
    write_tileset_fixture();

    bool ok = true;
    ok = test_palette_roundtrip() && ok;
    ok = test_atlas_mode() && ok;
    ok = test_bare() && ok;
    ok = test_entities_passthrough() && ok;
    ok = test_error_paths() && ok;
    ok = test_json_equivalence() && ok;
    ok = test_serializer_failure_faces() && ok;

    remove_tileset_fixture();
    std::printf("[scene_spec_test] checks=%d failures=%d\n", tg_test::g_checks,
                tg_test::g_failures);
    if (tg_test::g_failures == 0 && ok) {
        std::printf("[scene_spec_test] OK\n");
        return 0;
    }
    std::printf("[scene_spec_test] FAILED\n");
    return 1;
}
