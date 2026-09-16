// sim_test.cpp —— 无窗口断言：只跑纯逻辑层（sim.hpp 不含 raylib），不建窗口、不渲染。
//
// 为什么能无窗口：位移/状态机/玩法判定全在 sim.*，场景用纯 trogue 构造（build_scene）。
// 断言钉住的都是「引擎契约 + 玩法规则」，不是实现细节：贴地/不穿地、跑墙不重叠
// solid（引擎的「解算后不重叠」不变式）、土狼时间、踩顶/侧撞、终点旗、确定性。
#include <cstdio>

#include "trogue/scene.hpp"  // tg::rect_hits_solid（独立的几何对照，不用游戏内状态）
#include "sim.hpp"

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (ok) return;
    ++g_failures; std::printf("FAIL: %s\n", what);
}

#define CHECK(cond) check((cond), #cond)

void run(plat::Level& lv, const plat::Input& in, int n) { for (int i = 0; i < n; ++i) lv.step(in); }

// 世界矩形与 solid 层是否重叠：用引擎查询做**独立对照**，避免自证自话。
bool overlaps_solid(const plat::Level& lv, tg::Rect r) { return tg::rect_hits_solid(lv.scene(), r) == tg::TileQueryResult::solid; }

}  // namespace

int main() {
    // ① 自由落体 → 贴地、不穿地、贴住后不再下沉
    {
        plat::Level lv{plat::build_scene(plat::kTile)};
        lv.set_spawn(tg::Vec2{40.0f, 40.0f});
        run(lv, plat::Input{}, 120);
        CHECK(lv.player().grounded());
        CHECK(lv.player().state() == plat::Player::State::Idle);
        CHECK(!overlaps_solid(lv, lv.player().box()));
        const float y = lv.player().box().y;
        run(lv, plat::Input{}, 60);
        CHECK(lv.player().box().y == y);  // 已贴地：位置逐位不变（无抖动/下沉）
        CHECK(lv.deaths() == 0);
    }

    // ② 跑向墙壁 → X 被挡住，且解算后不与 solid 重叠（引擎的不重叠不变式）
    {
        plat::Level lv{plat::build_scene(plat::kTile)};
        lv.set_spawn(tg::Vec2{700.0f, 240.0f});  // 立柱（col 45，左面 x=720）左侧地面
        plat::Input right; right.right = true;
        run(lv, right, 60);
        const tg::Rect b = lv.player().box();
        CHECK(b.x + b.w <= 720.0f + tg::kEpsilon);  // 前缘停在墙面上（不穿入）
        CHECK(!overlaps_solid(lv, b));
        CHECK(lv.player().state() == plat::Player::State::Run);  // 顶着墙仍是 Run
    }

    // ③ 土狼时间：走下坑沿（离地）后仍能在宽限内起跳 → fall 转 jump
    {
        plat::Level lv{plat::build_scene(plat::kTile)};
        lv.set_spawn(tg::Vec2{340.0f, 240.0f});  // 地板终点在 col 22（x=352）
        plat::Input right; right.right = true;
        run(lv, right, 5);  // 先落地（探地由引擎事件给出，要跑一步才有）
        CHECK(lv.player().grounded());
        int i = 0;
        while (i < 120 && lv.player().grounded()) {
            lv.step(right);
            ++i;
        }
        CHECK(!lv.player().grounded());  // 已走出坑沿
        CHECK(lv.player().state() == plat::Player::State::Fall);
        plat::Input jump = right;
        jump.jump = true;  // 离地后立刻按跳：土狼时间内的起跳
        lv.step(jump);
        CHECK(lv.player().state() == plat::Player::State::Jump);
        CHECK(lv.player().vy() < 0.0f);
    }

    // ④ 踩敌人顶部 → 敌人减少 + 玩家向上弹（vy<0），且不算受伤
    {
        plat::Level lv{plat::build_scene(plat::kTile)};
        const tg::Rect eb = lv.enemies().front()->box(); const int before = lv.alive_enemies();
        lv.set_spawn(tg::Vec2{eb.x + 6.0f, eb.y - 60.0f});  // 从敌人正上方落下
        for (int i = 0; i < 90 && lv.alive_enemies() == before; ++i) lv.step(plat::Input{});
        CHECK(lv.alive_enemies() == before - 1);
        CHECK(lv.player().vy() < 0.0f);
        CHECK(lv.player().hp() == 2);
    }

    // ⑤ 侧面接触受伤（无敌帧 + 闪烁），无敌帧内再次接触不再扣血
    {
        plat::Level lv{plat::build_scene(plat::kTile)};
        const tg::Rect eb = lv.enemies().front()->box();
        lv.set_spawn(tg::Vec2{eb.x + 10.0f, eb.y});  // 站在敌人右侧：敌兵会走过来撞上
        lv.step(plat::Input{});
        CHECK(lv.player().hp() == 1);
        CHECK(lv.player().invulnerable());
        run(lv, plat::Input{}, 20);  // 0.33s < 无敌帧 1.2s，期间持续接触
        CHECK(lv.player().hp() == 1);  // 不再扣血
        CHECK(lv.player().hp() == 1);
    }

    // ⑥ 到达终点旗 → won=1
    {
        plat::Level lv{plat::build_scene(plat::kTile)};
        CHECK(!lv.won());
        const tg::Rect g = lv.goal();
        lv.set_spawn(tg::Vec2{g.x, g.y});
        lv.step(plat::Input{});
        CHECK(lv.won());
    }

    // ⑦ 掉出场景 → 死亡：deaths+1 并重置关卡（回出生点、血量回满）
    {
        plat::Level lv{plat::build_scene(plat::kTile)};
        lv.set_spawn(tg::Vec2{360.0f, 240.0f});  // 坑（22..25）上方：自由落体出界
        for (int i = 0; i < 120 && lv.deaths() == 0; ++i) lv.step(plat::Input{});
        CHECK(lv.deaths() == 1 && lv.player().hp() == 2 && lv.player().box().x == 360.0f);
    }

    // ⑧ 确定性：同一输入序列 → 逐位一致（位置/存活数/死亡数），无随机源
    {
        plat::Level a{plat::build_scene(plat::kTile)}, b{plat::build_scene(plat::kTile)};
        for (int i = 0; i < 600; ++i) { a.step(plat::scripted_input(i)); b.step(plat::scripted_input(i)); }
        CHECK(a.player().box() == b.player().box());
        CHECK(a.alive_enemies() == b.alive_enemies() && a.deaths() == b.deaths());
    }

    // ⑨ spawn 之后「立即探地」：KinematicEvents::grounded 只在跑过一步之后才有
    //    意义（本文件 ③ 的注释就踩过这条），未推进任何步时 probe_grounded 已能
    //    回答「脚下是否有支撑」。默认出生点 box (32,240,12,16) 站在地面上：
    //    探地矩形 (32,256,12,1) 命中 solid 层。
    {
        plat::Level lv{plat::build_scene(plat::kTile)};
        const tg::Rect b = lv.player().box();
        CHECK(!lv.player().grounded());  // 一步未跑：事件未派生
        CHECK(tg::probe_grounded(lv.scene(), b) == tg::TileQueryResult::solid);
        lv.step(plat::Input{});          // 跑一步后事件派生：与探针结论一致
        CHECK(lv.player().grounded());
    }

    std::printf("checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
