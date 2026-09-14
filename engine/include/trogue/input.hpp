#pragma once
// input.hpp —— 虚拟输入通道（确定性按键注入与固定步边界消费）。
//
// 边界：本类只管**注入通道**的排队、排序与步边界提取——与平台真实键盘无任何
// 关联（真实键盘轮询是 game 直调 raylib 的事）。它解决的问题是：自动化
// （IPC/回放/子代理试玩）以任意节奏注入按键时，事件不得在模拟步中间撕开、
// 不得迟到迟到乱序、积压可观测——一整类「注入伪影被误诊为游戏 bug」的排查
// 成本由本类消灭。
//
// 语义契约（钉死）：
// - **消费在固定步边界**：game 每执行一个模拟步，在**步执行前**调
//   `step_due(i * cfg.step_seconds)`——i 为调用方本地步序（已完成步数，每执行
//   完一步 +1，跨帧累计；多步帧内各步边界严格递增）。一次取走全部
//   `t < boundary` 的事件，按 (t, 入队序) 稳定序返回。注入落在两次边界之间
//   任意时刻都安全：最迟下一边界生效，绝不在步中间生效。
// - `down(key)` 在**消费时**更新（step_due 应用事件后置位/清位）：未消费的
//   注入不影响状态视图。flush() 清空队列（含推迟中未生效的 up）后 down()
//   **保持现值**直至下一个真实 up——flush 撤销未消费事件，不伪造按键状态。
// - min_hold_steps > 0（撕裂防护，可选项）：同键 up 若距其 down 被消费不足
//   N 步 → 推迟到恰好第 down_step+N 个边界出队（down 永不推迟）；推迟期间
//   同键新的 down 被消费 → 未生效的 up 直接丢弃（最新意图优先）。
// - 队列上限 kPendingMax：push 超限丢最旧（入队序最小）+ dropped() 计数，
//   不静默。
// - t 由调用方提供（任意单调秒表；引擎只按数值比较与排序，不解释时钟来源）；
//   t == boundary 归下一批（严格 <）。
// - key 为平台键值（int；约定采用 raylib KeyboardKey 数值，公共头不引
//   raylib 类型）。键位语义/映射归 game。
//
// 程序错误：无（全部输入合法；非法 key 值也照常排队，语义归 game）。
// 单线程：主循环推进。

#include <cstdint>     // std::int64_t / std::uint64_t
#include <utility>     // std::pair
#include <vector>

#include "trogue/config.hpp"  // kPendingMax

namespace tg {

struct InputEvent {
    double t = 0.0;   // 调用方时钟（秒；仅用于排序与边界提取）
    int key = 0;      // 平台键值（约定 raylib KeyboardKey 数值）
    bool down = false;
};

class VirtualInput {
public:
    struct Config {
        double step_seconds = 1.0 / 60.0;  // 步边界粒度（与 StepClock 同步长；
                                           // 两处配置互指对齐惯例，见 time.hpp）
        int min_hold_steps = 0;            // 同键 down→up 最小消费步间隔（0 = 透传）
    };

    VirtualInput() : VirtualInput(Config{}) {}
    explicit VirtualInput(Config cfg);

    // 注入一个按键事件。任意时刻可调；t 为调用方时钟数值（仅排序用）。
    void push(int key, bool down, double t);

    // 步边界消费：取走全部 t < boundary 的到期事件（含到期的推迟 up），按
    // (t, 入队序) 稳定序返回；应用后更新 down()。每次调用视作一个模拟步的
    // 边界（min_hold 的「步」即本调用序号）。
    std::vector<InputEvent> step_due(double boundary);

    // 清空未消费事件（队列 + 推迟中的 up）；down() 保持现值。
    void flush();

    int pending() const;                       // 未消费事件数（含推迟 up）
    std::uint64_t dropped() const { return dropped_; }
    bool down(int key) const;                  // 按键状态视图（消费时更新）

private:
    Config cfg_;
    std::uint64_t seq_ = 0;      // 入队序（稳定排序 tie-break + 丢最旧依据）
    std::uint64_t dropped_ = 0;
    std::int64_t step_index_ = -1;  // step_due 调用序号（0 起每调用 +1）

    struct Entry {
        std::uint64_t seq;
        InputEvent ev;
    };
    std::vector<Entry> queue_;   // 注入队列（push 序；出队前排序）

    // 推迟中的 up（min_hold 防撕裂）：key → 事件 + 目标边界序号
    struct DeferredUp {
        std::uint64_t seq;
        InputEvent ev;
        std::int64_t due_step;   // 应出队的 step_index_
    };
    std::vector<DeferredUp> deferred_;

    // 最近一次被消费的 down 的 step_index_（min_hold 间隔计算依据）
    std::vector<std::pair<int, std::int64_t>> down_steps_;  // key → step
    std::vector<std::pair<int, bool>> down_state_;          // key → 状态位
};

}  // namespace tg
