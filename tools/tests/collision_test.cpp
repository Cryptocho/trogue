// collision_test.cpp —— 碰撞几何原语单测（纯公共 API，无窗口）。
//
// 覆盖：aabb_overlap 半开/退化；segment_hits_solid 的闭区间、supercover 对角线、
// 多 solid 层（不同 origin）、层外、error；sweep_move 的暴力对照（0.05px 步进
// rect_hits_solid，复刻「先 X 后 Y」轴序）、接触语义、沿墙滑动、多 origin、层外、
// 大位移不穿透、退化/非法、确定性。
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>  // getpid（临时文件跨进程隔离）

#include "trogue/trogue.hpp"
#include "test_util.hpp"

namespace {

using tg::Rect;
using tg::SceneAsset;
using tg::Vec2;

int g_seq = 0;
std::string write_scene(const std::string& content) {
    const std::string path = "build/tmp_collision_" + std::to_string(getpid()) +
                             "_" + std::to_string(g_seq++) + ".json";
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();
    return path;
}

// 8×8 tile、16px 单 solid 层：横向墙在 ty=4 全行实心；垂直墙在 col6 的 ty∈[4,8)。
std::string wall_scene() {
    return R"({"format":"tro-scene","version":2,"tilemap":{
        "tile_width":16,"tile_height":16,
        "palette":["#2a2d3a","#7f8ca3"],
        "layers":[
          {"name":"g","width":8,"height":8,"solid":true,
           "tiles":[-1,-1,-1,-1,-1,-1,-1,-1,
                    -1,-1,-1,-1,-1,-1,-1,-1,
                    -1,-1,-1,-1,-1,-1,-1,-1,
                    -1,-1,-1,-1,-1,-1,-1,-1,
                     1, 1, 1, 1, 1, 1, 1, 1,
                    -1,-1,-1,-1,-1,-1, 1,-1,
                    -1,-1,-1,-1,-1,-1, 1,-1,
                    -1,-1,-1,-1,-1,-1, 1,-1]} ]},"entities":[]})";
}

// 单实心格 (0,0) 的 4×4 场景。
std::string corner_scene() {
    return R"({"format":"tro-scene","version":2,"tilemap":{
        "tile_width":16,"tile_height":16,
        "palette":["#2a2d3a","#7f8ca3"],
        "layers":[
          {"name":"g","width":4,"height":4,"solid":true,
           "tiles":[1,-1,-1,-1,  -1,-1,-1,-1,  -1,-1,-1,-1,  -1,-1,-1,-1]} ]},
        "entities":[]})";
}

// 单格实心场景（w×h，仅 (sx,sy) 实心）；用于层外/边界与不变式回归。
std::string cell_only_scene(int w, int h, int sx, int sy) {
    std::string tiles;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!tiles.empty()) tiles += ",";
            tiles += (x == sx && y == sy) ? "1" : "-1";
        }
    return std::string(R"({"format":"tro-scene","version":2,"tilemap":{)") +
           R"("tile_width":16,"tile_height":16,)" +
           R"("palette":["#2a2d3a","#7f8ca3"],)" +
           R"("layers":[{"name":"g","width":)" + std::to_string(w) +
           R"(,"height":)" + std::to_string(h) +
           R"(,"solid":true,"tiles":[)" + tiles + R"(]}]},"entities":[]})";
}

// 两层不同 origin：层0 (1,1)@origin(0,0) → 世界 [16,32)×[16,32)；
// 层1 (2,2)@origin(8,8) → 世界 [40,56)×[40,56)。
std::string two_origin_scene() {
    return R"({"format":"tro-scene","version":2,"tilemap":{
        "tile_width":16,"tile_height":16,
        "palette":["#2a2d3a","#7f8ca3"],
        "layers":[
          {"name":"a","width":4,"height":4,"solid":true,"origin":[0,0],
           "tiles":[-1,-1,-1,-1, -1,1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1]},
          {"name":"b","width":4,"height":4,"solid":true,"origin":[8,8],
           "tiles":[-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,1,-1, -1,-1,-1,-1]} ]},
        "entities":[]})";
}

// 无 solid 层场景（层内全实心但 solid=false）。
std::string no_solid_scene() {
    return R"({"format":"tro-scene","version":2,"tilemap":{
        "tile_width":16,"tile_height":16,
        "palette":["#2a2d3a","#7f8ca3"],
        "layers":[
          {"name":"g","width":4,"height":4,"solid":false,
           "tiles":[1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1]} ]},"entities":[]})";
}

// 承装 scene + 其临时文件路径（析构删文件）。SceneAsset 无默认构造，故用 optional。
struct LoadedScene {
    std::optional<tg::SceneAsset> asset;
    std::string path;
    ~LoadedScene() {
        if (!path.empty()) std::remove(path.c_str());
    }
};

bool load(const std::string& json, LoadedScene& out) {
    out.path = write_scene(json);
    auto r = tg::SceneAsset::load(out.path);
    if (!r) return false;
    out.asset = std::move(*r);
    return true;
}

// ── aabb_overlap ──
bool test_aabb() {
    using tg::aabb_overlap;
    REQUIRE(aabb_overlap(Rect{0, 0, 10, 10}, Rect{20, 0, 10, 10}) == false);
    REQUIRE(aabb_overlap(Rect{0, 0, 10, 10}, Rect{5, 5, 10, 10}) == true);
    // 仅边界相接 → false（半开）
    REQUIRE(aabb_overlap(Rect{0, 0, 10, 10}, Rect{10, 0, 10, 10}) == false);
    REQUIRE(aabb_overlap(Rect{0, 0, 10, 10}, Rect{0, 10, 10, 10}) == false);
    // 相接再进 0.001 → true
    REQUIRE(aabb_overlap(Rect{0, 0, 10, 10}, Rect{9.999f, 0, 10, 10}) == true);
    // 包含、相同、退化
    REQUIRE(aabb_overlap(Rect{0, 0, 20, 20}, Rect{5, 5, 5, 5}) == true);
    REQUIRE(aabb_overlap(Rect{0, 0, 10, 10}, Rect{0, 0, 10, 10}) == true);
    REQUIRE(aabb_overlap(Rect{0, 0, 0, 10}, Rect{0, 0, 10, 10}) == false);
    REQUIRE(aabb_overlap(Rect{0, 0, 10, 10}, Rect{0, 0, 10, -1}) == false);
    return true;
}

// ── segment_hits_solid ──
bool test_segment() {
    using tg::TileQueryResult;
    LoadedScene s;
    REQUIRE(load(wall_scene(), s));
    const SceneAsset& a = *s.asset;

    // 水平线段沿 row4（墙）从 x=0 起 → t=0 命中 (0,4)
    auto r = tg::segment_hits_solid(a, Vec2{0, 72}, Vec2{127, 72});
    REQUIRE(r.result == TileQueryResult::solid);
    CHECK(r.layer == 0);
    CHECK(r.ty == 4);
    CHECK(r.tx == 0);
    CHECK(std::fabs(r.t) < 1e-6f);  // 起点即在墙格
    CHECK(std::fabs(r.point.x) < 1e-3f);

    // 空行水平 → clear
    r = tg::segment_hits_solid(a, Vec2{0, 24}, Vec2{127, 24});
    CHECK(r.result == TileQueryResult::clear);

    // 垂直穿墙（x=8 在 col0）：从 (8,0) 到 (8,127) → 命中 row4 顶部边界
    r = tg::segment_hits_solid(a, Vec2{8, 0}, Vec2{8, 127});
    REQUIRE(r.result == TileQueryResult::solid);
    CHECK(r.tx == 0);
    CHECK(r.ty == 4);
    CHECK(std::fabs(r.t - (64.0f / 127.0f)) < 1e-5f);
    CHECK(std::fabs(r.point.y - 64.0f) < 1e-3f);

    // 起点在 solid → t==0（闭区间）
    r = tg::segment_hits_solid(a, Vec2{8, 72}, Vec2{127, 72});
    REQUIRE(r.result == TileQueryResult::solid);
    CHECK(std::fabs(r.t) < 1e-6f);

    // 终点在 solid → 命中（闭区间）：(0,24)→(8,72) 终点落 row4
    r = tg::segment_hits_solid(a, Vec2{0, 24}, Vec2{8, 72});
    CHECK(r.result == TileQueryResult::solid);

    // 多层不同 origin：斜穿 two_origin：从 (0,0) 到 (63,63)
    LoadedScene s2;
    REQUIRE(load(two_origin_scene(), s2));
    r = tg::segment_hits_solid(*s2.asset, Vec2{0, 0}, Vec2{63, 63});
    CHECK(r.result == TileQueryResult::solid);

    // 层外线段 → clear
    r = tg::segment_hits_solid(a, Vec2{-100, -100}, Vec2{-50, -50});
    CHECK(r.result == TileQueryResult::clear);

    // 非有限 → error
    const float nan_v = std::nanf("");
    r = tg::segment_hits_solid(a, Vec2{nan_v, 0}, Vec2{10, 10});
    CHECK(r.result == TileQueryResult::error);

    // supercover 对角线：corner_scene (0,0) 实心，从 (0.5,0.5)→(63.5,63.5)
    LoadedScene s3;
    REQUIRE(load(corner_scene(), s3));
    r = tg::segment_hits_solid(*s3.asset, Vec2{0.5f, 0.5f}, Vec2{63.5f, 63.5f});
    REQUIRE(r.result == TileQueryResult::solid);
    CHECK(r.tx == 0 && r.ty == 0);
    CHECK(std::fabs(r.t) < 1e-6f);

    // 反对角（避开 (0,0)）→ clear
    r = tg::segment_hits_solid(*s3.asset, Vec2{63.5f, 0.5f}, Vec2{0.5f, 63.5f});
    CHECK(r.result == TileQueryResult::clear);

    // 端点恰在 solid 半开边界上：corner_scene (0,0) 实心即世界 [0,16)²。
    // 终点 (16,8) 按半开不属 (0,0)（is_solid_at 为 clear），故不命中。
    r = tg::segment_hits_solid(*s3.asset, Vec2{40, 8}, Vec2{16, 8});
    CHECK(r.result == TileQueryResult::clear);
    CHECK(tg::is_solid_at(*s3.asset, Vec2{16, 8}) == TileQueryResult::clear);
    // 终点再进 1px（在半开区间内）→ 命中
    r = tg::segment_hits_solid(*s3.asset, Vec2{40, 8}, Vec2{15, 8});
    CHECK(r.result == TileQueryResult::solid);
    // 终点在 solid 内部（闭区间）→ 命中
    r = tg::segment_hits_solid(*s3.asset, Vec2{40, 8}, Vec2{8, 8});
    CHECK(r.result == TileQueryResult::solid);
    // 非对角线上与 solid 仅边界相切（半开）→ 不命中
    r = tg::segment_hits_solid(*s3.asset, Vec2{16, 8}, Vec2{40, 8});
    CHECK(r.result == TileQueryResult::clear);

    // 命中 t 必在 [0,1] 且命中点在线段 bbox 内（回归：曾经在层外起点被夹进
    // 边缘格后 t 越界、报出不在线段上的命中）。6×6 仅 (3,1) 实心，线段与
    // 该格完全不相交。
    {
        LoadedScene s4;
        REQUIRE(load(cell_only_scene(6, 6, 3, 1), s4));
        auto h = tg::segment_hits_solid(*s4.asset, Vec2{0, 144}, Vec2{32, 64});
        CHECK(h.result == TileQueryResult::clear);
        if (h.result == TileQueryResult::solid) {
            CHECK(h.t >= 0.0f && h.t <= 1.0f);
            CHECK(h.point.x >= 0.0f && h.point.x <= 32.0f);
            CHECK(h.point.y >= 64.0f && h.point.y <= 144.0f);
        }
        // 端点恰停在层远边界、层内无正长度交集（2×2 仅 (1,1)）→ clear
        {
            LoadedScene s5;
            REQUIRE(load(cell_only_scene(2, 2, 1, 1), s5));
            auto h = tg::segment_hits_solid(*s5.asset, Vec2{24, 40}, Vec2{24, 32});
            CHECK(h.result == TileQueryResult::clear);
            auto h2 = tg::segment_hits_solid(*s5.asset, Vec2{40, 24}, Vec2{32, 24});
            CHECK(h2.result == TileQueryResult::clear);
            // 但确有正长度穿越层内 → 命中
            auto h3 = tg::segment_hits_solid(*s5.asset, Vec2{24, 31.9999f}, Vec2{24, 20});
            CHECK(h3.result == TileQueryResult::solid);
        }

        // 层远边界上的零长度点 = is_solid_at 同判（4×4 仅 (3,3) 实心）
        auto h2 = tg::segment_hits_solid(*s4.asset, Vec2{96, 96}, Vec2{96, 96});
        CHECK(h2.result == TileQueryResult::clear);
        CHECK(tg::is_solid_at(*s4.asset, Vec2{96, 96}) == TileQueryResult::clear);
        // 反向：实心格中心处的零长度点 → 命中 t=0
        auto h3 = tg::segment_hits_solid(*s4.asset, Vec2{56, 24}, Vec2{56, 24});
        REQUIRE(h3.result == TileQueryResult::solid);
        CHECK(h3.tx == 3 && h3.ty == 1);
        CHECK(std::fabs(h3.t) < 1e-6f);
    }

    return true;
}

// 参考：沿单轴 0.05px 步进，返回首个 rect_hits_solid 前的位置（is_x 时返回
// 新的 x，否则返回新的 y）。调用方按「先 X（原始 y）后 Y（解算后 x）」组合。
double reference_axis(const SceneAsset& asset, double pos, double other,
                      double size, double other_size, double delta, bool is_x) {
    const double sign = delta > 0 ? 1.0 : -1.0;
    const double absd = std::fabs(delta);
    const double step = 0.05;
    double best = 0.0;
    const int n = static_cast<int>(std::ceil(absd / step));
    for (int k = 1; k <= n; ++k) {
        double t = step * k;
        if (t > absd) t = absd;
        const double np = pos + sign * t;
        const Rect r =
            is_x ? Rect{static_cast<float>(np), static_cast<float>(other),
                        static_cast<float>(size), static_cast<float>(other_size)}
                 : Rect{static_cast<float>(other), static_cast<float>(np),
                        static_cast<float>(other_size), static_cast<float>(size)};
        if (tg::rect_hits_solid(asset, r) == tg::TileQueryResult::solid) break;
        best = t;
    }
    return pos + sign * best;
}

// ── sweep 暴力对照 ──
bool test_sweep_bruteforce() {
    LoadedScene s;
    REQUIRE(load(wall_scene(), s));
    const SceneAsset& a = *s.asset;

    const float boxes[][4] = {
        {0, 0, 12, 12},  {20, 40, 16, 16}, {40, 56, 10, 20},
        {0, 40, 16, 20}, {90, 10, 14, 14}, {100, 100, 12, 12},
    };
    const float deltas[][2] = {
        {100, 0}, {-100, 0}, {0, 100},  {0, -100}, {60, 60},
        {-60, 60}, {60, -60}, {-60, -60}, {37, 11}, {-23, -41},
    };
    int compared = 0;
    for (const auto& bx : boxes) {
        for (const auto& dl : deltas) {
            const Rect box{bx[0], bx[1], bx[2], bx[3]};
            if (tg::rect_hits_solid(a, box) == tg::TileQueryResult::solid)
                continue;  // 起始不重叠（调用方不变式）
            const Vec2 delta{dl[0], dl[1]};
            const auto got = tg::sweep_move(a, box, delta);

            const double rx = reference_axis(a, box.x, box.y, box.w, box.h,
                                             delta.x, true);
            const double ry = reference_axis(a, box.y, rx, box.h, box.w,
                                             delta.y, false);
            ++compared;

            const double e_x = static_cast<double>(got.box.x) - rx;
            const double e_y = static_cast<double>(got.box.y) - ry;
            // 引擎停在接触位置：方向同号且不超过参考 + 步长
            if (!(e_x >= -1e-3 && e_x <= 0.05 + 1e-2)) {
                std::fprintf(stderr,
                             "  sweep X box(%.0f,%.0f,%.0f,%.0f) d(%.0f,%.0f): "
                             "got %.4f ref %.4f\n",
                             box.x, box.y, box.w, box.h, delta.x, delta.y,
                             got.box.x, rx);
                CHECK(false);
            } else {
                CHECK(true);
            }
            if (!(e_y >= -1e-3 && e_y <= 0.05 + 1e-2)) {
                std::fprintf(stderr,
                             "  sweep Y box(%.0f,%.0f,%.0f,%.0f) d(%.0f,%.0f): "
                             "got %.4f ref %.4f\n",
                             box.x, box.y, box.w, box.h, delta.x, delta.y,
                             got.box.y, ry);
                CHECK(false);
            } else {
                CHECK(true);
            }
            // 核心不变式
            CHECK(tg::rect_hits_solid(a, got.box) != tg::TileQueryResult::solid);
        }
    }
    CHECK(compared > 20);  // 实际比较组数（部分夹具因起始即重叠被跳过）
    std::printf("[collision_test] sweep 暴力对照比较组数=%d\n", compared);
    return true;
}

// ── sweep 语义 ──
bool test_sweep_semantics() {
    using tg::TileQueryResult;
    LoadedScene s;
    REQUIRE(load(wall_scene(), s));
    const SceneAsset& a = *s.asset;

    // 1) 纯下移撞 row4 顶：前缘停在 y=64 → box.y=48；再走 1px 即阻挡
    {
        const auto r = tg::sweep_move(a, Rect{0, 0, 16, 16}, Vec2{0, 100});
        REQUIRE(r.result == TileQueryResult::solid);
        CHECK(r.blocked_y == true);
        CHECK(r.blocked_x == false);
        CHECK(std::fabs(r.box.y - 48.0f) < 1e-3f);
        CHECK(std::fabs(r.box.x) < 1e-6f);
        CHECK(tg::rect_hits_solid(a, Rect{0, r.box.y + 1.0f, 16, 16}) ==
              TileQueryResult::solid);
    }

    // 2) 全量位移（无阻挡）
    {
        const auto r = tg::sweep_move(a, Rect{0, 0, 16, 16}, Vec2{-50, -50});
        CHECK(r.result == TileQueryResult::clear);
        CHECK(r.blocked_x == false && r.blocked_y == false);
        CHECK(std::fabs(r.box.x + 50.0f) < 1e-3f);
        CHECK(std::fabs(r.box.y + 50.0f) < 1e-3f);
    }

    // 3) 沿墙滑动：斜向右下。X 全量（row0 无横向墙）→ x=100（跨 col6-7）；
    //    Y：col6 在 ty4..7 solid → 前缘停在 y=64 → box.y=48。
    {
        const auto r = tg::sweep_move(a, Rect{0, 0, 16, 16}, Vec2{100, 100});
        CHECK(r.blocked_x == false);
        CHECK(r.blocked_y == true);
        CHECK(std::fabs(r.box.y - 48.0f) < 1e-3f);
        CHECK(std::fabs(r.box.x - 100.0f) < 1e-3f);
    }

    // 4) 大位移不穿透（向左穿出地图）
    {
        const auto r = tg::sweep_move(a, Rect{120, 8, 8, 8}, Vec2{-500, 0});
        CHECK(r.result == TileQueryResult::clear);
        CHECK(std::fabs(r.box.x - (120.0f - 500.0f)) < 1e-2f);
    }

    // 5) 多 solid 层不同 origin 取最紧约束：层1 的 (2,2)@(8,8) → 世界 [40,56)²
    {
        LoadedScene s2;
        REQUIRE(load(two_origin_scene(), s2));
        const auto r = tg::sweep_move(*s2.asset, Rect{0, 40, 16, 16}, Vec2{100, 0});
        CHECK(r.result == TileQueryResult::solid);
        CHECK(r.blocked_x == true);
        CHECK(std::fabs(r.box.x - 24.0f) < 1e-3f);  // 前缘停 40
    }

    // 6) 无 solid 层 → 整体位移
    {
        LoadedScene s2;
        REQUIRE(load(no_solid_scene(), s2));
        const auto r = tg::sweep_move(*s2.asset, Rect{0, 0, 8, 8}, Vec2{100, 100});
        CHECK(r.result == TileQueryResult::clear);
        CHECK(std::fabs(r.box.x - 100.0f) < 1e-3f);
    }

    // 7) 退化/非法 → error 且 box == 入参
    {
        const auto r0 = tg::sweep_move(a, Rect{5, 5, 0, 10}, Vec2{10, 10});
        CHECK(r0.result == TileQueryResult::error);
        CHECK((r0.box == Rect{5, 5, 0, 10}));
        const float nan_v = std::nanf("");
        const auto r1 = tg::sweep_move(a, Rect{5, 5, 10, 10}, Vec2{nan_v, 0});
        CHECK(r1.result == TileQueryResult::error);
        CHECK((r1.box == Rect{5, 5, 10, 10}));
    }

    // 8) 确定性
    {
        const Rect box{4, 4, 12, 12};
        const Vec2 d{73, 91};
        const auto r1 = tg::sweep_move(a, box, d);
        const auto r2 = tg::sweep_move(a, box, d);
        CHECK(r1.box == r2.box);
        CHECK(r1.blocked_x == r2.blocked_x);
        CHECK(r1.blocked_y == r2.blocked_y);
        CHECK(r1.result == r2.result);
    }

    // 9) 「极大但有限」坐标：不得因 double→整数转换溢出而假阻挡
    //（远在层外的 box 不应被任何 solid 阻挡）。
    {
        const auto r = tg::sweep_move(a, Rect{1.5e20f, 0, 16, 16}, Vec2{1, 0});
        CHECK(r.result == TileQueryResult::clear);
        CHECK(r.blocked_x == false);
        const auto r2 = tg::sweep_move(a, Rect{4, 1e30f, 8, 8}, Vec2{1, 0});
        CHECK(r2.result == TileQueryResult::clear);
        CHECK(r2.blocked_y == false);
    }

    return true;
}

}  // namespace

int main() {
    bool ok = true;
    ok = test_aabb() && ok;
    ok = test_segment() && ok;
    ok = test_sweep_bruteforce() && ok;
    ok = test_sweep_semantics() && ok;
    std::printf("[collision_test] checks=%d failures=%d\n", tg_test::g_checks,
                tg_test::g_failures);
    if (tg_test::g_failures == 0 && ok) {
        std::printf("[collision_test] OK\n");
        return 0;
    }
    std::printf("[collision_test] FAILED\n");
    return 1;
}
