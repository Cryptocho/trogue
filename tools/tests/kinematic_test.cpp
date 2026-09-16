// kinematic_test.cpp —— 运动学/脱出/混合扫掠/SolidGrid 单测（纯公共 API，无窗口）。
//
// 覆盖：kinematic_step 的事件派生（landed 差分、探墙内缩、hit_ceiling/hit_wall）、
// 单向平台规则（仅下落/ε 容差/==不隧道化/先 X 后 Y/贴台停位/已在内不阻挡/
// 探地纳入探墙排除/静态取最紧/空数组逐位回归）、resolve_overlap（暴力对照、
// 轴 tie 取 X、非法入参）、sweep_move_mixed（最紧约束、hit_dyn 索引、空数组
// 等价）、SolidGrid（load/refresh 非 solid error、set_tile 失败零修改与同步）。

#include <cmath>
#include <cstdint>
#include <string>
#include <cstdio>
#include <limits>
#include <vector>

#include "trogue/trogue.hpp"
#include "test_util.hpp"

namespace {

using tg::DynBox;
using tg::kEpsilon;
using tg::KinematicResult;
using tg::Rect;
using tg::SceneAsset;
using tg::SolidGridView;
using tg::TileQueryResult;
using tg::Vec2;

// 直连掩码视图：8×8、16px。mask 由调用方填充（1 = 阻挡）。
struct Grid {
    std::vector<std::uint8_t> mask;
    SolidGridView view;
    Grid(int w, int h) : mask(std::size_t(w) * h, 0) {
        view = SolidGridView{w, h, 16, 16, 0, 0, mask.data(), w, 0};
    }
    void fill(int tx, int ty) { mask[std::size_t(ty) * view.width + tx] = 1; }
};

// ════════════════════ kinematic_step：事件派生 ════════════════════

void test_kinematic_events() {
    Grid g(8, 8);
    for (int x = 0; x < 8; ++x) g.fill(x, 4);        // 地面行 ty=4（y 64..80)
    for (int y = 5; y < 8; ++y) g.fill(6, y);        // 竖墙 col6、ty 5..7

    // 下落一步着陆：底部从 64 停在地面顶，grounded+landed。
    const KinematicResult land =
        tg::kinematic_step(&g.view, 1, Rect{32, 32, 16, 16}, Vec2{0, 24});
    CHECK((land.sweep.box == Rect{32, 48, 16, 16}));
    CHECK(land.sweep.blocked_y);
    CHECK(land.ev.grounded);
    CHECK(land.ev.landed);
    CHECK(!land.ev.hit_ceiling);
    CHECK(!land.ev.hit_wall);
    CHECK(land.ev.wall_dir == 0);

    // 持续站立：不再报 landed。
    const KinematicResult stand =
        tg::kinematic_step(&g.view, 1, Rect{32, 48, 16, 16}, Vec2{0, 1});
    CHECK((stand.sweep.box == Rect{32, 48, 16, 16}));
    CHECK(stand.ev.grounded);
    CHECK(!stand.ev.landed);

    // 原地不动（delta 0）：grounded 但无 landed。
    const KinematicResult idle =
        tg::kinematic_step(&g.view, 1, Rect{32, 48, 16, 16}, Vec2{0, 0});
    CHECK(idle.ev.grounded);
    CHECK(!idle.ev.landed);

    // 头顶：地面下方上跳撞地面底。
    const KinematicResult ceil =
        tg::kinematic_step(&g.view, 1, Rect{32, 80, 16, 16}, Vec2{0, -16});
    CHECK(ceil.sweep.blocked_y);
    CHECK(ceil.ev.hit_ceiling);
    CHECK(!ceil.ev.landed);

    // 贴墙：竖墙右侧（墙 ty5..7）→ wall_dir=+1。
    const KinematicResult wallr =
        tg::kinematic_step(&g.view, 1, Rect{80, 80, 16, 16}, Vec2{0, 0});
    CHECK(wallr.ev.wall_dir == 1);
    CHECK(!wallr.ev.grounded);

    // 探墙内缩判别：底边沉入地面 0.5px、右侧无墙 → 无内缩会误报 ty4 地面
    // 为墙；有内缩（上下各 2px）→ wall_dir=0。
    const KinematicResult sunk =
        tg::kinematic_step(&g.view, 1, Rect{80, 48.5f, 16, 16}, Vec2{0, 0});
    CHECK(sunk.ev.wall_dir == 0);
    CHECK(sunk.ev.grounded);

    // 无墙侧：wall_dir=0。
    const KinematicResult nowall =
        tg::kinematic_step(&g.view, 1, Rect{32, 48, 16, 16}, Vec2{0, 0});
    CHECK(nowall.ev.wall_dir == 0);

    // 横向撞墙 → hit_wall 与 blocked_x 映射。
    {
        Grid hw(8, 8);
        hw.fill(4, 2);
        const KinematicResult r =
            tg::kinematic_step(&hw.view, 1, Rect{48, 32, 16, 16}, Vec2{32, 0});
        CHECK(r.sweep.blocked_x);
        CHECK(r.ev.hit_wall);
        CHECK(r.ev.wall_dir == 1);
    }

    // 右优先：两侧都有墙 → +1。
    Grid both(8, 8);
    for (int x = 0; x < 8; ++x) both.fill(x, 4);
    both.fill(2, 3);
    both.fill(4, 3);
    const KinematicResult bothr =
        tg::kinematic_step(&both.view, 1, Rect{48, 48, 16, 16}, Vec2{0, 0});
    CHECK(bothr.ev.wall_dir == 1);
}

// ════════════════════ 单向平台规则 ════════════════════

void test_one_way() {
    const Rect plat{32, 64, 32, 8};
    Grid g(8, 8);
    for (int x = 0; x < 8; ++x) g.fill(x, 7);  // 底行实心地面（y 112..128)
    const Rect* pw = &plat;
    const int pc = 1;
    // 命中：下落越过平台顶 → 贴台停位、blocked_y、grounded、landed。
    {
        const KinematicResult r =
            tg::kinematic_step(&g.view, 1, pw, pc, Rect{40, 40, 16, 16},
                               Vec2{0, 32});
        CHECK((r.sweep.box == Rect{40, 48, 16, 16}));
        CHECK(r.sweep.blocked_y);
        CHECK(r.ev.grounded);
        CHECK(r.ev.landed);
    }
    // == 不隧道化：delta 恰好使新底边等于平台顶 → 命中。
    {
        const KinematicResult r = tg::kinematic_step(
            &g.view, 1, pw, pc, Rect{40, 40, 16, 16}, Vec2{0, 8});
        CHECK((r.sweep.box == Rect{40, 48, 16, 16}));
        CHECK(r.sweep.blocked_y);
        CHECK(r.ev.grounded);
    }
    // 向上/横向全穿越：不阻挡。
    {
        const KinematicResult up = tg::kinematic_step(
            &g.view, 1, pw, pc, Rect{40, 48, 16, 16}, Vec2{0, -16});
        CHECK(!up.sweep.blocked_y);
        CHECK((up.sweep.box == Rect{40, 32, 16, 16}));
    }
    {
        // 先 X 解算后 x 跨度已离开平台 → 横穿不阻挡（平台 x 32..48）。
        const Rect plat2{32, 64, 16, 8};
        const Rect* pw2 = &plat2;
        const KinematicResult side = tg::kinematic_step(
            &g.view, 1, pw2, pc, Rect{16, 48, 16, 16}, Vec2{32, 16});
        CHECK((side.sweep.box == Rect{48, 64, 16, 16}));
        CHECK(!side.sweep.blocked_y);
    }
    // ε 容差边界：底边 = top+kEpsilon（浮点内略小）→ 仍命中；
    // 底边明显在平台内（> top+kEpsilon）→ 无论位移多大都不阻挡。
    {
        const float top = plat.y + kEpsilon;
        const KinematicResult r =
            tg::kinematic_step(&g.view, 1, pw, pc, Rect{40, top - 16, 16, 16},
                               Vec2{0, 32});
        CHECK(r.sweep.blocked_y);
    }
    {
        const KinematicResult r = tg::kinematic_step(
            &g.view, 1, pw, pc, Rect{40, 64 + 0.01f - 16, 16, 16},
            Vec2{0, 100});
        CHECK(r.sweep.blocked_y);  // 平台不阻挡，但静态地面接住
        CHECK((r.sweep.box == Rect{40, 96, 16, 16}));
    }
    // 下跳穿越：数组摘除（pc=0）→ 同位下落穿过平台落到静态地面。
    {
        const KinematicResult r = tg::kinematic_step(&g.view, 1, pw, 0,
                                                     Rect{40, 40, 16, 16},
                                                     Vec2{0, 100});
        CHECK((r.sweep.box == Rect{40, 96, 16, 16}));
        CHECK(r.ev.grounded);
        CHECK(r.ev.landed);
    }
    // 与静态层取最紧：平台顶(64)高于静态地面(112) → 停平台。
    {
        const KinematicResult r = tg::kinematic_step(
            &g.view, 1, pw, pc, Rect{40, 40, 16, 16}, Vec2{0, 200});
        CHECK((r.sweep.box == Rect{40, 48, 16, 16}));
        CHECK(r.ev.grounded);
    }
    // 静态更紧：平台顶(120)低于静态地面(112) → 停静态地面。
    {
        const Rect low{32, 120, 32, 8};
        const Rect* pl = &low;
        const KinematicResult r = tg::kinematic_step(
            &g.view, 1, pl, pc, Rect{40, 40, 16, 16}, Vec2{0, 200});
        CHECK((r.sweep.box == Rect{40, 96, 16, 16}));
        CHECK(r.sweep.blocked_y);
        CHECK(r.ev.grounded);
    }
    // 探墙不含 one_way：box 侧面贴平台 → wall_dir=0、横向不阻挡。
    {
        const Rect sideplat{96, 48, 16, 16};
        const Rect* ps = &sideplat;
        const KinematicResult r = tg::kinematic_step(&g.view, 1, ps, pc,
                                                     Rect{80, 48, 16, 16},
                                                     Vec2{8, 0});
        CHECK(r.ev.wall_dir == 0);
        CHECK(!r.sweep.blocked_x);
        CHECK((r.sweep.box == Rect{88, 48, 16, 16}));
    }
    // delta.y == 0（规则 1 边界）：底边恰在平台顶也不阻挡。
    {
        const KinematicResult r = tg::kinematic_step(&g.view, 1, pw, pc,
                                                     Rect{40, 48, 16, 16},
                                                     Vec2{0, 0});
        CHECK(!r.sweep.blocked_y);
    }
    // 空数组与既有重载逐位一致（sweep 与 kinematic 双形态）。
    {
        for (const Vec2 d : {Vec2{0, 16}, Vec2{16, 16}, Vec2{-32, 40},
                             Vec2{0, 0}}) {
            const auto a = tg::sweep_move(&g.view, 1, Rect{40, 48, 16, 16}, d);
            const auto b = tg::sweep_move(&g.view, 1, nullptr, 0,
                                          Rect{40, 48, 16, 16}, d);
            CHECK(a.box == b.box && a.blocked_x == b.blocked_x &&
                  a.blocked_y == b.blocked_y && a.result == b.result);
            const auto ka = tg::kinematic_step(&g.view, 1, Rect{40, 48, 16, 16}, d);
            const auto kb =
                tg::kinematic_step(&g.view, 1, nullptr, 0, Rect{40, 48, 16, 16}, d);
            CHECK(ka.sweep.box == kb.sweep.box && ka.ev.grounded == kb.ev.grounded &&
                  ka.ev.landed == kb.ev.landed &&
                  ka.ev.wall_dir == kb.ev.wall_dir);
        }
    }
}

// ════════════════════ resolve_overlap ════════════════════

void test_resolve_overlap() {
    Grid g(8, 8);
    for (int x = 0; x < 8; ++x) g.fill(x, 4);  // 地面行 y [64,80)

    // 压入地面 12px → Y 轴向上推出。
    {
        const auto r = tg::resolve_overlap(&g.view, 1, Rect{32, 60, 16, 16});
        CHECK(r.resolved);
        CHECK((r.box == Rect{32, 48, 16, 16}));
    }
    // 斜角对称重叠 → 轴 tie 取 X（正方向 8 与负方向 -8 同幅，保序取先判的 px_pos；实际结果为 X 负向因 px_neg 幅度更小先胜出——此处验证确定性而非方向偏好）。
    {
        Grid c(4, 4);
        c.fill(0, 0);
        const auto r = tg::resolve_overlap(&c.view, 1, Rect{-8, -8, 16, 16});
        CHECK(r.resolved);
        CHECK((r.box == Rect{-16, -8, 16, 16}));  // tie → X 负向
    }
    // 不重叠 → 原样返回。
    {
        const Rect in{32, 40, 16, 16};
        const auto r = tg::resolve_overlap(&g.view, 1, in);
        CHECK(r.resolved);
        CHECK(r.box == in);
    }
    // 参数非法 → {原值, false}。
    {
        const Rect bad{0, 0, -1, 4};
        const auto r = tg::resolve_overlap(&g.view, 1, bad);
        CHECK(!r.resolved);
        CHECK(r.box == bad);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const Rect bad2{nan, 0, 4, 4};
        const auto r2 = tg::resolve_overlap(&g.view, 1, bad2);
        CHECK(!r2.resolved);
        CHECK(std::isnan(r2.box.x));
    }
    // 多层视图逐层取最紧：同方向取各层所需的最大推距（见断言行注释）。
    {
        Grid g1(8, 8);
        for (int x = 0; x < 8; ++x) g1.fill(x, 4);  // 世界 y [64,80)
        Grid g2(8, 8);
        g2.fill(2, 4);
        g2.view.origin_y = -12;                     // cell (2,4) 世界 y [52,68)
        const SolidGridView views[2] = {g1.view, g2.view};
        const Rect in{32, 60, 16, 16};
        const auto r = tg::resolve_overlap(views, 2, in);
        CHECK(r.resolved);
        CHECK((r.box == Rect{32, 80, 16, 16}));  // 同方向取各层最大推距：Y+ 需
                                                 // 同时清 layer1(20px) 与 layer2(8px) → 20px
        CHECK(tg::rect_hits_solid(views, 2, r.box) != TileQueryResult::solid);
    }
    // 暴力对照：随机重叠盒，resolved ⇒ 不重叠，且推距 ≤ 逐像素回退法。
    {
        Grid w(8, 8);
        w.fill(3, 3);
        w.fill(3, 4);
        w.fill(4, 4);
        w.fill(5, 2);
        tg::Random rng{42};  // 引擎确定性随机：60 组随机重叠盒（≥ 计划下限 40）
        for (int case_i = 0; case_i < 60; ++case_i) {
            const Rect in{static_cast<float>(rng.next_int(-48, 160)),
                          static_cast<float>(rng.next_int(-48, 160)),
                          static_cast<float>(rng.next_int(8, 32)),
                          static_cast<float>(rng.next_int(8, 32))};
            const auto r = tg::resolve_overlap(&w.view, 1, in);
            if (!r.resolved) continue;
            CHECK(tg::rect_hits_solid(&w.view, 1, r.box) !=
                  TileQueryResult::solid);
            // 逐像素回退基线（沿两轴各回退至清空，取较小者）。
            float bx = in.x;
            while (tg::rect_hits_solid(&w.view, 1,
                                       Rect{bx + 1, in.y, in.w, in.h}) ==
                       TileQueryResult::solid &&
                   bx + 1 < in.x + 256)
                bx += 1;
            const float px = bx + 1 - in.x;
            float py = in.y;
            while (tg::rect_hits_solid(&w.view, 1,
                                       Rect{in.x, py + 1, in.w, in.h}) ==
                       TileQueryResult::solid &&
                   py + 1 < in.y + 256)
                py += 1;
            const float pyd = py + 1 - in.y;
            const float dist =
                std::fabs(r.box.x - in.x) + std::fabs(r.box.y - in.y);
            CHECK(dist <= px + pyd + 2.0f * kEpsilon);
        }
    }
}

// ════════════════════ sweep_move_mixed ════════════════════

void test_mixed_sweep() {
    Grid g(8, 8);
    for (int x = 0; x < 8; ++x) g.fill(x, 4);  // 地面行 y [64,80)

    // 动态盒比静态墙更紧 → 动态阻挡，索引报告。
    {
        const DynBox boxes[1] = {DynBox{Rect{96, 48, 16, 16}, Vec2{0, 0}}};
        const auto r = tg::sweep_move_mixed(&g.view, 1, boxes, 1,
                                            Rect{48, 48, 16, 16}, Vec2{64, 0});
        CHECK((r.sweep.box == Rect{80, 48, 16, 16}));
        CHECK(r.sweep.blocked_x);
        CHECK(r.hit_dyn_x == 0);
        CHECK(r.hit_dyn_y == -1);
    }
    // 静态层 + 动态盒同轴阻挡 → 仍报动态下标（静态不占下标）。
    {
        // 地面行 ty=4 在 y 下方；向上撞动态盒（y 32..48），静态无阻挡。
        const DynBox boxes[1] = {DynBox{Rect{32, 32, 16, 16}, Vec2{0, 0}}};
        const auto r = tg::sweep_move_mixed(&g.view, 1, boxes, 1,
                                            Rect{32, 48, 16, 16}, Vec2{0, -16});
        CHECK((r.sweep.box == Rect{32, 48, 16, 16}));
        CHECK(r.sweep.blocked_y);
        CHECK(r.hit_dyn_y == 0);
    }
    // 静态层与动态盒同轴阻挡：仍报 DynBox 下标（静态不占下标）。
    {
        Grid hw(8, 8);
        hw.fill(2, 1);  // 静态 cell (2,1)：世界 [32,48)x[16,32)
        const DynBox boxes[1] = {DynBox{Rect{32, 24, 16, 16}, Vec2{0, 0}}};
        const auto r = tg::sweep_move_mixed(&hw.view, 1, boxes, 1,
                                            Rect{32, 48, 16, 16}, Vec2{0, -100});
        CHECK(r.sweep.blocked_y);
        CHECK((r.sweep.box == Rect{32, 40, 16, 16}));  // 动态更紧（gap 8 < 静态 16）
        CHECK(r.hit_dyn_y == 0);
    }

    // 多盒阻挡取小下标：两盒同列。
    {
        const DynBox boxes[2] = {DynBox{Rect{120, 48, 16, 16}, Vec2{0, 0}},
                                 DynBox{Rect{96, 48, 16, 16}, Vec2{0, 0}}};
        const auto r = tg::sweep_move_mixed(&g.view, 1, boxes, 2,
                                            Rect{48, 48, 16, 16}, Vec2{128, 0});
        CHECK(r.sweep.blocked_x);
        CHECK(r.hit_dyn_x == 0);
    }
    // 无动态阻挡 → -1；动态盒不截断位移 → 不报告。
    {
        const DynBox boxes[1] = {DynBox{Rect{200, 200, 16, 16}, Vec2{0, 0}}};
        const auto r = tg::sweep_move_mixed(&g.view, 1, boxes, 1,
                                            Rect{32, 48, 16, 16}, Vec2{8, 0});
        CHECK((r.sweep.box == Rect{40, 48, 16, 16}));
        CHECK(r.hit_dyn_x == -1);
        CHECK(r.hit_dyn_y == -1);
    }
    // 空数组与 sweep_move 逐位一致。
    {
        for (const Vec2 d : {Vec2{16, 0}, Vec2{0, 16}, Vec2{-16, -16}}) {
            const auto a = tg::sweep_move_mixed(&g.view, 1, nullptr, 0,
                                                Rect{32, 48, 16, 16}, d);
            const auto b = tg::sweep_move(&g.view, 1, Rect{32, 48, 16, 16}, d);
            CHECK(a.sweep.box == b.box && a.sweep.result == b.result &&
                  a.hit_dyn_x == -1 && a.hit_dyn_y == -1);
        }
    }
    // 参数非法（DynBox 尺寸非正）→ error。
    {
        const DynBox boxes[1] = {DynBox{Rect{0, 0, 0, 4}, Vec2{0, 0}}};
        const auto r = tg::sweep_move_mixed(&g.view, 1, boxes, 1,
                                            Rect{32, 48, 16, 16}, Vec2{8, 0});
        CHECK(r.sweep.result == TileQueryResult::error);
        CHECK((r.sweep.box == Rect{32, 48, 16, 16}));
    }
}

// ════════════════════ SolidGrid ════════════════════

void test_solid_grid() {
    // 场景：2×2 solid 层（palette 模式），tile 值 0/1 均为阻挡。
    const std::string scene = R"({"format":"tro-scene","version":2,"tilemap":{
        "tile_width":16,"tile_height":16,
        "palette":["#2a2d3a","#7f8ca3"],
        "layers":[
          {"name":"g","width":2,"height":2,"solid":true,
           "tiles":[1,-1, -1,1]} ]},"entities":[]})";
    auto asset = SceneAsset::load_json(scene, "grid_test");
    CHECK(asset.has_value());

    auto loaded = tg::SolidGrid::load(*asset, 0);
    CHECK(loaded.has_value());
    if (!loaded.has_value()) return;
    tg::SolidGrid grid = std::move(*loaded);
    const auto& v = grid.view();
    CHECK(v.width == 2 && v.height == 2 && v.layer_id == 0);
    CHECK(v.mask[0] == 1 && v.mask[1] == 0 && v.mask[2] == 0 && v.mask[3] == 1);
    CHECK((tg::is_solid_at(&v, 1, Vec2{8, 8}) == TileQueryResult::solid));
    // set_tile 同步：写 asset + 掩码一次完成；写后 view 立即可见。
    auto ok = grid.set_tile(*asset, 0, 1, 0, 1);
    CHECK(ok.has_value());
    CHECK(v.mask[1] == 1);
    CHECK((tg::is_solid_at(&v, 1, Vec2{24, 8}) == TileQueryResult::solid));
    auto cleared = grid.set_tile(*asset, 0, 1, 0, -1);
    CHECK(cleared.has_value());
    CHECK(v.mask[1] == 0);
    CHECK((tg::is_solid_at(&v, 1, Vec2{24, 8}) == TileQueryResult::clear));
    // asset 写失败（值域越界：palette 仅 2 项）→ 掩码零修改。
    auto bad = grid.set_tile(*asset, 0, 1, 0, 9);
    CHECK(!bad.has_value());
    CHECK(v.mask[1] == 0);
    // 层不一致 → error。
    auto wrong = grid.set_tile(*asset, 1, 0, 0, 1);
    CHECK(!wrong.has_value());
    CHECK(v.mask[1] == 0);
    // refresh 重物化：与磁盘/内存态一致。
    auto r = grid.refresh(*asset, 0);
    CHECK(r.has_value());
    CHECK(v.mask[0] == 1 && v.mask[1] == 0 && v.mask[2] == 0 && v.mask[3] == 1);
    // 非 solid 层 / 层越界：load 与 refresh 均 error 且旧掩码保留。
    const std::string scene2 = R"({"format":"tro-scene","version":2,"tilemap":{
        "tile_width":16,"tile_height":16,
        "palette":["#2a2d3a"],
        "layers":[
          {"name":"bg","width":2,"height":2,"solid":false,
           "tiles":[0,0, 0,0]} ]},"entities":[]})";
    auto asset2 = SceneAsset::load_json(scene2, "grid_test2");
    CHECK(asset2.has_value());
    auto bad_load = tg::SolidGrid::load(*asset2, 0);
    CHECK(!bad_load.has_value());
    auto oob = tg::SolidGrid::load(*asset2, 3);
    CHECK(!oob.has_value());
    auto bad_refresh = grid.refresh(*asset2, 0);
    CHECK(!bad_refresh.has_value());
    CHECK(v.mask[0] == 1 && v.mask[3] == 1);
    auto oob_refresh = grid.refresh(*asset2, 5);
    CHECK(!oob_refresh.has_value());
}

// ════════════════════ 非法入参与确定性 ════════════════════

void test_invalid_and_determinism() {
    Grid g(4, 4);
    g.fill(1, 1);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Rect in{32, 32, 16, 16};
    // NaN → sweep.result=error、全事件 false、wall_dir=0、box 原值。
    const auto r = tg::kinematic_step(&g.view, 1, in, Vec2{nan, 0});
    CHECK(r.sweep.result == TileQueryResult::error);
    CHECK(r.sweep.box == in);
    CHECK(!r.ev.grounded && !r.ev.landed && !r.ev.hit_ceiling &&
          !r.ev.hit_wall && r.ev.wall_dir == 0);
    // 零尺寸 → error。
    const auto r2 = tg::kinematic_step(&g.view, 1, Rect{0, 0, 0, 4}, Vec2{1, 0});
    CHECK(r2.sweep.result == TileQueryResult::error);
    // 确定性：同输入两次调用逐位一致。
    const auto a = tg::kinematic_step(&g.view, 1, Rect{24, 8, 16, 16},
                                      Vec2{4, 4});
    const auto b = tg::kinematic_step(&g.view, 1, Rect{24, 8, 16, 16},
                                      Vec2{4, 4});
    CHECK(a.sweep.box == b.sweep.box && a.ev.grounded == b.ev.grounded &&
          a.ev.landed == b.ev.landed && a.ev.wall_dir == b.ev.wall_dir);
}

// ════════════════════ probe_grounded ↔ kinematic_step 一致性 ════════════════════
// 探针语义只有一份实现（probe_grounded 导出后 kinematic_step 改调它）：同一 box
// 上两条路径必须同值。delta 取**恰 0.0**——零位移时 sweep_core 不改 box，故
// kinematic_step 的 grounded 用的就是入参 box 本身。
void test_probe_matches_step() {
    Grid g(8, 8);
    for (int x = 0; x < 8; ++x) g.fill(x, 4);  // 地面行 ty=4（世界 y 64..80）

    const Rect boxes[] = {{32, 32, 16, 16}, {32, 48, 16, 16}, {0, 48, 16, 16},
                          {100, 48, 16, 16}, {32, 64, 16, 16}};
    for (const Rect& box : boxes) {
        const KinematicResult k =
            tg::kinematic_step(&g.view, 1, box, Vec2{0.0f, 0.0f});
        const bool probe =
            tg::probe_grounded(&g.view, 1, box) == TileQueryResult::solid;
        CHECK(k.ev.grounded == probe);
    }

    // one_way 也走同一路径：站在薄板上两条路径都判 grounded
    const Rect plat{32, 64, 32, 6};
    const Rect on_plat{32, 48, 16, 16};
    const KinematicResult k2 = tg::kinematic_step(&g.view, 1, &plat, 1, on_plat,
                                                  Vec2{0.0f, 0.0f});
    CHECK(k2.ev.grounded);
    CHECK(tg::probe_grounded(&g.view, 1, &plat, 1, on_plat) ==
          TileQueryResult::solid);

    // 参数非法：probe 报 error（不伪装成悬空），不进「非阻挡」分支
    CHECK(tg::probe_grounded(&g.view, 1, Rect{32, 48, 0, 16}) ==
          TileQueryResult::error);
    CHECK(tg::probe_grounded(&g.view, 1, Rect{std::nanf(""), 48, 16, 16}) ==
          TileQueryResult::error);
}

}  // namespace

int main() {
    test_kinematic_events();
    test_one_way();
    test_resolve_overlap();
    test_mixed_sweep();
    test_solid_grid();
    test_probe_matches_step();
    test_invalid_and_determinism();
    if (::tg_test::g_failures == 0) {
        std::printf("kinematic_test: all %d checks passed\n",
                    ::tg_test::g_checks);
        return 0;
    }
    std::printf("kinematic_test: %d/%d checks FAILED\n", ::tg_test::g_failures,
                ::tg_test::g_checks);
    return 1;
}
