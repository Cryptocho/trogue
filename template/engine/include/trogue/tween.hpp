#pragma once
// tween.hpp —— 数值/位置/颜色补间执行原语（plan-5.4 §4）。
//
// 边界（§1/§4）：值补间器，不持对象指针——描述 from→to、时长、延迟、缓动、
// 循环；每次采样把当前值交给回调，由 game 应用到自己的对象。engine 不把
// Tween 与任何实体/系统耦合；触发、目标值与应用层归 game。
//
// 纪律（§4.2 / §5/§6）：无限循环补间须由 game 显式 cancel（勿在 on_complete
// 泄漏）；`wait(id)` 只等「完成」一次（cancel/不存在/已完成 → 立即完成）；
// 同一 wait(id) 事件至多一个等待协程（single_consumer）。宿主（manager）必须
// 先于协程销毁，或先 cancel_all 使等待即时完成——done 信号用 shared_ptr 持有，
// 使已取消后 resume 安全（5.1 §5.2 契约）。
//
// 实现（风格总纲无虚函数）：三种值类型各持一个独立表，id 共用自增空间；
// 无继承/无类型擦除，tick 分别推进。

#include <cstdint>      // std::uint64_t
#include <functional>
#include <memory>       // std::shared_ptr / unique_ptr
#include <utility>      // std::move
#include <unordered_map>

#include "trogue/coro.hpp"   // tg::task / single_consumer_event
#include "trogue/types.hpp"  // Vec2 / Color

namespace tg {

// ════════════════════ 缓动 ════════════════════

enum class Easing {
    linear,
    quad_in, quad_out, quad_in_out,
    cubic_out,
    sine_out,
};

// ════════════════════ 补间规格 ════════════════════

struct TweenSpec {
    double duration = 1.0;   // 时长（秒；必须 >0）
    double delay = 0.0;      // 延迟（秒；>=0）
    int repeats = 0;         // 0 = 一次；<0 = 无限（配合 cancellation）
    Easing easing = Easing::linear;
};

// ════════════════════ TweenManager ════════════════════

class TweenManager {
public:
    using Id = std::uint64_t;

    template <typename T>
    struct Sample {
        T value;
        double t;  // 归一化进度 ∈ [0,1]（不含 delay）
    };

    TweenManager() = default;
    ~TweenManager() = default;
    TweenManager(const TweenManager&) = delete;
    TweenManager& operator=(const TweenManager&) = delete;

    template <typename T>
    using UpdateFn = std::function<void(const Sample<T>&)>;
    using CompleteFn = std::function<void()>;

    Id add_float(float from, float to, TweenSpec spec, UpdateFn<float> on_update,
                 CompleteFn on_complete = {});
    Id add_vec2(Vec2 from, Vec2 to, TweenSpec spec, UpdateFn<Vec2> on_update,
                CompleteFn on_complete = {});
    Id add_color(Color from, Color to, TweenSpec spec, UpdateFn<Color> on_update,
                 CompleteFn on_complete = {});

    // 取消补间：后续不再回调；等待者即时完成（alive=false）。已完成的 id 返回 false。
    bool cancel(Id id);
    void cancel_all();

    // 推进全部活动补间（单线程调用方每帧）。dt 可为固定步长；
    // 完成在越过 duration 的当次 tick 触发（不补中间帧）。
    void tick(double dt);

    // id 对应的补间是否仍在活动（未完成/未取消）。
    bool alive(Id id) const;

    // 等待某补间完成（演出脚本）；cancel/不存在/已完成 → 立即完成。
    // single_consumer 纪律：同一 wait(id) 至多一个等待协程。
    tg::task<> wait(Id id) const;

private:
    // 单条补间公共形态（值类型具体字段由使用处内联，见 add_* 实现）。
    struct TweenBase {
        TweenSpec spec;
        double time = 0.0;       // 已耗时（秒）；delay 计入但仍处于前段
        double progress = 0.0;   // 归一化进度 [0,1]（跳过 delay）
        bool running = true;     // 活动（未完成/未取消）
        bool cancelled = false;
        std::shared_ptr<single_consumer_event> done;  // 完成信号（wait 持份）
    };

    // 三张表：值类型字段存放在对应 struct 里（get_update/get_complete）。
    struct FloatSlot : TweenBase {
        float from = 0, to = 0;
        UpdateFn<float> update;
        CompleteFn complete;
        int repeats_left = 0;
    };
    struct Vec2Slot : TweenBase {
        Vec2 from, to;
        UpdateFn<Vec2> update;
        CompleteFn complete;
        int repeats_left = 0;
    };
    struct ColorSlot : TweenBase {
        Color from, to;
        UpdateFn<Color> update;
        CompleteFn complete;
        int repeats_left = 0;
    };

    std::unordered_map<Id, FloatSlot> floats_;
    std::unordered_map<Id, Vec2Slot> vec2s_;
    std::unordered_map<Id, ColorSlot> colors_;
    Id next_id_ = 1;

    Id alloc_id() { return next_id_++; }

    // 表推进核心（tick 的公共实现）：**两阶段**——先只推进/erase（不调用
    // 任何用户回调），把 update/complete/done 收集为延迟动作，循环结束后
    // 统一执行。否则用户回调内 add_*/cancel_all 再入会令正在迭代的 map
    // 迭代器失效（UB，门禁 M2）。定义于 tween.cpp。
    template <typename Value, typename Slot>
    void tick_map(std::unordered_map<Id, Slot>& slots, double dt);
};

// ── 缓动/插值（detail；纯函数） ──
namespace detail {
double apply_easing(Easing e, double t);
float  lerp(float a, float b, double t);
Vec2   lerp(const Vec2& a, const Vec2& b, double t);
Color  lerp(const Color& a, const Color& b, double t);
}  // namespace detail

}  // namespace tg