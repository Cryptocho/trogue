// step_clock_test.cpp —— StepClock 无头单测：黄金序列（二进制精确 dt，杜绝
// 浮点模糊）+ 双池语义（授步不丢/时间通道护栏）+ 暂停/恢复 + 构造程序错误。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "trogue/time.hpp"
#include "test_util.hpp"

namespace {

using tg::StepClock;

// 黄金序列：跑一段 (dt, credit) 脚本，冻结全部 Tick 输出。
struct ScriptStep {
    double dt;      // tick 的 frame_dt
    int credit;     // tick 前 credit（0 = 不授）
};
std::vector<StepClock::Tick> run_script(StepClock& c,
                                        const std::vector<ScriptStep>& script) {
    std::vector<StepClock::Tick> out;
    for (const auto& s : script) {
        if (s.credit != 0) c.credit(s.credit);
        out.push_back(c.tick(s.dt));
    }
    return out;
}

void test_golden_normal() {
    // 二进制精确参数：step=0.25，dt 全为 0.25 的倍数 → 零浮点误差。
    StepClock c(0.25, 5);
    // ① 常规推进：整步弹出 + 余量累积
    auto r = run_script(c, {{0.25, 0}, {0.1, 0}, {0.15, 0}, {0.6, 0}});
    CHECK(r[0].steps == 1 && r[0].alpha == 0.0 && !r[0].overflowed);
    CHECK(r[1].steps == 0 && std::abs(r[1].alpha - 0.4) < 1e-12);
    CHECK(r[2].steps == 1 && r[2].alpha == 0.0);
    CHECK(r[3].steps == 2 && std::abs(r[3].alpha - 0.4) < 1e-12);
    CHECK(c.total_steps() == 4);
    CHECK(c.overflow_count() == 0);
}

void test_golden_overflow_guard() {
    // ② 螺旋死亡护栏：单帧 4 整步、max=2 → 弹 2 丢 2、无债务
    StepClock c(0.25, 2);
    auto r = run_script(c, {{1.0, 0}, {0.0, 0}});
    CHECK(r[0].steps == 2 && r[0].overflowed);
    CHECK(r[0].alpha == 0.0);  // 超额整步丢弃后余量归零
    CHECK(r[1].steps == 0);    // 无债务累积
    CHECK(c.overflow_count() == 1);
    // 小数余量不丢、超额整步丢弃：1.3s = 5 整步 + 0.05 → 弹 4（max）、
    // 丢 1 整步、余 0.05 → alpha = 0.2
    StepClock c2(0.25, 4);
    auto r2 = run_script(c2, {{1.3, 0}});
    CHECK(r2[0].steps == 4 && r2[0].overflowed);
    CHECK(std::abs(r2[0].alpha - 0.2) < 1e-12);
    auto r2b = run_script(c2, {{0.0, 0}});
    CHECK(r2b[0].steps == 0);  // 无债务
}

void test_golden_credit_never_lost() {
    // ③ 反丢步（必测）：credit(max+2) → tick(0) 两次 → 恰 max+2 步、零溢出
    StepClock c(0.25, 5);
    c.credit(7);
    auto a = c.tick(0.0);
    CHECK(a.steps == 5 && !a.overflowed);
    auto b = c.tick(0.0);
    CHECK(b.steps == 2 && !b.overflowed);
    CHECK(c.total_steps() == 7 && c.overflow_count() == 0);
}

void test_golden_mixed_overflow() {
    // ④ 混合溢出：授步 7 + 时间 4 整步、max=5 → 弹 5 全出自授步池，
    //    时间池 4 整步丢弃（cap 不越界）、授步余 2 步后续可取
    StepClock c(0.25, 5);
    c.credit(7);
    auto a = c.tick(1.0);  // 时间通道 4 整步
    CHECK(a.steps == 5 && a.overflowed);
    CHECK(c.total_steps() == 5 && c.overflow_count() == 1);
    auto b = c.tick(0.0);
    CHECK(b.steps == 2 && !b.overflowed);  // 授步余量完好
    CHECK(c.total_steps() == 7);
    // 极端：授步 7 + 时间 0.5（2 整步）→ 弹 5 全出自授步池；时间池 2 整步
    // 超 clamp → 丢弃 2、余 0 → overflowed=true、alpha=0（丢弃不触碰授步池）
    StepClock c2(0.25, 5);
    c2.credit(7);
    auto x = c2.tick(0.5);
    CHECK(x.steps == 5 && x.overflowed && x.alpha == 0.0);
    auto y = c2.tick(0.0);
    CHECK(y.steps == 2 && !y.overflowed);  // 授步余量完好
}

void test_golden_pause() {
    // ⑤ 暂停 = 停时间通道：不累加、alpha 恒 0；授步照常排空
    StepClock c(0.25, 5);
    c.pause();
    auto r = run_script(c, {{0.5, 0}, {0.0, 3}, {0.0, 0}, {0.0, 0}});
    CHECK(r[0].steps == 0 && r[0].alpha == 0.0);  // dt 被忽略
    CHECK(r[1].steps == 3);                        // credit 排空（<max 单次取完）
    CHECK(r[2].steps == 0 && r[3].steps == 0);
    CHECK(c.total_steps() == 3);
    // 暂停期 dt 被忽略（时间池冻结无积累）：resume 后无凭空步
    c.resume();
    auto p = c.tick(0.0);
    CHECK(p.steps == 0 && p.alpha == 0.0);
}

void test_pause_resume_continuity() {
    // 余量跨暂停保持：0.1 余量 → 暂停 → 恢复 → 再 0.15 凑整步
    StepClock c(0.25, 5);
    auto r1 = run_script(c, {{0.1, 0}});
    CHECK(r1[0].steps == 0);
    c.pause();
    run_script(c, {{1.0, 0}});   // 暂停期 dt 全忽略
    c.resume();
    auto r2 = run_script(c, {{0.15, 0}});
    CHECK(r2[0].steps == 1 && r2[0].alpha == 0.0);
    CHECK(c.total_steps() == 1);
}

void test_determinism() {
    // 同输入序列两次运行 → 全部输出逐位一致
    const std::vector<ScriptStep> script = {
        {1.0 / 3.0, 0}, {0.01, 2}, {0.0, 0}, {0.7, 0}, {0.33, 0}, {0.0, 4}};
    StepClock a(1.0 / 60.0, 5);
    StepClock b(1.0 / 60.0, 5);
    auto ra = run_script(a, script);
    auto rb = run_script(b, script);
    CHECK(ra.size() == rb.size());
    const size_t n = std::min(ra.size(), rb.size());
    for (size_t i = 0; i < n; ++i) {
        CHECK(ra[i].steps == rb[i].steps);
        CHECK(ra[i].alpha == rb[i].alpha);        // double 逐位比较（同运算序）
        CHECK(ra[i].overflowed == rb[i].overflowed);
    }
    CHECK(a.total_steps() == b.total_steps());
    CHECK(a.overflow_count() == b.overflow_count());
}

void test_ctor_and_edges() {
    bool threw = false;
    try { StepClock bad(0.0, 5); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { StepClock bad(-1.0, 5); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { StepClock bad(0.25, 0); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { StepClock bad(0.25, -3); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);

    StepClock c(0.25, 5);
    auto r = run_script(c, {{-1.0, 0}, {0.0, 0}});  // 负 dt 安全
    CHECK(r[0].steps == 0 && !r[0].overflowed);
    c.credit(0);
    c.credit(-5);  // 无操作
    CHECK(c.tick(0.0).steps == 0);
    CHECK(c.step_seconds() == 0.25);
}

}  // namespace

int main() {
    test_golden_normal();
    test_golden_overflow_guard();
    test_golden_credit_never_lost();
    test_golden_mixed_overflow();
    test_golden_pause();
    test_pause_resume_continuity();
    test_determinism();
    test_ctor_and_edges();
    std::printf("[step clock test] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}
