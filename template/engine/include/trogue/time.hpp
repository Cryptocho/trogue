#pragma once
// time.hpp —— 固定步长模拟时钟（时间步进执行原语）。
//
// 边界：纯算术推进器，不持有游戏状态、不调用任何回调——game 每帧调 tick(dt)
// 取「本帧应推进的整步数」，自己循环执行模拟步（逻辑更新/tween/动画用固定
// dt 驱动）。alpha 是帧内小数余量（插值与否归 game，引擎只报告）。
//
// 语义契约（钉死）：
// - 对外单车道、内部双池：credit(n) 进授步池、frame_dt 累加进时间池，tick
//   统一弹出且**授步池优先扣减**。授步池余量永不被溢出丢弃——credit(n) 授的
//   步绝不丢（step_frames n 对任意 n 精确的保证）。
// - 溢出丢弃仅作用于时间通道的超额整步，且以时间池现有整步数为上限；每 tick
//   结束后时间池 < step 恒成立（不变式），因此 alpha ∈ [0,1) 恒良定。这就是
//   「螺旋死亡护栏」：突发大 dt 只丢时间，不累积债务。
// - 暂停 = 停止时间通道：不累加 frame_dt、时间池不消费（alpha 恒 0）；
//   授步池照常弹出——这是「暂停下 step_frames 精确推进」的机制。
// - 逐步对齐惯例（供注入类原语对齐步边界，见 input.hpp）：调用方维护本地
//   步序 i（已完成步数，每执行完一步 +1），**步执行前**调 step-side
//   消费（i*step_seconds 为边界）；total_steps() 仅作累计观测（多步帧内它
//   一次跳变，不能作逐步锚点）。
//
// 程序错误：step_seconds <= 0 或 max_steps_per_tick <= 0 → 构造期抛
// std::logic_error（对齐 coro.hpp 的程序错误策略，不用断言避免 NDEBUG 分叉）。
// 单线程：与引擎其余推进原语同一约定，调用方在主循环推进。

#include <cstdint>     // std::int64_t / std::uint64_t
#include <stdexcept>   // std::logic_error（构造期程序错误）

namespace tg {

class StepClock {
public:
    // 一次 tick 的结果：steps = 应推进的整步数；alpha = 时间通道小数余量
    //（暂停下恒 0）；overflowed = 本次 tick 时间通道发生溢出丢弃（护栏触发）。
    struct Tick {
        int steps = 0;
        double alpha = 0.0;
        bool overflowed = false;
    };

    // step_seconds：固定步长（秒，>0）；max_steps_per_tick：单帧步数上限
    //（>0，螺旋死亡护栏；被丢的是时间通道超额，模拟不会追赶债务）。
    explicit StepClock(double step_seconds, int max_steps_per_tick = 5);

    // 每帧一次。frame_dt = 真实帧时长（秒；负值按 0 处理并记日志）。
    // 返回本帧应推进的整步数（授步池优先弹出）；循环 tick 直至取满授步时
    // 传 dt=0 合法（只弹出已积累步）。
    Tick tick(double frame_dt);

    // 显式授步（如 IPC step_frames n）。credit<=0 无操作。授步与时间通道
    // 合并弹出，但永不因溢出丢弃。
    void credit(int steps);

    void pause();
    void resume();
    bool paused() const { return paused_; }

    double step_seconds() const { return step_seconds_; }
    std::uint64_t total_steps() const { return total_steps_; }  // 已弹出整步累计
    std::uint64_t overflow_count() const { return overflow_count_; }

private:
    double step_seconds_ = 0.0;
    int max_steps_ = 5;
    bool paused_ = false;
    std::int64_t credit_steps_ = 0;  // 授步池（**整步计数**，非秒——credit 路径
                                     // 全程整数，不受 n*step/step 除法回差影响）
    double time_pool_ = 0.0;         // 时间池（秒）；每 tick 后 < step_seconds_
    std::uint64_t total_steps_ = 0;
    std::uint64_t overflow_count_ = 0;
};

}  // namespace tg
