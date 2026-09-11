// event_bus.hpp —— game 层进程内事件总线。
//
// 对齐原版 trogue-orign/src/core/events.lua 的最小核心：
//   - on(name, handler, priority) 返回订阅句柄，off(handle) 注销；
//   - priority 越小越先执行，同优先级按注册序（稳定排序）；
//   - dirty 标记：on/off O(1)，emit 时延迟重建有序表；
//   - emit 先快照后调用：handler 内 on/off/emit 重入不迭代器失效
//     （tween 回调重入 UB 教训的直接应用）。
//
// 与原版的差异（无消费者不做）：emitTo/emitToMany/child。
// 纯逻辑零 IPC 依赖（桥接在 main 层完成），单线程主循环内使用，无锁。
// 载荷 = tg::Json（与 IPC wire 同构，桥接零转换；顶层 entity/source/target =
// 字符串 id，与 subscribe filter「顶层字段等值匹配」口径直接兼容）。

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "trogue/ipc.hpp"  // tg::Json

namespace game {

using EventData = tg::Json;
using EventHandler = std::function<void(const EventData&)>;
using Subscription = std::uint64_t;  // 0 = 无效句柄

class EventBus {
public:
    // 注册监听；priority 越小越先，同优先级按注册序。返回句柄供 off 使用。
    Subscription on(std::string_view name, EventHandler handler, int priority = 0) {
        const Subscription id = next_id_++;
        Entry e;
        e.id = id;
        e.priority = priority;
        e.seq = next_seq_++;
        e.handler = std::move(handler);
        listeners_[std::string(name)].push_back(std::move(e));
        dirty_[std::string(name)] = true;
        return id;
    }

    // 按句柄注销；无效/未知句柄为 no-op。只标 dirty，不立即重建（延迟到 emit）。
    void off(Subscription id) {
        if (id == 0) return;
        for (auto& [name, list] : listeners_) {
            for (std::size_t i = 0; i < list.size(); ++i) {
                if (list[i].id == id) {
                    list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
                    dirty_[name] = true;
                    return;
                }
            }
        }
    }

    // 广播：重建（若 dirty）→ 拷贝执行序快照 → 逐个调用。
    // 快照是值拷贝：handler 内 on/off/嵌套 emit 同名事件均不影响本次遍历。
    void emit(std::string_view name, const EventData& data) {
        const std::string key(name);
        if (auto d = dirty_.find(key); d != dirty_.end() && d->second) rebuild(key);
        std::vector<Entry> run;
        if (auto s = sorted_.find(key); s != sorted_.end()) run = s->second;
        for (const auto& e : run) e.handler(data);
    }

    // 当前某事件的注册数（测试/诊断用）。
    std::size_t count(std::string_view name) const {
        const auto it = listeners_.find(std::string(name));
        return it == listeners_.end() ? 0 : it->second.size();
    }

private:
    struct Entry {
        Subscription id = 0;
        int priority = 0;
        std::uint64_t seq = 0;  // 注册序（同优先级稳定排序键）
        EventHandler handler;
    };

    // 重建执行序缓存：按 (priority, seq) 稳定排序（对齐原版 dirty-flag 优化）。
    void rebuild(const std::string& name) {
        auto it = listeners_.find(name);
        std::vector<Entry> sorted;
        if (it != listeners_.end()) sorted = it->second;
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const Entry& a, const Entry& b) {
                             if (a.priority != b.priority) return a.priority < b.priority;
                             return a.seq < b.seq;
                         });
        sorted_[name] = std::move(sorted);
        dirty_[name] = false;
    }

    std::unordered_map<std::string, std::vector<Entry>> listeners_;  // 注册序
    std::unordered_map<std::string, std::vector<Entry>> sorted_;     // 执行序缓存
    std::unordered_map<std::string, bool> dirty_;
    Subscription next_id_ = 1;
    std::uint64_t next_seq_ = 0;
};

}  // namespace game
