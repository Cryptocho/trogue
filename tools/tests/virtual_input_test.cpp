// virtual_input_test.cpp —— VirtualInput 无头单测：稳定序、步边界提取、
// 消费时状态更新、min_hold 撕裂防护、flush/积压可观测、上限丢最旧。

#include <cstdint>
#include <vector>

#include "trogue/input.hpp"
#include "test_util.hpp"

namespace {

using tg::InputEvent;
using tg::VirtualInput;

// 便捷：把一批事件转成 "key:down" 可比较串（按序断言用）
std::vector<std::string> keys_of(const std::vector<InputEvent>& evs) {
    std::vector<std::string> out;
    for (const auto& e : evs) out.push_back(std::to_string(e.key) + (e.down ? "d" : "u"));
    return out;
}

void test_ordering_and_boundary() {
    // 乱序注入按 (t, 入队序) 稳定出队；t == boundary 归下一批（严格 <）
    VirtualInput in;
    in.push(3, true, 0.5);
    in.push(1, true, 0.2);
    in.push(2, true, 0.2);   // 与 key1 同 t：入队序 tie-break
    in.push(4, true, 0.25);  // 恰在边界 0.25 → 归下一批
    auto b1 = in.step_due(0.25);
    CHECK(keys_of(b1) == std::vector<std::string>({"1d", "2d"}));
    auto b2 = in.step_due(0.75);
    CHECK(keys_of(b2) == std::vector<std::string>({"4d", "3d"}));
    CHECK(in.pending() == 0);
}

void test_multi_step_boundary_progression() {
    // 本地步序 i 对齐惯例：多步帧内各步 boundary 严格递增，事件不重不漏
    VirtualInput in({1.0 / 4.0, 0});
    in.push(9, true, 0.1);    // 落在步 0（boundary 0）与步 1（0.25）之间
    in.push(8, true, 0.3);    // 落在步 1 与步 2（0.5）之间
    const double boundaries[] = {0.0, 0.25, 0.5, 0.75};
    auto s0 = in.step_due(boundaries[0]);
    CHECK(keys_of(s0).empty());
    auto s1 = in.step_due(boundaries[1]);
    CHECK(keys_of(s1) == std::vector<std::string>({"9d"}));
    auto s2 = in.step_due(boundaries[2]);
    CHECK(keys_of(s2) == std::vector<std::string>({"8d"}));
    auto s3 = in.step_due(boundaries[3]);
    CHECK(keys_of(s3).empty());
}

void test_down_consumed_time_update() {
    // down() 在消费时更新：push 后未消费不影响
    VirtualInput in;
    in.push(7, true, 0.0);
    CHECK(in.down(7) == false);
    auto evs = in.step_due(1.0);
    CHECK(evs.size() == 1);
    CHECK(in.down(7) == true);
    in.push(7, false, 1.5);
    CHECK(in.down(7) == true);   // 未消费仍 true
    in.step_due(2.0);
    CHECK(in.down(7) == false);
    CHECK(in.down(999) == false);  // 未知键 false
}

void test_min_hold_defers_up() {
    // min_hold=2：up 距 down 不足 2 步 → 推迟到 down_step+2 边界出队
    VirtualInput in({0.25, 2});
    in.push(5, true, 0.01);
    in.push(5, false, 0.02);
    auto c0 = in.step_due(0.25);   // 步 0：down 消费（down_step=0）
    CHECK(keys_of(c0) == std::vector<std::string>({"5d"}));
    auto c1 = in.step_due(0.5);    // 步 1：up 到期但 1-0<2 → 推迟
    CHECK(c1.empty());
    CHECK(in.down(5) == true);
    CHECK(in.pending() == 1);      // 推迟 up 计入 pending
    auto c2 = in.step_due(0.75);   // 步 2：down_step+2=2 → 出队
    CHECK(keys_of(c2) == std::vector<std::string>({"5u"}));
    CHECK(in.down(5) == false);
    CHECK(in.pending() == 0);
}

void test_min_hold_redown_discards_deferred() {
    // 推迟期新 down：未生效 up 丢弃，down 保持 true 不闪断
    VirtualInput in({0.25, 3});
    in.push(5, true, 0.01);
    in.push(5, false, 0.02);
    in.step_due(0.25);             // 步 0：down（down_step=0）
    in.step_due(0.5);              // 步 1：up 推迟（due_step=3）
    in.push(5, true, 0.55);
    auto c2 = in.step_due(0.75);   // 步 2：新 down 消费 → 推迟 up 丢弃
    CHECK(keys_of(c2) == std::vector<std::string>({"5d"}));
    CHECK(in.down(5) == true);
    CHECK(in.pending() == 0);
    // 新 down 之后的新 up：距新 down_step=2 不足 3 → 再推迟；第 5 步出队
    in.push(5, false, 0.8);
    auto c3 = in.step_due(1.0);    // 步 3：3-2<3 → 推迟
    CHECK(c3.empty());
    auto c4 = in.step_due(1.25);   // 步 4：4-2<3 → 推迟
    CHECK(c4.empty());
    auto c5 = in.step_due(1.5);    // 步 5：2+3=5 → 出队
    CHECK(keys_of(c5) == std::vector<std::string>({"5u"}));
    CHECK(in.down(5) == false);
}

void test_flush_keeps_down_state() {
    // flush 清队列（含推迟 up）+ down() 保持现值；后续真实 up 正常生效
    VirtualInput in({0.25, 5});
    in.push(5, true, 0.01);
    in.push(5, false, 0.02);
    in.step_due(0.25);             // down 消费
    in.step_due(0.5);              // up 推迟（due_step=5）
    in.push(6, true, 0.6);
    in.flush();
    CHECK(in.pending() == 0);
    CHECK(in.dropped() == 0);      // flush 不计丢弃
    CHECK(in.down(5) == true);     // 保持现值
    CHECK(in.down(6) == false);
    CHECK(in.step_due(1.0).empty());   // 步 2：队列已清
    // 后续真实 up 正常走完（min_hold 满足后于步 5 出队）
    in.push(5, false, 1.1);
    auto c3 = in.step_due(1.25);       // 步 3：到期但 3-0<5 → 推迟至步 5
    CHECK(c3.empty());
    auto c4 = in.step_due(1.5);        // 步 4：推迟未到期
    CHECK(c4.empty());
    auto c5 = in.step_due(1.75);       // 步 5：0+5=5 → 出队
    CHECK(keys_of(c5) == std::vector<std::string>({"5u"}));
    CHECK(in.down(5) == false);
}

void test_overflow_drops_oldest() {
    // 上限丢最旧：kPendingMax 条未消费再 push → seq 最小者被丢 + dropped 计数
    VirtualInput in;
    for (int k = 0; k < tg::kPendingMax; ++k) in.push(k, true, 0.0);
    CHECK(in.pending() == tg::kPendingMax);
    CHECK(in.dropped() == 0);
    in.push(9999, true, 0.0);      // 挤掉 key=0（最旧）
    CHECK(in.pending() == tg::kPendingMax);
    CHECK(in.dropped() == 1);
    auto evs = in.step_due(1e9);   // 全部到期
    CHECK(static_cast<int>(evs.size()) == tg::kPendingMax);
    bool saw_zero = false, saw_max = false;
    for (const auto& e : evs) {
        if (e.key == 0) saw_zero = true;
        if (e.key == 9999) saw_max = true;
    }
    CHECK(!saw_zero);
    CHECK(saw_max);
}

void test_deferred_up_then_redown_same_boundary() {
    // 推迟 up 的到期边界与同键新 down 重合：先放行到期 up、再应用新 down
    //（「未生效」= 尚未放行；放行顺序冻结为 [up, down]，终态 down==true）
    VirtualInput in({0.25, 2});
    in.push(5, true, 0.01);
    in.push(5, false, 0.02);
    in.step_due(0.25);             // 步 0：down（down_step=0）
    in.step_due(0.5);              // 步 1：up 推迟（due=2）
    in.push(5, true, 0.55);        // 新 down（步 2 边界前注入）
    auto c2 = in.step_due(0.75);   // 步 2：up 到期放行 + 新 down 应用
    CHECK((keys_of(c2) == std::vector<std::string>{"5u", "5d"}));
    CHECK(in.down(5) == true);
}

}  // namespace

int main() {
    test_ordering_and_boundary();
    test_multi_step_boundary_progression();
    test_down_consumed_time_update();
    test_min_hold_defers_up();
    test_min_hold_redown_discards_deferred();
    test_deferred_up_then_redown_same_boundary();
    test_flush_keeps_down_state();
    test_overflow_drops_oldest();
    std::printf("[virtual input test] checks=%d failures=%d\n",
                ::tg_test::g_checks, ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}
