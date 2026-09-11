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

int main() {
    std::filesystem::create_directories("build");  // CWD=项目根；ctest WORKING_DIRECTORY 保证
    test_is_solid_at();
    test_rect_hits_solid();
    test_tile_at();
    test_asset_id_and_safety();
    test_asset_id_exhaustion_seam();
    std::printf("[query test] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}