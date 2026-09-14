// input.cpp —— VirtualInput 实现（契约见 input.hpp 头注释）。
//
// 实现：队列 + 推迟表 + 两张小型 key→值 线性表（注入通道事件量级很小，
// 线性查找足够；不引入哈希容器依赖）。出队前对到期事件按 (t, seq) 稳定排序。

#include "trogue/input.hpp"

#include <algorithm>  // std::sort / std::stable_sort / std::find_if / std::remove_if

namespace tg {

namespace {

std::vector<std::pair<int, std::int64_t>>::iterator find_key(
    std::vector<std::pair<int, std::int64_t>>& v, int key) {
    return std::find_if(v.begin(), v.end(),
                        [key](const auto& p) { return p.first == key; });
}
std::vector<std::pair<int, bool>>::iterator find_key(
    std::vector<std::pair<int, bool>>& v, int key) {
    return std::find_if(v.begin(), v.end(),
                        [key](const auto& p) { return p.first == key; });
}
std::vector<std::pair<int, bool>>::const_iterator find_key(
    const std::vector<std::pair<int, bool>>& v, int key) {
    return std::find_if(v.begin(), v.end(),
                        [key](const auto& p) { return p.first == key; });
}

}  // namespace

VirtualInput::VirtualInput(Config cfg) : cfg_(cfg) {}

void VirtualInput::push(int key, bool down, double t) {
    if (static_cast<int>(queue_.size()) >= kPendingMax) {
        // 超限丢最旧（入队序最小 = queue_ 中 seq 最小者；push 序即 seq 序）
        auto oldest = std::min_element(
            queue_.begin(), queue_.end(),
            [](const Entry& a, const Entry& b) { return a.seq < b.seq; });
        queue_.erase(oldest);
        ++dropped_;
    }
    queue_.push_back(Entry{seq_++, InputEvent{t, key, down}});
}

std::vector<InputEvent> VirtualInput::step_due(double boundary) {
    ++step_index_;
    std::vector<InputEvent> out;

    // ① 推迟表到期的 up 先出（其原始 t 更老，先于本批新到期事件）
    for (auto it = deferred_.begin(); it != deferred_.end();) {
        if (it->due_step <= step_index_) {
            out.push_back(it->ev);
            it = deferred_.erase(it);
        } else {
            ++it;
        }
    }

    // ② 收集队列中 t < boundary 的到期事件，(t, seq) 稳定排序
    std::vector<Entry> due;
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->ev.t < boundary) {
            due.push_back(*it);
            it = queue_.erase(it);
        } else {
            ++it;
        }
    }
    std::stable_sort(due.begin(), due.end(),
                     [](const Entry& a, const Entry& b) {
                         if (a.ev.t != b.ev.t) return a.ev.t < b.ev.t;
                         return a.seq < b.seq;
                     });

    // ③ 逐事件应用（min_hold 只约束 up；down 永不推迟）
    for (const Entry& e : due) {
        if (e.ev.down) {
            out.push_back(e.ev);
            if (auto it = find_key(down_steps_, e.ev.key); it != down_steps_.end()) {
                it->second = step_index_;
            } else {
                down_steps_.emplace_back(e.ev.key, step_index_);
            }
            // 同键新的 down：丢弃推迟中未生效的 up（最新意图优先）
            deferred_.erase(
                std::remove_if(deferred_.begin(), deferred_.end(),
                               [&e](const DeferredUp& d) {
                                   return d.ev.key == e.ev.key &&
                                          d.ev.down == false;
                               }),
                deferred_.end());
        } else {
            // up：查同键最近 down 的消费步，间隔不足则推迟
            std::int64_t down_step = -1;
            if (auto it = find_key(down_steps_, e.ev.key);
                it != down_steps_.end()) {
                down_step = it->second;
            }
            const std::int64_t held = down_step < 0
                                          ? cfg_.min_hold_steps
                                          : step_index_ - down_step;
            if (cfg_.min_hold_steps > 0 && held < cfg_.min_hold_steps) {
                deferred_.push_back(
                    DeferredUp{e.seq, e.ev, down_step + cfg_.min_hold_steps});
            } else {
                out.push_back(e.ev);
            }
        }
    }

    // ④ down() 状态视图：消费时更新（含推迟到期与本批新事件）
    for (const InputEvent& ev : out) {
        if (auto it = find_key(down_state_, ev.key); it != down_state_.end()) {
            it->second = ev.down;
        } else {
            down_state_.emplace_back(ev.key, ev.down);
        }
    }
    return out;
}

void VirtualInput::flush() {
    queue_.clear();
    deferred_.clear();
    // down_state_ 不动：flush 撤销未消费事件，不伪造按键状态（契约）。
}

int VirtualInput::pending() const {
    return static_cast<int>(queue_.size() + deferred_.size());
}

bool VirtualInput::down(int key) const {
    if (auto it = find_key(down_state_, key); it != down_state_.end()) {
        return it->second;
    }
    return false;
}

}  // namespace tg
