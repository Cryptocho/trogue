#pragma once
// hotreload.hpp —— 文件变化通知（Watcher；plan-5.5 §3）。
//
// Watcher 只报告监听目录内安全的 .json basename（裸名，无目录前缀、无 `/`）；
// 150ms 防抖尾沿补触发（沿用 C 版定案）。engine 不拼路径：path 拼接归 game
// （game 用自身固定前缀如 assets/scenes + '/' + basename 组 load 路径）。
//
// Release / 非 Linux：create 返回 invalid，poll() 恒 nullopt（安全 no-op）。

#include <memory>  // std::unique_ptr
#include <optional>
#include <string>
#include <string_view>

namespace tg {

class Watcher {
public:
    // 监听目录；无效目录 / 非 Linux → invalid。
    static Watcher create(std::string_view dir);

    Watcher();  // 默认构造（空实例，invalid）；定义于 cpp（Impl 完整处）
    ~Watcher();
    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;
    Watcher(Watcher&&) noexcept;
    Watcher& operator=(Watcher&&) noexcept;

    bool valid() const;

    // 报告一个去抖后的合法 .json basename（裸名）；无事件/无效 → nullopt。
    std::optional<std::string> poll();

    // 释放在 inotify fd（缺省析构亦释放；显式调用可提前）。
    void shutdown();

private:
    struct Impl;
    explicit Watcher(std::unique_ptr<Impl>);  // 见 cpp（不透明指针）
    std::unique_ptr<Impl> impl_;
};

}  // namespace tg