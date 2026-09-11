// tween.cpp —— 补间推进/采样实现。
//
// 时间推进确定性：dt 累计；完成在越过 duration 的当次 tick 触发
// （不补中间帧）。delay 计入 time 但不产出采样。
// 重复：repeats>0 = 完成后再播 N 次（总播 1+N 次）；repeats=-1 无限；
// 完成后 repeats>0 语义：下一次重播（从 0 计时）。
#include "trogue/tween.hpp"

#include <cmath>   // std::fmod
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace tg {

namespace detail {

double apply_easing(Easing e, double t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    switch (e) {
        case Easing::linear:
            return t;
        case Easing::quad_in:
            return t * t;
        case Easing::quad_out:
            return 1 - (1 - t) * (1 - t);
        case Easing::quad_in_out:
            return t < 0.5 ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2;
        case Easing::cubic_out:
            return 1 - std::pow(1 - t, 3);
        case Easing::sine_out:
            return std::sin(t * 3.141592653589793 / 2.0);
    }
    return t;
}

float lerp(float a, float b, double t) {
    return static_cast<float>(a + (b - a) * t);
}
Vec2 lerp(const Vec2& a, const Vec2& b, double t) {
    return Vec2{tg::detail::lerp(a.x, b.x, t), tg::detail::lerp(a.y, b.y, t)};
}
Color lerp(const Color& a, const Color& b, double t) {
    return Color{
        static_cast<std::uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<std::uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<std::uint8_t>(a.b + (b.b - a.b) * t),
        static_cast<std::uint8_t>(a.a + (b.a - a.a) * t),
    };
}

}  // namespace detail

// ════════════════════ add_* ════════════════════

TweenManager::Id TweenManager::add_float(float from, float to, TweenSpec spec,
                                         UpdateFn<float> on_update,
                                         CompleteFn on_complete) {
    const Id id = alloc_id();
    FloatSlot s;
    s.spec = spec;
    s.from = from;
    s.to = to;
    s.update = std::move(on_update);
    s.complete = std::move(on_complete);
    s.repeats_left = spec.repeats;
    s.done = std::make_shared<single_consumer_event>();
    floats_.emplace(id, std::move(s));
    return id;
}

TweenManager::Id TweenManager::add_vec2(Vec2 from, Vec2 to, TweenSpec spec,
                                        UpdateFn<Vec2> on_update,
                                        CompleteFn on_complete) {
    const Id id = alloc_id();
    Vec2Slot s;
    s.spec = spec;
    s.from = from;
    s.to = to;
    s.update = std::move(on_update);
    s.complete = std::move(on_complete);
    s.repeats_left = spec.repeats;
    s.done = std::make_shared<single_consumer_event>();
    vec2s_.emplace(id, std::move(s));
    return id;
}

TweenManager::Id TweenManager::add_color(Color from, Color to, TweenSpec spec,
                                         UpdateFn<Color> on_update,
                                         CompleteFn on_complete) {
    const Id id = alloc_id();
    ColorSlot s;
    s.spec = spec;
    s.from = from;
    s.to = to;
    s.update = std::move(on_update);
    s.complete = std::move(on_complete);
    s.repeats_left = spec.repeats;
    s.done = std::make_shared<single_consumer_event>();
    colors_.emplace(id, std::move(s));
    return id;
}

// ════════════════════ cancel ════════════════════

bool TweenManager::cancel(Id id) {
    auto finish_and_erase = [](auto& map, Id iid, auto& slot) {
        if (!slot.running) return false;  // 已完成/已取消 → false
        slot.running = false;
        slot.cancelled = true;
        if (slot.done) slot.done->set();  // 等待者即时完成
        map.erase(iid);
        return true;
    };
    if (auto it = floats_.find(id); it != floats_.end())
        return finish_and_erase(floats_, id, it->second);
    if (auto it = vec2s_.find(id); it != vec2s_.end())
        return finish_and_erase(vec2s_, id, it->second);
    if (auto it = colors_.find(id); it != colors_.end())
        return finish_and_erase(colors_, id, it->second);
    return false;
}

void TweenManager::cancel_all() {
    for (auto& [id, s] : floats_) { s.running = false; if (s.done) s.done->set(); }
    for (auto& [id, s] : vec2s_)  { s.running = false; if (s.done) s.done->set(); }
    for (auto& [id, s] : colors_) { s.running = false; if (s.done) s.done->set(); }
    floats_.clear();
    vec2s_.clear();
    colors_.clear();
}

// ════════════════════ tick（三张表走同一两阶段推进） ════════════════════

// 两阶段推进的一个表：
//   阶段 1：只推进内部状态（time/progress/repeats）、判定完成并 erase；
//           期间**不调用任何用户回调**，把"待触发动作"收集到 deferred。
//           这样阶段 1 结束后 map 已稳定，阶段 2 再执行用户回调——
//           回调内 add_*/cancel/cancel_all 再入 manager 不会触碰到
//           正在迭代的迭代器（无失效 UB；这也是头文件 tick 注释的承诺）。
//   阶段 2：按收集序依次执行 update 采样、complete、done 信号 set。
template <typename Value, typename Slot>
void TweenManager::tick_map(std::unordered_map<Id, Slot>& slots, double dt) {
    if (dt < 0) dt = 0;  // 负 dt 修正为 0（与原实现一致，tick 顶层一次）

    // 延迟动作：update 采样回调（含 t=1.0 完成采样）、完成回调、done 信号。
    struct Deferred {
        std::function<void()> update;   // 采样回调（捕获最终值）
        CompleteFn complete;
        std::shared_ptr<single_consumer_event> done;
    };
    std::vector<Deferred> deferred;
    deferred.reserve(slots.size() * 2);

    for (auto it = slots.begin(); it != slots.end();) {
        Slot& s = it->second;
        if (!s.running) { it = slots.erase(it); continue; }
        s.time += dt;
        const double dur = s.spec.duration;
        if (dur <= 0) {
            // 非法时长：立即完成（原语义），不采样
            s.running = false;
            deferred.push_back(Deferred{{}, std::move(s.complete), s.done});
            it = slots.erase(it);
            continue;
        }
        const double delay = s.spec.delay;
        if (s.time >= delay + dur) {
            // 本段完成：先收 t=1.0 采样，再决定重播还是收尾
            const Value v = detail::lerp(s.from, s.to,
                                         detail::apply_easing(s.spec.easing, 1.0));
            // 注意：update 采样回调**必须拷贝**进 deferred——每次 tick 都要能
            // 触发，move 会让首次采样后槽位变空（历史 bug：多 tick 只采样一次）。
            if (s.update) {
                deferred.push_back(
                    Deferred{[upd = s.update, v]() mutable {
                                 upd(Sample<Value>{v, 1.0});
                             },
                             {}, {}});
            }
            if (s.repeats_left != 0) {
                if (s.repeats_left > 0) --s.repeats_left;
                s.time = delay;  // 重播段从头（保持 delay 语义：重播走 delay）
                ++it;
                continue;
            }
            s.running = false;
            deferred.push_back(Deferred{{}, std::move(s.complete), s.done});
            it = slots.erase(it);
            continue;
        }
        // 延迟段：时间未到 delay 不采样
        if (s.time < delay) { ++it; continue; }
        const double t = (s.time - delay) / dur;
        s.progress = t;
        const Value v = detail::lerp(s.from, s.to,
                                     detail::apply_easing(s.spec.easing, t));
        if (s.update) {  // 拷贝（理由同上：update 每 tick 可用）
            deferred.push_back(Deferred{[upd = s.update, v, t]() {
                                            upd(Sample<Value>{v, t});
                                        },
                                        {}, {}});
        }
        ++it;
    }

    // 阶段 2：统一执行（map 已稳定，回调内可自由 add_*/cancel）
    for (auto& d : deferred) {
        if (d.update) d.update();
        if (d.complete) d.complete();
        if (d.done) d.done->set();
    }
}

void TweenManager::tick(double dt) {
    tick_map<float>(floats_, dt);
    tick_map<Vec2>(vec2s_, dt);
    tick_map<Color>(colors_, dt);
}

bool TweenManager::alive(Id id) const {
    if (floats_.count(id)) return floats_.at(id).running;
    if (vec2s_.count(id)) return vec2s_.at(id).running;
    if (colors_.count(id)) return colors_.at(id).running;
    return false;
}

tg::task<> TweenManager::wait(Id id) const {
    // 找对应槽的 done 信号；不存在/已完成 → 立即完成
    std::shared_ptr<single_consumer_event> done;
    if (auto it = floats_.find(id); it != floats_.end()) {
        if (!it->second.running) co_return;
        done = it->second.done;
    } else if (auto it = vec2s_.find(id); it != vec2s_.end()) {
        if (!it->second.running) co_return;
        done = it->second.done;
    } else if (auto it = colors_.find(id); it != colors_.end()) {
        if (!it->second.running) co_return;
        done = it->second.done;
    }
    if (!done) co_return;
    co_await *done;
}

}  // namespace tg