// sim_test.cpp —— swarm 的无窗口断言（ctest 目标 swarm_sim_test；不 include raylib）。
//
// 为什么能在无 GL 环境跑：断言只碰纯逻辑层（sim.*）+ 引擎的碰撞查询原语，把房间
// 掩码自己拼出来后用固定步推进，全程不需要窗口、贴图或渲染。
// 钉住的口径：确定性（同参两次摘要逐字相同）、实体不出墙、击杀/拾取/升级链推进、
// 站着不动最终被打死并重置、升级规则生效。
#include <cstdint>
#include <cstdio>
#include <cstdlib>  // std::exit（掩码构造失败的常量入参即程序错误）
#include <string>
#include <vector>

#include "sim.hpp"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what);
}

#define CHECK(cond) check((cond), #cond)
#define REQUIRE(cond)                                                    \
    do {                                                                 \
        check((cond), #cond);                                            \
        if (!(cond)) {                                                   \
            std::printf("checks=%d failures=%d\n", g_checks, g_failures); \
            return 1;                                                    \
        }                                                                \
    } while (false)

// 房间碰撞视图：由引擎按公开谓词构造（RAII 自持掩码，移动后视图自洽）
tg::SolidGrid make_grid() {
    auto grid = tg::SolidGrid::create(swarm::kRoomW, swarm::kRoomH, swarm::kTile,
                                      swarm::kTile, swarm::room_solid);
    if (!grid) {  // 入参是编译期常量：失败即程序错误
        std::fprintf(stderr, "swarm_sim_test: SolidGrid::create 失败: %s\n",
                     grid.error().message.c_str());
        std::abort();
    }
    return std::move(*grid);
}

// 固定步跑满 seconds 秒（默认无输入）：同参数必得相同 World —— 这就是确定性口径
swarm::World run(int seconds, swarm::Input in = swarm::Input{}) {
    const tg::SolidGrid grid = make_grid();
    swarm::World w;
    swarm::reset_round(w);
    for (int i = 0; i < seconds * 60; ++i)
        swarm::step(w, in, &grid.view(), 1, swarm::kDt);
    return w;
}

// 所有实体都不得与墙重叠：墙碰撞的验收口径 = 引擎 rect_hits_solid 判 clear
bool all_inside(const swarm::World& w) {
    const tg::SolidGrid grid = make_grid();
    for (int i = 0; i < swarm::count(w); ++i)
        if (tg::rect_hits_solid(&grid.view(), 1, swarm::rect_of(w, i)) ==
            tg::TileQueryResult::solid)
            return false;
    return true;
}

int enemies_of(const swarm::World& w) {
    int n = 0;
    for (int i = 0; i < swarm::count(w); ++i)
        if (w.tag[i] == swarm::kEnemy) ++n;
    return n;
}

}  // namespace

int main() {
    // ① 房间几何与碰撞视图自洽：边界是墙，内部可走（前置条件，坏了后面都无意义）
    REQUIRE(swarm::room_solid(0, 0) && swarm::room_solid(swarm::kRoomW - 1, 5));
    REQUIRE(swarm::room_solid(3, swarm::kRoomH - 1));
    CHECK(!swarm::room_solid(swarm::kRoomW / 2, swarm::kRoomH / 2));

    // ② 确定性：同 seed、同输入序列、同步数 → 摘要逐字相同
    const swarm::World a = run(20);
    const swarm::World b = run(20);
    CHECK(swarm::summary(a) == swarm::summary(b));
    CHECK(a.steps == 20 * 60);

    // ③ 击杀/拾取/升级链确实推进（不是空转）
    CHECK(a.kills > 0);
    CHECK(a.pickups > 0);
    CHECK(a.level > 1);
    CHECK(a.spawned > 0);

    // ④ 实体始终在室内；敌人数不超上限；玩家血量合法
    CHECK(all_inside(a));
    CHECK(enemies_of(a) <= swarm::kMaxEnemies);
    CHECK(swarm::player_hp(a) > 0 && swarm::player_hp(a) <= swarm::kPlayerHp);

    // ⑤ 输入链路：一路向右走 1 秒会真的右移（输入 → 速度 → 位移）
    const swarm::World right = run(1, swarm::Input{1.0f, 0.0f, false});
    CHECK(right.pos[right.player].x > swarm::kRoomW * swarm::kTile * 0.5f + 100.0f);

    // ⑥ 站着不动最终挨打并死亡重置，且元进度（kills/level）跨重置保留
    const swarm::World dead = run(45);
    CHECK(dead.deaths > 0);
    CHECK(dead.kills >= a.kills && dead.kills > 0);
    CHECK(dead.level >= 1);
    CHECK(all_inside(dead));
    CHECK(swarm::summary(dead) == swarm::summary(run(45)));

    // ⑦ 升级规则：5 级起射速阶梯（间隔变短）、10 级起伤害阶梯（+1）
    CHECK(swarm::fire_interval(5) < swarm::fire_interval(1));
    CHECK(swarm::bullet_dmg(10) == swarm::bullet_dmg(1) + 1);
    CHECK(swarm::xp_need(1) == 4);

    // ⑧ 掩码等价性：SolidGrid::create(谓词) 与逐格重建的期望字节逐字节相同
    //    （摘要只有 8 个标量，对「掩码变了但没影响统计量」的变化会漏报）
    {
        const tg::SolidGrid grid = make_grid();
        const tg::SolidGridView& v = grid.view();
        REQUIRE(v.width == swarm::kRoomW && v.height == swarm::kRoomH);
        REQUIRE(v.tile_w == swarm::kTile && v.tile_h == swarm::kTile);
        CHECK(v.stride == swarm::kRoomW);  // create 内部显式置 stride = width
        CHECK(v.layer_id == -1);           // 非资产来源掩码
        int mismatches = 0;
        for (int ty = 0; ty < swarm::kRoomH; ++ty)
            for (int tx = 0; tx < swarm::kRoomW; ++tx) {
                const std::uint8_t want = swarm::room_solid(tx, ty) ? 1 : 0;
                if (v.mask[static_cast<std::size_t>(ty) * swarm::kRoomW + tx] != want)
                    ++mismatches;
            }
        CHECK(mismatches == 0);
    }

    std::printf("checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
