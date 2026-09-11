#pragma once
// coro.hpp —— 自研最小协程原语（header-only，零第三方依赖）。
//
// 选型：不引入第三方协程库——cppcoro 上游停留在 C++17 TS 的
// `<experimental/coroutine>`（GCC 12 起移除），GCC 15 下不可编译；
// 故自研本组原语（已通过 GCC 15 + ASan/UBSan 原型验证）。
//
// 提供：
//   tg::task<T> / tg::task<>   : lazy 协程载体，单线程显式推进（start/pump）
//   tg::single_consumer_event  : 单消费者事件（auto-reset），供 co_await 完成信号
//   tg::generator<T>           : 惰性值产出
//
// 使用契约：
// 1. 单线程推进：演出协程由 game 主循环显式推进（start/pump），不另起线程。
//    数据驱动播放器（Animation/Tween 核心）不依赖协程。
// 2. 生命周期：等待者持有对宿主（asset/player/manager）的非拥有引用；
//    宿主必须先于协程销毁，或先取消使等待即时完成。捕获的 this/对象引用
//    在演出任务存活期间必须有效——由 game 保证；推进容器由引擎提供
//    （tg::TaskRunner：启动/回收；见 task_runner.hpp）。
// 3. 等待者纪律：同一 single_consumer_event 至多一个等待协程。多个等待者
//    同时挂起时行为未定义（实现为后登记覆盖先登记，不检查不唤醒旧者）。
//    禁止未经确认即放任多等待者。
// 4. 异常不跨 API 抛：协程体内异常先存入 promise，由 result()（或 generator
//    迭代）重抛；result() 在违反调用约定（空/未完成 task）时抛
//    std::logic_error，属程序错误信号，非可预期失败路径。

#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>  // std::logic_error
#include <utility>

namespace tg {

template <typename T = void>
class task;

namespace detail {

template <typename T>
class task_awaiter;  // 前置声明：task 需要它作返回类型/friend

// ---------- task_promise：通用（T != void）----------
template <typename T>
class task_promise {
public:
    task_promise() = default;
    task_promise(const task_promise&) = delete;
    task_promise& operator=(const task_promise&) = delete;  // 协程帧唯一归属
    ~task_promise() = default;

    task<T> get_return_object() noexcept;  // 类外定义（task 完整后）

    // lazy：创建时不执行，等待 start()/pump()/co_await 显式推进
    std::suspend_always initial_suspend() const noexcept { return {}; }

    struct final_awaiter {
        bool await_ready() const noexcept { return false; }
        // 完成时对称转移到 continuation（若有），否则交还调用者
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<task_promise> h) noexcept {
            auto& c = h.promise().continuation_;
            return c ? c : std::noop_coroutine();
        }
        void await_resume() const noexcept {}
    };
    final_awaiter final_suspend() noexcept { return {}; }

    void return_value(T v) { result_.emplace(std::move(v)); }
    // 异常先存起来，由 take_result() 重抛（契约 4）
    void unhandled_exception() noexcept { error_ = std::current_exception(); }

    T take_result() {
        if (error_) std::rethrow_exception(error_);
        return std::move(*result_);  // 仅协程已完成时调用
    }

    std::coroutine_handle<> continuation_ = nullptr;  // 谁在等本任务完成

private:
    std::optional<T> result_;
    std::exception_ptr error_;
};

// ---------- task_promise<void> ----------
template <>
class task_promise<void> {
public:
    task_promise() = default;
    task_promise(const task_promise&) = delete;
    task_promise& operator=(const task_promise&) = delete;
    ~task_promise() = default;

    task<> get_return_object() noexcept;  // 类外定义

    std::suspend_always initial_suspend() const noexcept { return {}; }

    struct final_awaiter {
        bool await_ready() const noexcept { return false; }
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<task_promise> h) noexcept {
            auto& c = h.promise().continuation_;
            return c ? c : std::noop_coroutine();
        }
        void await_resume() const noexcept {}
    };
    final_awaiter final_suspend() noexcept { return {}; }

    void return_void() noexcept {}
    void unhandled_exception() noexcept { error_ = std::current_exception(); }

    void take_result() {
        if (error_) std::rethrow_exception(error_);
    }

    std::coroutine_handle<> continuation_ = nullptr;

private:
    std::exception_ptr error_;
};

}  // namespace detail

// ---------- task<T> ----------
template <typename T>
class task {
public:
    using promise_type = detail::task_promise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    task() noexcept = default;
    explicit task(handle_type h) noexcept : coro_(h) {}
    ~task() {
        if (coro_) coro_.destroy();  // 协程帧唯一所有权；析构释放
    }
    task(const task&) = delete;
    task& operator=(const task&) = delete;
    task(task&& o) noexcept : coro_(o.coro_) { o.coro_ = nullptr; }
    task& operator=(task&& o) noexcept {
        if (this != &o) {
            if (coro_) coro_.destroy();
            coro_ = o.coro_;
            o.coro_ = nullptr;
        }
        return *this;
    }

    // ---- 单线程显式推进（契约 1） ----
    bool done() const noexcept { return !coro_ || coro_.done(); }
    // 推进一次：resume 到下一个挂起点或完成；返回是否仍未完成
    bool pump() {
        if (done()) return false;
        coro_.resume();
        return !done();
    }
    void start() noexcept {
        if (coro_ && !coro_.done()) coro_.resume();
    }

    // 取出结果（仅已完成时调用；`task&&` 所有权语义）。异常在此重抛（契约 4）。
    T result() && {
        if (!coro_) throw std::logic_error("task::result on empty task");
        return coro_.promise().take_result();
    }

    detail::task_awaiter<T> operator co_await() && noexcept;  // 类外定义

private:
    friend class detail::task_awaiter<T>;
    handle_type coro_ = nullptr;
};

// ---------- task<void> ----------
template <>
class task<void> {
public:
    using promise_type = detail::task_promise<void>;
    using handle_type = std::coroutine_handle<promise_type>;

    task() noexcept = default;
    explicit task(handle_type h) noexcept : coro_(h) {}
    ~task() {
        if (coro_) coro_.destroy();
    }
    task(const task&) = delete;
    task& operator=(const task&) = delete;
    task(task&& o) noexcept : coro_(o.coro_) { o.coro_ = nullptr; }
    task& operator=(task&& o) noexcept {
        if (this != &o) {
            if (coro_) coro_.destroy();
            coro_ = o.coro_;
            o.coro_ = nullptr;
        }
        return *this;
    }

    bool done() const noexcept { return !coro_ || coro_.done(); }
    bool pump() {
        if (done()) return false;
        coro_.resume();
        return !done();
    }
    void start() noexcept {
        if (coro_ && !coro_.done()) coro_.resume();
    }

    void result() && {
        if (!coro_) throw std::logic_error("task::result on empty task");
        coro_.promise().take_result();
    }

    detail::task_awaiter<void> operator co_await() && noexcept;  // 类外定义

private:
    friend class detail::task_awaiter<void>;
    handle_type coro_ = nullptr;
};

namespace detail {

// ---------- task_promise::get_return_object（类外定义）----------
template <typename T>
task<T> task_promise<T>::get_return_object() noexcept {
    return task<T>{std::coroutine_handle<task_promise>::from_promise(*this)};
}

// 显式特化的成员类外定义：不加 template<> 前缀（GCC/Clang 一致）。
// 非模板成员类外定义非隐式 inline：必须显式 inline 避免多 TU 强符号重复。
inline task<> task_promise<void>::get_return_object() noexcept {
    return task<>{std::coroutine_handle<task_promise>::from_promise(*this)};
}

// ---------- task_awaiter：co_await task<T> ----------
template <typename T>
class task_awaiter {
public:
    explicit task_awaiter(task<T>&& t) noexcept : t_(std::move(t)) {}
    task_awaiter(const task_awaiter&) = delete;
    task_awaiter& operator=(const task_awaiter&) = delete;

    bool await_ready() const noexcept { return !t_.coro_; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
        // 当前协程登记为子任务的 continuation；对称转移执行子任务
        t_.coro_.promise().continuation_ = h;
        return t_.coro_;
    }
    T await_resume() { return std::move(t_).result(); }

private:
    task<T> t_;
};

}  // namespace detail

// ---------- task::operator co_await（类外定义）----------
template <typename T>
detail::task_awaiter<T> task<T>::operator co_await() && noexcept {
    return detail::task_awaiter<T>{std::move(*this)};
}

// task<void> 是显式特化类：成员类外定义不加 template<> 前缀；需 inline（见上）。
inline detail::task_awaiter<void> task<void>::operator co_await() && noexcept {
    return detail::task_awaiter<void>{std::move(*this)};
}

// ---------- single_consumer_event：单消费者事件（auto-reset）----------
// 语义对齐 cppcoro::single_consumer_event：至多一个等待者（契约 3）；
// set() 同步 resume 等待者；await_resume 消费并复位（auto-reset）。
class single_consumer_event {
public:
    explicit single_consumer_event(bool initially_set = false) noexcept
        : ready_(initially_set) {}

    bool is_set() const noexcept { return ready_; }

    void set() noexcept {
        ready_ = true;
        std::coroutine_handle<> w = waiter_;
        if (w) {
            waiter_ = nullptr;
            w.resume();  // 单线程：同步推进等待者（嵌套 resume）
        }
    }

    struct awaiter {
        single_consumer_event& ev;
        bool await_ready() const noexcept { return ev.ready_; }
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            ev.waiter_ = h;  // 单消费者：登记唯一等待者（契约 3，不检查旧者）
            return true;     // 挂起，等待 set()
        }
        void await_resume() noexcept { ev.ready_ = false; }  // auto-reset 消费
    };
    awaiter operator co_await() noexcept { return awaiter{*this}; }

private:
    bool ready_ = false;
    std::coroutine_handle<> waiter_ = nullptr;
};

// ---------- generator<T> ----------
// 惰性值产出：begin() 推进到首个 co_yield；++ 推进到下一个；协程内异常在
// 推进处重抛（契约 4）。single-pass（input iterator 语义），勿重复遍历。
template <typename T>
class generator {
public:
    struct promise_type {
        promise_type() = default;
        ~promise_type() = default;
        promise_type(const promise_type&) = delete;
        promise_type& operator=(const promise_type&) = delete;

        generator get_return_object() noexcept {
            return generator{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { error_ = std::current_exception(); }
        std::suspend_always yield_value(T v) noexcept {
            current_ = std::move(v);
            return {};
        }

        std::optional<T> current_;  // optional：区分「尚未产出」
        std::exception_ptr error_;
    };
    using handle_type = std::coroutine_handle<promise_type>;

    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = std::remove_cv_t<T>;
        using difference_type = std::ptrdiff_t;
        using pointer = const value_type*;
        using reference = const value_type&;

        iterator() noexcept = default;
        explicit iterator(handle_type h) noexcept : h_(h) {}

        bool operator==(const iterator& o) const noexcept { return h_ == o.h_; }
        bool operator!=(const iterator& o) const noexcept { return !(*this == o); }

        reference operator*() const noexcept {
            return h_.promise().current_.value();
        }
        pointer operator->() const noexcept {
            return std::addressof(h_.promise().current_.value());
        }

        iterator& operator++() {
            if (!h_) return *this;
            h_.resume();
            if (h_.promise().error_)
                std::rethrow_exception(h_.promise().error_);
            if (h_.done()) h_ = nullptr;  // 消费到结束 → end
            return *this;
        }
        void operator++(int) { (void)++*this; }

    private:
        handle_type h_ = nullptr;
    };

    generator() noexcept = default;
    explicit generator(handle_type h) noexcept : h_(h) {}
    ~generator() {
        if (h_) h_.destroy();
    }
    generator(const generator&) = delete;
    generator& operator=(const generator&) = delete;
    generator(generator&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
    generator& operator=(generator&& o) noexcept {
        if (this != &o) {
            if (h_) h_.destroy();
            h_ = o.h_;
            o.h_ = nullptr;
        }
        return *this;
    }

    iterator begin() {
        if (h_) {
            h_.resume();  // 推进到第一个 yield（或结束）
            if (h_.promise().error_)
                std::rethrow_exception(h_.promise().error_);
            if (h_.done()) return iterator{};  // 空产出 → end
        }
        return iterator{h_};
    }
    iterator end() const noexcept { return iterator{}; }

private:
    handle_type h_ = nullptr;
};

}  // namespace tg