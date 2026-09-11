// task_runner.cpp —— TaskRunner 的启动、回收实现。
//
// 单线程、无锁：与 ipc/watcher/tween/animation 同域（主循环每帧）。
//
// 关键语义（勿改成「每帧 resume 所有 task」）：tg::task 是 lazy 协程，
// resume 一次即执行到下一个挂起点。协程挂起在 single_consumer_event 上时，
// 其后续推进由事件 set() 同步 resume 完成（见 coro.hpp）——TaskRunner 若在
// 挂起期间再次 pump，会把协程提前唤醒到事件未就绪的状态（错误）。
// 故本容器只「启动一次 + 回收完成」。

#include "trogue/task_runner.hpp"

#include <utility>  // std::move

namespace tg {

void TaskRunner::push(tg::task<> t) {
    tasks_.push_back(Entry{std::move(t), false});
}

void TaskRunner::pump_all() {
    // 就地紧凑：跳过已完成的（回收），启动未启动的。启动可能同步跑完
    // （无挂起）→ 当拍即回收；也可能挂起等事件 → 保留。
    std::size_t write = 0;
    for (std::size_t i = 0; i < tasks_.size(); ++i) {
        if (tasks_[i].t.done()) continue;   // 已完成：回收
        if (!tasks_[i].started) {
            tasks_[i].started = true;
            tasks_[i].t.pump();             // 启动：resume 到首个挂起点/完成
        }
        // 已启动但未完成 → 挂起等事件，本拍不动（事件 set 时同步 resume）。
        if (!tasks_[i].t.done()) {
            if (write != i) tasks_[write] = std::move(tasks_[i]);
            ++write;
        }
    }
    tasks_.resize(write);
}

int TaskRunner::active_count() const {
    int n = 0;
    for (const auto& e : tasks_) {
        if (!e.t.done()) ++n;
    }
    return n;
}

}  // namespace tg
