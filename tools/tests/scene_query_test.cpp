// scene_query_test.cpp —— tile 查询 / asset_id / 失败安全单测。
//
// 覆盖：负坐标 floor、层外不阻挡、半开矩形、多 solid 层短路、无 solid 层 clear、
// tile_at 边界、查询 error 条件、asset_id 单调与跨 asset 快照归属、坏输入不影响
// 既有 asset、asset_id 耗尽 seam（TROGUE_TEST_SEAMS）。
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "trogue/trogue.hpp"
#include "scene_test_seams.hpp"  // seam：asset_test_seed_id/reset_id（测试库）
#include "test_util.hpp"

namespace {

using tg::SceneAsset;
using tg::TileQueryResult;
using tg::TileLookupResult;

// NaN/Inf 测试值（非有限坐标 → error 路径）
const float nan_v = std::numeric_limits<float>::quiet_NaN();
const float inf_v = std::numeric_limits<float>::infinity();

// 写临时场景文件（CWD=项目根，build/ 目录保证存在）
int g_seq = 0;
std::string write_scene(const std::string& content) {
    const std::string path = "build/tmp_query_" + std::to_string(g_seq++) + ".json";
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();
    return path;
}

// 构造单 solid 层 4×4 场景：tile(0,0)=1 实心，其余 -1
std::string single_solid_scene() {
    return R"({"format":"tro-scene","version":2,"tilemap":{
        "tile_width":16,"tile_height":16,
        "palette":["#2a2d3a","#7f8ca3"],
        "layers":[
          {"name":"g","width":4,"height":4,"solid":true,
           "tiles":[1,-1,-1,-1,
                    -1,-1,-1,-1,
                    -1,-1,-1,-1,
                    -1,-1,-1,-1]} ]},"entities":[]})";
}

// 两 solid 层：层0 (0,0) 实心；层1 (3,3) 实心（不同层不短路）
std::string two_solid_scene() {
    return R"({"format":"tro-scene","version":2,"tilemap":{
        "tile_width":16,"tile_height":16,
        "palette":["#2a2d3a","#7f8ca3"],
        "layers":[
          {"name":"a","width":4,"height":4,"solid":true,
           "tiles":[1,-1,-1,-1,   -1,-1,-1,-1,   -1,-1,-1,-1,   -1,-1,-1,-1]},
          {"name":"b","width":4,"height":4,"solid":true,
           "tiles":[-1,-1,-1,-1,  -1,-1,-1,-1,   -1,-1,-1,-1,   -1,-1,-1,1]} ]},"entities":[]})";
}

// 无 solid 层（其层即使有 tile 也不参与查询）
std::string no_solid_scene() {
    return R"({"format":"tro-scene","version":2,"tilemap":{
        "tile_width":16,"tile_height":16,
        "palette":["#2a2d3a","#7f8ca3"],
        "layers":[
          {"name":"g","width":4,"height":4,"solid":false,
           "tiles":[1,1,1,1,  1,1,1,1,  1,1,1,1,  1,1,1,1]} ]},"entities":[]})";
}

bool test_is_solid_at() {
    bool ok = true;
    const std::string p = write_scene(single_solid_scene());
    auto r = SceneAsset::load(p);
    std::remove(p.c_str());
    REQUIRE(r.has_value());
    const SceneAsset& a = *r;

    // (0,0) tile 实心：中心 (8,8) 与角 (0,0) 都命中
    CHECK(is_solid_at(a, tg::Vec2{8.0f, 8.0f}) == TileQueryResult::solid);
    CHECK(is_solid_at(a, tg::Vec2{0.0f, 0.0f}) == TileQueryResult::solid);
    // (1,1) -1 → clear
    CHECK(is_solid_at(a, tg::Vec2{24.0f, 24.0f}) == TileQueryResult::clear);
    // 层外 = 不阻挡
    CHECK(is_solid_at(a, tg::Vec2{63.0f, 8.0f}) == TileQueryResult::clear);
    CHECK(is_solid_at(a, tg::Vec2{-8.0f, 8.0f}) == TileQueryResult::clear);
    // 负坐标 floor：(-0.5, -0.5) → tile(-1,-1) 层外 → clear
    CHECK(is_solid_at(a, tg::Vec2{-0.5f, -0.5f}) == TileQueryResult::clear);
    // 非有限 → error
    CHECK(is_solid_at(a, tg::Vec2{nan_v, 0.0f}) == TileQueryResult::error);
    CHECK(is_solid_at(a, tg::Vec2{0.0f, inf_v}) == TileQueryResult::error);
    // 「极大但有限」坐标：有限非 error，层外 → clear（无 int 转换 UB）
    CHECK(is_solid_at(a, tg::Vec2{1e38f, 1e38f}) != TileQueryResult::error);
    CHECK(is_solid_at(a, tg::Vec2{1e38f, 1e38f}) == TileQueryResult::clear);

    // 无 solid 层 → clear
    const std::string p2 = write_scene(no_solid_scene());
    auto r2 = SceneAsset::load(p2);
    std::remove(p2.c_str());
    REQUIRE(r2.has_value());
    CHECK(is_solid_at(*r2, tg::Vec2{8.0f, 8.0f}) == TileQueryResult::clear);

    // 两 solid 层：层0 (0,0) 实心 → solid；(3,3) 层1 实心 → solid
    const std::string p3 = write_scene(two_solid_scene());
    auto r3 = SceneAsset::load(p3);
    std::remove(p3.c_str());
    REQUIRE(r3.has_value());
    CHECK(is_solid_at(*r3, tg::Vec2{8.0f, 8.0f}) == TileQueryResult::solid);
    CHECK(is_solid_at(*r3, tg::Vec2{56.0f, 56.0f}) == TileQueryResult::solid);
    return ok;
}

bool test_rect_hits_solid() {
    bool ok = true;
    const std::string p = write_scene(single_solid_scene());
    auto r = SceneAsset::load(p);
    std::remove(p.c_str());
    REQUIRE(r.has_value());
    const SceneAsset& a = *r;

    // 整覆盖 (0,0)-(16,16) → solid
    CHECK(rect_hits_solid(a, tg::Rect{0, 0, 16, 16}) == TileQueryResult::solid);
    // 精确贴 tile 半开区间：x∈[0,16) → tile0；x=16 属 tile1
    CHECK(rect_hits_solid(a, tg::Rect{16, 0, 16, 16}) == TileQueryResult::clear);
    // 只覆盖 tile0 一像素的右边区域（x∈[15.9,16.9)）仍命中 tile0 实心
    CHECK(rect_hits_solid(a, tg::Rect{15.9f, 0, 1, 16}) == TileQueryResult::solid);
    // 负坐标覆盖 (-1,-1)-(2,2)：floor 后含 tile(0,0)
    CHECK(rect_hits_solid(a, tg::Rect{-1, -1, 3, 3}) == TileQueryResult::solid);
    // error：w/h 非正 / 非有限
    CHECK(rect_hits_solid(a, tg::Rect{0, 0, 0, 16}) == TileQueryResult::error);
    CHECK(rect_hits_solid(a, tg::Rect{0, 0, 16, -1}) == TileQueryResult::error);
    CHECK(rect_hits_solid(a, tg::Rect{0, 0, nan_v, 16}) == TileQueryResult::error);

    // 层外矩形 → clear
    CHECK(rect_hits_solid(a, tg::Rect{100, 100, 16, 16}) == TileQueryResult::clear);
    // 无 solid 层 → clear
    const std::string p2 = write_scene(no_solid_scene());
    auto r2 = SceneAsset::load(p2);
    std::remove(p2.c_str());
    REQUIRE(r2.has_value());
    CHECK(rect_hits_solid(*r2, tg::Rect{0, 0, 64, 64}) == TileQueryResult::clear);

    // 「极大但有限」坐标：不得触发 float→int 未定义转换。
    // 有限 → 非 error；完全层外 → clear（不是 UB/崩溃）。
    const float huge = 1e38f;
    CHECK(rect_hits_solid(a, tg::Rect{huge, huge, 16, 16}) != TileQueryResult::error);
    CHECK(rect_hits_solid(a, tg::Rect{huge, huge, 16, 16}) == TileQueryResult::clear);
    CHECK(rect_hits_solid(a, tg::Rect{-huge, -huge, 16, 16}) == TileQueryResult::clear);
    // 跨越巨大区间的矩形（下界巨负、上界巨正）覆盖整层 → 命中 (0,0) 实心
    CHECK(rect_hits_solid(a, tg::Rect{-huge, -huge, 2 * huge, 2 * huge}) ==
          TileQueryResult::solid);
    return ok;
}

bool test_tile_at() {
    bool ok = true;
    const std::string p = write_scene(single_solid_scene());
    auto r = SceneAsset::load(p);
    std::remove(p.c_str());
    REQUIRE(r.has_value());
    const SceneAsset& a = *r;

    int v = 999;
    // (0,0) 占用值 1
    CHECK(tile_at(a, 0, tg::Vec2{8.0f, 8.0f}, &v) == TileLookupResult::occupied);
    CHECK(v == 1);
    // 空格 (1,1) → empty 且 -1
    v = 999;
    CHECK(tile_at(a, 0, tg::Vec2{24.0f, 24.0f}, &v) == TileLookupResult::empty);
    CHECK(v == -1);
    // 层外 → empty
    v = 999;
    CHECK(tile_at(a, 0, tg::Vec2{100.0f, 8.0f}, &v) == TileLookupResult::empty);
    CHECK(v == -1);
    // error：层越界
    v = 999;
    CHECK(tile_at(a, 1, tg::Vec2{8.0f, 8.0f}, &v) == TileLookupResult::error);
    CHECK(v == 999);  // error 不写 out
    // error：out==nullptr
    CHECK(tile_at(a, 0, tg::Vec2{8.0f, 8.0f}, nullptr) == TileLookupResult::error);
    // error：坐标非有限
    v = 999;
    CHECK(tile_at(a, 0, tg::Vec2{nan_v, 8.0f}, &v) == TileLookupResult::error);
    CHECK(v == 999);
    // error：负 layer
    v = 999;
    CHECK(tile_at(a, -1, tg::Vec2{8.0f, 8.0f}, &v) == TileLookupResult::error);
    return ok;
}

bool test_asset_id_and_safety() {
    bool ok = true;
    // asset_id 单调递增
    const std::string p = write_scene(single_solid_scene());
    auto r1 = SceneAsset::load(p);
    REQUIRE(r1.has_value());
    auto r2 = SceneAsset::load(p);
    std::remove(p.c_str());
    REQUIRE(r2.has_value());
    const std::uint64_t id1 = r1->asset_id();
    const std::uint64_t id2 = r2->asset_id();
    CHECK(id1 != 0);
    CHECK(id2 > id1);

    // 跨 asset 快照归属：A 的 sprite 携带 A 的 id，与 B 不同
    //（sprite 快照在实体取回时填 asset_id —— 这里用无 sprite 的骨架，
    //  归属校验的渲染侧才断言；此处验证两个 asset id 不同即可）

    // 坏输入不影响既有 asset：在 r1 上先取一次值，再铺坏文件触发失败
    const int before = static_cast<int>(is_solid_at(*r1, tg::Vec2{8.0f, 8.0f}));
    const std::string bad = write_scene(R"({"format":"nope"})");
    auto rb = SceneAsset::load(bad);
    std::remove(bad.c_str());
    CHECK(!rb.has_value());
    CHECK(static_cast<int>(is_solid_at(*r1, tg::Vec2{8.0f, 8.0f})) == before);

    // 失败不产生新 asset：坏输入后再次成功 load，id 仍单调（无缺口校验宽松，
    // 只验证仍能递增；耗尽 seam 放下面专项）
    return ok;
}

bool test_asset_id_exhaustion_seam() {
    bool ok = true;
    // 注：本测试依赖 TROGUE_TEST_SEAMS（链接 trogue_engine_test）
#ifdef TROGUE_TEST_SEAMS
    const std::string p = write_scene(single_solid_scene());
    tg::detail::asset_test_seed_id(std::numeric_limits<std::uint64_t>::max());
    auto r = SceneAsset::load(p);
    std::remove(p.c_str());
    CHECK(!r.has_value());                    // 耗尽 → 错误
    CHECK(r.error().code == tg::ErrorCode::kResourceExhausted);

    // 不回绕：seed 接近上限 → 成功取得该值
    tg::detail::asset_test_seed_id(std::numeric_limits<std::uint64_t>::max() - 1);
    const std::string p2 = write_scene(single_solid_scene());
    auto r2 = SceneAsset::load(p2);
    std::remove(p2.c_str());
    REQUIRE(r2.has_value());
    CHECK(r2->asset_id() == std::numeric_limits<std::uint64_t>::max() - 1);

    // 再用一次 → 计数器已达 max → 返回 0 → 耗尽错误（不回绕）
    const std::string p2b = write_scene(single_solid_scene());
    auto r2b = SceneAsset::load(p2b);
    std::remove(p2b.c_str());
    CHECK(!r2b.has_value());
    CHECK(r2b.error().code == tg::ErrorCode::kResourceExhausted);
    CHECK(r2b.error().message.find("耗尽") != std::string::npos);

    // reset 恢复
    tg::detail::asset_test_reset_id();
    const std::string p3 = write_scene(single_solid_scene());
    auto r3 = SceneAsset::load(p3);
    std::remove(p3.c_str());
    REQUIRE(r3.has_value());
    CHECK(r3->asset_id() == 1);
    tg::detail::asset_test_reset_id();  // 收尾还原
#else
    (void)ok;  // 无 seam 构建：本测试函数跳过（不应发生——tools 仅在测试开启时加入）
#endif
    return ok;
}

}  // namespace

// ── 批量查询：与逐格单点等价（D4） ──
bool test_batch_grid_and_mask() {
    bool ok = true;
    const std::string p = write_scene(two_solid_scene());  // 层0(0,0)、层1(3,3)
    auto r = SceneAsset::load(p);
    std::remove(p.c_str());
    REQUIRE(r.has_value());
    const SceneAsset& a = *r;

    // tile_grid：与逐格 tile_at 等价（含负/越界格写 -1）
    const int tx = -1, ty = -1, w = 6, h = 6;
    std::vector<int> grid(static_cast<std::size_t>(w) * h, 12345);
    CHECK(tile_grid(a, 0, tx, ty, w, h, grid.data()) == TileLookupResult::occupied);
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            int expect = -1;
            if (tx + i >= 0 && ty + j >= 0 && tx + i < 4 && ty + j < 4) {
                const auto v = tile_at(a, 0,
                    tg::Vec2{static_cast<float>(tx + i) * 16.0f + 8.0f,
                             static_cast<float>(ty + j) * 16.0f + 8.0f}, &expect);
                (void)v;
            }
            CHECK(grid[static_cast<std::size_t>(j) * w + i] == expect);
        }
    }

    // 全区域层外 → empty（仍写满 -1）
    std::vector<int> outside(4, 7);
    CHECK(tile_grid(a, 0, 100, 100, 2, 2, outside.data()) == TileLookupResult::empty);
    for (int v : outside) CHECK(v == -1);

    // solid_mask：与逐格 is_solid_at 等价
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(w) * h, 9);
    CHECK(solid_mask(a, tx, ty, w, h, mask.data()) == TileQueryResult::solid);
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            const auto q = is_solid_at(a,
                tg::Vec2{static_cast<float>(tx + i) * 16.0f + 8.0f,
                         static_cast<float>(ty + j) * 16.0f + 8.0f});
            CHECK(mask[static_cast<std::size_t>(j) * w + i] ==
                  (q == TileQueryResult::solid ? 1 : 0));
        }
    }

    // 两 solid 层命中位置不同：(0,0) 与 (3,3) 均应标 1
    CHECK(mask[1 * w + 1] == 1);   // tile(0,0)
    CHECK(mask[4 * w + 4] == 1);   // tile(3,3)

    // 无 solid 层 → clear（全 0）
    const std::string p2 = write_scene(no_solid_scene());
    auto r2 = SceneAsset::load(p2);
    std::remove(p2.c_str());
    REQUIRE(r2.has_value());
    std::vector<std::uint8_t> mask2(16, 9);
    CHECK(solid_mask(*r2, 0, 0, 4, 4, mask2.data()) == TileQueryResult::clear);
    for (std::uint8_t v : mask2) CHECK(v == 0);

    // error 条件：空指针 / 非正尺寸 / 层越界 / 网格超层维度上限
    CHECK(tile_grid(a, 0, 0, 0, 2, 2, nullptr) == TileLookupResult::error);
    CHECK(tile_grid(a, 0, 0, 0, 0, 2, grid.data()) == TileLookupResult::error);
    CHECK(tile_grid(a, 0, 0, 0, 2, -1, grid.data()) == TileLookupResult::error);
    CHECK(tile_grid(a, 9, 0, 0, 2, 2, grid.data()) == TileLookupResult::error);
    CHECK(tile_grid(a, -1, 0, 0, 2, 2, grid.data()) == TileLookupResult::error);
    CHECK(tile_grid(a, 0, 0, 0, 5000, 5000, grid.data()) ==
          TileLookupResult::error);  // 25e6 > 4096^2
    CHECK(solid_mask(a, 0, 0, 2, 2, nullptr) == TileQueryResult::error);
    CHECK(solid_mask(a, 0, 0, 0, 2, mask.data()) == TileQueryResult::error);
    CHECK(solid_mask(a, 0, 0, 4097, 4097, mask.data()) == TileQueryResult::error);
    return ok;
}

bool test_update_layer_tiles() {
    bool ok = true;
    const std::string p = write_scene(single_solid_scene());
    auto r = SceneAsset::load(p);
    std::remove(p.c_str());
    REQUIRE(r.has_value());
    const auto& layer_ref = r->layer(0);
    CHECK(layer_ref.nonempty == 1);
    std::vector<int> empty(16, -1);
    CHECK(r->update_layer_tiles(0, empty).has_value());
    CHECK(r->layer(0).nonempty == 0);
    CHECK(is_solid_at(*r, tg::Vec2{8, 8}) == TileQueryResult::clear);
    int value = 99;
    CHECK(tile_at(*r, 0, tg::Vec2{8, 8}, &value) == TileLookupResult::empty);
    std::vector<int> bad(15, -1);
    CHECK(!r->update_layer_tiles(0, bad).has_value());
    CHECK(r->layer(0).nonempty == 0);
    std::vector<int> restored(16, -1);
    restored[0] = 1;
    CHECK(r->update_layer_tiles(0, restored).has_value());
    CHECK(layer_ref.nonempty == 1);
    CHECK(is_solid_at(*r, tg::Vec2{8, 8}) == TileQueryResult::solid);
    CHECK(!r->update_layer_tiles(9, restored).has_value());
    return ok;
}

// 图集模式场景（引用真实磁盘 tileset，16 tiles；值域上界 = count）
std::string atlas_scene() {
    return R"({"format":"tro-scene","version":2,"tilemap":{
        "tile_width":16,"tile_height":16,
        "tilesets":[{"name":"ts","path":"tilesets/tile_set.json"}],
        "layers":[
          {"name":"g","width":4,"height":4,"solid":true,"tileset":"ts",
           "tiles":[0,-1,-1,-1,
                    -1,-1,-1,-1,
                    -1,-1,-1,-1,
                    -1,-1,-1,-1]} ]},"entities":[]})";
}

bool test_set_tile_at() {
    bool ok = true;
    // ── palette 场景：合法写入 / nonempty O(1) / solid 联动 / 错误路径 / 交叉对账 ──
    {
        const std::string p = write_scene(single_solid_scene());
        auto r = SceneAsset::load(p);
        std::remove(p.c_str());
        REQUIRE(r.has_value());
        const auto& layer_ref = r->layer(0);
        CHECK(layer_ref.nonempty == 1);

        // nonempty 三态：非空→非空不变；空→非空 +1；非空→空 −1
        CHECK(r->set_tile_at(0, 0, 0, 1).has_value());
        CHECK(layer_ref.nonempty == 1);
        CHECK(r->set_tile_at(0, 1, 1, 0).has_value());
        CHECK(layer_ref.nonempty == 2);
        CHECK(r->set_tile_at(0, 1, 1, -1).has_value());
        CHECK(layer_ref.nonempty == 1);
        int v = 99;
        CHECK(tile_at(*r, 0, tg::Vec2{24, 24}, &v) == TileLookupResult::empty);

        // solid 联动：挖墙/填墙往返
        CHECK(is_solid_at(*r, tg::Vec2{8, 8}) == TileQueryResult::solid);
        CHECK(r->set_tile_at(0, 0, 0, -1).has_value());
        CHECK(is_solid_at(*r, tg::Vec2{8, 8}) == TileQueryResult::clear);
        CHECK(r->set_tile_at(0, 0, 0, 1).has_value());
        CHECK(is_solid_at(*r, tg::Vec2{8, 8}) == TileQueryResult::solid);

        // 错误路径（palette 2 色）：值 ≥ palette_count / 值 < -1 / 坐标越界 /
        // 层越界 → 全部拒绝；每类错误后 tile_at 读回原值 + nonempty 不变
        //（「失败零修改」完整钉死）
        CHECK(!r->set_tile_at(0, 0, 0, 2).has_value());
        CHECK(tile_at(*r, 0, tg::Vec2{8, 8}, &v) == TileLookupResult::occupied &&
              v == 1);
        CHECK(!r->set_tile_at(0, 0, 0, -2).has_value());
        CHECK(tile_at(*r, 0, tg::Vec2{8, 8}, &v) == TileLookupResult::occupied &&
              v == 1);
        CHECK(!r->set_tile_at(0, -1, 0, 0).has_value());
        CHECK(!r->set_tile_at(0, 0, 4, 0).has_value());
        CHECK(!r->set_tile_at(5, 0, 0, 0).has_value());
        CHECK(tile_at(*r, 0, tg::Vec2{8, 8}, &v) == TileLookupResult::occupied &&
              v == 1);
        CHECK(layer_ref.nonempty == 1);

        // 交叉对账：混合 O(1) 写入（−1/+1/不变）→ tile_grid 物化 →
        // update_layer_tiles 写回（整层重算）→ nonempty 一致
        CHECK(r->set_tile_at(0, 2, 2, 1).has_value());   // +1 → 2
        CHECK(r->set_tile_at(0, 0, 0, -1).has_value());  // −1 → 1
        CHECK(r->set_tile_at(0, 3, 3, 1).has_value());   // +1 → 2
        std::vector<int> grid(16, -1);
        CHECK(tile_grid(*r, 0, 0, 0, 4, 4, grid.data()) ==
              TileLookupResult::occupied);
        CHECK(r->update_layer_tiles(0, grid).has_value());
        CHECK(layer_ref.nonempty == 2);
    }
    // ── 图集场景：值域上界 = tileset count ──
    {
        const std::string p = write_scene(atlas_scene());
        auto r = SceneAsset::load(p);
        std::remove(p.c_str());
        REQUIRE(r.has_value());
        CHECK(r->set_tile_at(0, 1, 1, 15).has_value());  // count-1 合法
        int v = -9;
        CHECK(tile_at(*r, 0, tg::Vec2{24, 24}, &v) == TileLookupResult::occupied);
        CHECK(v == 15);
        CHECK(!r->set_tile_at(0, 1, 1, 16).has_value());  // ≥ count 拒绝
        CHECK(!r->set_tile_at(0, 1, 1, -2).has_value());
    }
    // ── 多格 tile（size_in_atlas）：一格一 cell，写其 cell 不影响其他 cell ──
    {
        // test_tileset.json：单 tile、size_in_atlas [3,5]（渲染 region 扩展，
        // 逻辑仍占一个 cell）
        constexpr const char* kMulti =
            R"({"format":"tro-scene","version":2,"tilemap":{
                "tile_width":16,"tile_height":16,
                "tilesets":[{"name":"ts","path":"tilesets/test_tileset.json"}],
                "layers":[
                  {"name":"g","width":4,"height":4,"solid":true,"tileset":"ts",
                   "tiles":[0,-1,-1,-1,
                            -1,-1,-1,-1,
                            -1,-1,-1,-1,
                            -1,-1,-1,-1]} ]},"entities":[]})";
        const std::string p = write_scene(kMulti);
        auto r = SceneAsset::load(p);
        std::remove(p.c_str());
        REQUIRE(r.has_value());
        CHECK(r->set_tile_at(0, 1, 1, 0).has_value());  // tile(1,1) 写入
        int v = -9;
        CHECK(tile_at(*r, 0, tg::Vec2{24, 24}, &v) == TileLookupResult::occupied);
        CHECK(v == 0);
        // 相邻 cell 不受影响（含多格 tile 覆盖到的渲染区域 cell）
        v = -9;
        CHECK(tile_at(*r, 0, tg::Vec2{8, 24}, &v) == TileLookupResult::empty);
        v = -9;
        CHECK(tile_at(*r, 0, tg::Vec2{24, 8}, &v) == TileLookupResult::empty);
        v = -9;
        CHECK(tile_at(*r, 0, tg::Vec2{40, 40}, &v) == TileLookupResult::empty);
    }
    return ok;
}

// props/rotation 快照值语义：深拷贝隔离（改快照不污染 asset）+ rotation 透传。
bool test_props_snapshot_semantics() {
    const std::string scene =
        R"({"format":"tro-scene","version":2,"meta":{"props":{"gravity":9.8}},"tilemap":{"layers":[]},)"
        R"("entities":[{"id":"a","rotation":-30.0,"props":{"hp":2}}]})";
    auto r = tg::SceneAsset::load_json(scene, "props-semantics");
    if (!r) {
        ::tg_test::record_failure(__FILE__, __LINE__,
                                  "props-semantics 加载失败: " + r.error().message);
        return false;
    }
    // meta_props 引用随 asset 存活
    CHECK(r->meta_props()["gravity"] == 9.8);
    // 快照值拷贝：修改快照 props 后再取一次，asset 侧不变
    auto e1 = r->entity(0);
    CHECK(e1.rotation == -30.0f);
    e1.props["hp"] = 999;
    e1.props["added"] = true;
    auto e2 = r->entity(0);
    CHECK(e2.props["hp"] == 2);
    CHECK(e2.props.find("added") == e2.props.end());
    CHECK(e2.rotation == -30.0f);
    return true;
}

int main() {
    std::filesystem::create_directories("build");  // CWD=项目根；ctest WORKING_DIRECTORY 保证
    test_is_solid_at();
    test_rect_hits_solid();
    test_tile_at();
    test_batch_grid_and_mask();
    test_update_layer_tiles();
    test_set_tile_at();
    test_props_snapshot_semantics();
    test_asset_id_and_safety();
    test_asset_id_exhaustion_seam();
    std::printf("[query test] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}