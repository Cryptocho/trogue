// time.cpp —— StepClock 实现（契约见 time.hpp 头注释；此处只放算式）。
//
// 双池实现：授步池 = 整步计数（credit 路径全程整数，无浮点回差）；时间池 =
// 累计秒。每 tick 弹出 min(两池整步合计, max_steps)，授步池优先扣减；时间池
// 的超额整步丢弃（护栏，上限 = 时间池现有整步数），丢弃后时间池 < step。

#include "trogue/time.hpp"

#include <algorithm>  // std::min
#include <cmath>      // std::floor

#include "raylib.h"   // TraceLog（负 dt 记日志）

namespace tg {

StepClock::StepClock(double step_seconds, int max_steps_per_tick)
    : step_seconds_(step_seconds), max_steps_(max_steps_per_tick) {
    // 程序错误（调用方契约）：不设断言，抛异常对齐 coro.hpp 策略（NDEBUG 不分叉）。
    if (!(step_seconds_ > 0.0) || max_steps_ <= 0) {
        throw std::logic_error(
            "StepClock: step_seconds 必须 >0 且 max_steps_per_tick 必须 >0");
    }
}

StepClock::Tick StepClock::tick(double frame_dt) {
    Tick out;
    // 暂停 = 停止时间通道：不累加、不消费、alpha 恒 0；授步池照常弹出。
    if (!paused_) {
        if (frame_dt < 0.0) {
            TraceLog(LOG_WARNING, "[time] StepClock.tick 负 dt 按 0 处理");
            frame_dt = 0.0;
        }
        time_pool_ += frame_dt;
    }

    // 时间池整步数（先钳到 max 量级再 cast，防天文级 dt 的 int 溢出 UB；
    // pop 反正被 max 钳制，钳早钳晚语义一致且确定）。
    const double time_whole_d = std::floor(time_pool_ / step_seconds_);
    const std::int64_t whole =
        static_cast<std::int64_t>(
            std::min(time_whole_d, static_cast<double>(max_steps_))) +
        credit_steps_;
    const int pop = static_cast<int>(std::min<std::int64_t>(whole, max_steps_));

    // 授步池优先扣减：弹出的步先吃授步池（整数精确），不足部分才动时间池。
    const int from_credit =
        static_cast<int>(std::min<std::int64_t>(credit_steps_, pop));
    credit_steps_ -= from_credit;
    time_pool_ -= static_cast<double>(pop - from_credit) * step_seconds_;
    if (time_pool_ < 0.0) time_pool_ = 0.0;  // floor 回差防御（ε 级，确定性不变）

    // 溢出丢弃：仅时间通道的超额整步（护栏；授步池余量永不动）。
    // 扣减后时间池 < step 恒成立（本 tick 未及弹出的整步全部丢弃，无债务）。
    // 丢弃数同样先钳再 cast（同上防溢出；极端 dt 下逐 tick 限量丢弃，确定）。
    const int time_whole_after = static_cast<int>(std::min(
        std::floor(time_pool_ / step_seconds_), static_cast<double>(max_steps_)));
    if (time_whole_after > 0) {
        time_pool_ -= static_cast<double>(time_whole_after) * step_seconds_;
        ++overflow_count_;
        out.overflowed = true;
    }

    out.steps = pop;
    out.alpha = paused_ ? 0.0 : time_pool_ / step_seconds_;
    total_steps_ += static_cast<std::uint64_t>(pop);
    return out;
}

void StepClock::credit(int steps) {
    if (steps <= 0) return;
    credit_steps_ += steps;
}

void StepClock::pause() { paused_ = true; }
void StepClock::resume() { paused_ = false; }

}  // namespace tg
