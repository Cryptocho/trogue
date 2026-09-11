#pragma once
// task_runner.hpp —— 单线程协程推进容器。
//
// 动机：引擎以 tg::task<> 表达「可等待的完成」（AnimationPlayer::done()、
// TweenManager::wait()），但 task 是 lazy 的——必须由调用方显式推进。若每个
// 消费者各自实现「容器 + 每帧 pump + 析构前取消」，属重复造轮子且易出错；
// TaskRunner 提供这一机械容器，使协程能力开箱可用。
//
// 边界：只做容器与推进，不含任何玩法决策；不替代宿主（asset/player/manager）
// 的生命周期契约——见 coro.hpp/animation.hpp：宿主必须先于协程销毁，或先取消
// 使等待即时完成。TaskRunner 只能取消它持有的 task，无法保证宿主存活；因此
// 调用方须在宿主销毁前 cancel_all()，本类析构**不**隐式代劳（隐式销毁时协程体
// 可能引用已失效宿主，属不安全）。
//
// 语义：push 收编 task 所有权；首次 pump_all 启动它（执行到首个挂起点）；
// 之后推进由**等待的事件驱动**（single_consumer_event::set() 同步 resume 等待者，
// 见 coro.hpp），TaskRunner 不再重复 resume 挂起中的协程；pump_all 每帧负责
// 启动新 task 与回收已完成的。active_count 为当前未回收 task 数。
//
// 限制：仅适用于「等待事件」型协程（AnimationPlayer::done / TweenManager::wait
// 均属此类——事件 set 时同步 resume）。需要分帧驱动的循环式协程（每帧
// co_await 某个 tick）不在本容器职责内，应用场景尚无此需求。

#include <vector>

#include "trogue/coro.hpp"  // tg::task

namespace tg {

class TaskRunner {
public:
    TaskRunner() = default;
    // 析构：不隐式 cancel_all（协程体可能引用已失效宿主，隐式销毁不安全）。
    // 调用方须在宿主销毁前显式 cancel_all()。
    ~TaskRunner() = default;
    TaskRunner(const TaskRunner&) = delete;
    TaskRunner& operator=(const TaskRunner&) = delete;
    TaskRunner(TaskRunner&&) = default;
    TaskRunner& operator=(TaskRunner&&) = default;

    // 收编一个 task（所有权移交 runner；惰性，首次 pump_all 启动到首个挂起点）。
    void push(tg::task<> t);

    // 每帧调用：启动尚未启动的 task（resume 到首个挂起点/完成），并回收已完成
    // 的 task。**不**重复 resume 已挂起的 task（等待由事件同步驱动，见头注释）。
    void pump_all();

    // 取消并销毁全部 task（硬销毁协程帧）。
    // 契约：调用后不得再由宿主 set() 这些 task 所等待的事件——销毁帧会让
    // 宿主事件里的等待者指针悬垂，之后 set() 将 resume 已销毁帧（UB）。
    // 典型安全顺序：cancel_all() → 然后才销毁宿主。若需「协程正常收尾」，
    // 应改用宿主自身的取消（如 TweenManager::cancel / AnimationPlayer::stop，
    // 它们先 set 事件使协程同步完成），随后 pump_all 回收。
    void cancel_all() { tasks_.clear(); }

    // 当前未回收（未完成）task 数。
    int active_count() const;

private:
    struct Entry {
        tg::task<> t;
        bool started = false;  // 已启动（首次 resume 过）；避免重复 resume 挂起协程
    };
    std::vector<Entry> tasks_;
};

}  // namespace tg
