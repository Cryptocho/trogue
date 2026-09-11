// hotreload.cpp —— Watcher 实现。
//
// Linux（TROGUE_DEBUG）：inotify 监听目录，150ms 防抖尾沿补触发；poll() 经
// detail::classify_event_name 过滤合法 .json basename（裸名）。非 Linux /
// Release：create 返回 invalid，poll() 恒 nullopt（安全 no-op）。
#include "trogue/hotreload.hpp"

#include <cstddef>
#include <cstring>
#include <optional>
#include <string>

#include "trogue/config.hpp"

#ifdef TROGUE_DEBUG

#include <sys/inotify.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <ctime>
#include <utility>  // std::move

#include "raylib.h"  // TraceLog

namespace tg {

namespace detail {

// basename 分类纯函数（不依赖 inotify 类型，便于单测）。
// 输入 inotify 名称区字节（含尾部 NUL/padding）；输出合法 .json basename 或空。
std::optional<std::string> classify_event_name(const char* zone, std::size_t len) {
    if (len == 0) return std::nullopt;
    // 以 len 为上界扫描终止 NUL（strnlen 逻辑）
    std::size_t n = 0;
    while (n < len && zone[n] != '\0') ++n;
    if (n == len) return std::nullopt;  // 无终止（名称区被截断）
    // 名称 = [0, n)；尾部 padding（≥ n 的 NUL）忽略
    if (n == 0) return std::nullopt;
    const std::string_view name(zone, n);

    // 中段 NUL=0; 名含 0x00 → 已由终止扫描兜住（n 处即首个 NUL）
    if (name.find('/') != std::string_view::npos) return std::nullopt;
    if (name == "." || name == "..") return std::nullopt;
    if (name.size() >= static_cast<std::size_t>(kNameMax)) return std::nullopt;

    // 后缀：仅精确 ASCII 小写 .json（大小写不折叠，长度 ≥6）
    constexpr std::string_view kSuffix = ".json";
    if (name.size() < kSuffix.size() + 1) return std::nullopt;
    if (name.substr(name.size() - kSuffix.size()) != kSuffix) return std::nullopt;
    return std::string(name);
}

}  // namespace detail

struct Watcher::Impl {
    int inotify_fd = -1;
    int watch_fd = -1;
    std::string pending;        // 合并 pending：最新合法 basename
    long long last_event_ms = 0;
    bool armed = false;         // 尾沿补触发标志
};

namespace {

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

Watcher::Watcher() = default;
Watcher::Watcher(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Watcher::~Watcher() { shutdown(); }
Watcher::Watcher(Watcher&&) noexcept = default;
Watcher& Watcher::operator=(Watcher&&) noexcept = default;

Watcher Watcher::create(std::string_view dir) {
    auto im = std::make_unique<Impl>();
    im->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (im->inotify_fd < 0) return Watcher{};
    im->watch_fd = inotify_add_watch(im->inotify_fd, std::string(dir).c_str(),
                                     IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE |
                                         IN_MODIFY);
    if (im->watch_fd < 0) {
        ::close(im->inotify_fd);
        im->inotify_fd = -1;
        return Watcher{};
    }
    return Watcher(std::move(im));
}

bool Watcher::valid() const { return impl_ && impl_->inotify_fd >= 0; }

std::optional<std::string> Watcher::poll() {
    if (!valid()) return std::nullopt;

    // 防抖尾沿触发：窗口内合并 pending；窗口结束（距首事件 ≥ debounce）后
    // 的下一次 poll 报告单个最新 basename。C 版「150ms 防抖 + 尾沿补触发」。
    const long long now = now_ms();

    // 读尽当前可读事件（非阻塞）
    std::string latest;
    bool saw_event = false;
    alignas(inotify_event) char buf[8192];
    for (;;) {
        const ssize_t n = read(impl_->inotify_fd, buf, sizeof(buf));
        if (n > 0) {
            std::size_t off = 0;
            while (off < static_cast<std::size_t>(n)) {
                auto* ev = reinterpret_cast<inotify_event*>(buf + off);
                const std::size_t evsz = sizeof(inotify_event) + ev->len;
                if (off + evsz > static_cast<std::size_t>(n)) break;  // 截断防御
                auto cls = detail::classify_event_name(ev->name, ev->len);
                if (cls) latest = std::move(*cls);  // 取「最新」合法 basename
                off += evsz;
            }
            if (!latest.empty()) saw_event = true;
            continue;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            TraceLog(LOG_WARNING, "[watcher] 读取失败，停止监听");
            shutdown();
            return std::nullopt;
        }
        break;  // n==0 异常
    }

    if (saw_event) {
        // 事件到来：合并到 pending；并记录窗口起点（首个事件时）
        if (!impl_->armed) {
            impl_->last_event_ms = now;
            impl_->armed = true;
        }
        impl_->pending = std::move(latest);  // 窗口内只保留最新
        return std::nullopt;                 // 尾沿：窗口结束后才报告
    }

    // 无新事件：若窗口已结束（距首事件 ≥ debounce），报告合并的 pending
    if (impl_->armed && now - impl_->last_event_ms >= kWatchDebounceMs) {
        std::string result = std::move(impl_->pending);
        impl_->armed = false;
        impl_->pending.clear();
        return result.empty() ? std::nullopt
                              : std::optional<std::string>(std::move(result));
    }
    return std::nullopt;
}

void Watcher::shutdown() {
    if (!impl_) return;
    if (impl_->inotify_fd >= 0) {
        ::close(impl_->inotify_fd);
        impl_->inotify_fd = -1;
    }
    impl_->watch_fd = -1;
}

}  // namespace tg

#else  // ── TROGUE_DEBUG=OFF / 非 Linux：桩 ──

namespace tg {

// 桩也需要完整 Impl（unique_ptr 析构点要求完整类型）
struct Watcher::Impl {};

Watcher::Watcher() = default;
Watcher::Watcher(Watcher&&) noexcept = default;
Watcher& Watcher::operator=(Watcher&&) noexcept = default;
Watcher::~Watcher() = default;

Watcher Watcher::create(std::string_view /*dir*/) { return Watcher{}; }
std::optional<std::string> Watcher::poll() { return std::nullopt; }
void Watcher::shutdown() {}
bool Watcher::valid() const { return false; }

}  // namespace tg

#endif  // TROGUE_DEBUG