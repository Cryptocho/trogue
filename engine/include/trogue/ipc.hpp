#pragma once
// ipc.hpp —— 无 world 的 JSON-lines 传输与 callback 分发（plan-5.5 §1/§2）。
//
// engine 只做传输与 callback 分发，**不拥有任何业务命令语义**（唯一例外：
// 传输层内置 `ping`，见 §2.3）。命令归属 game（5.6 demo handler 实现）。
//
// Ipc 为 RAII 资源类（不可拷贝、可移动；持有监听 fd、连接槽、缓冲、handler）。
// handler 内 data 只在其调用期有效；engine 在包络后释放。handler 捕获 game
// 状态；不可保存 request 引用跨调用。
//
// Debug（TROGUE_DEBUG）：完整 TCP 实现。Release：create 返回 invalid，
// valid()==false，poll/set/clear 安全 no-op（API 形状与 Debug 一致）。

#include <cstdint>   // std::uint16_t
#include <functional>
#include <memory>     // std::unique_ptr
#include <optional>
#include <string>

#include <nlohmann/json.hpp>  // 类型别名（仅本头需要；plan-5.1 §5.1）

namespace tg {

// 唯一 JSON 类型（类型别名、非继承：仅供 handler 签名使用）
using Json = nlohmann::json;

enum class IpcStatus { handled, not_handled, error };

// cmd: 请求里 "cmd" 字段（非空 string，已校验）。
// request: 完整请求 JSON（object）。data: 输出——handled 时置 JSON object。
// error: 输出——error 时置原因。
using IpcHandler = std::function<IpcStatus(
    const std::string& cmd, const Json& request, std::optional<Json>& data,
    std::string& error)>;

class Ipc {
public:
    // 实现细节：不透明（如 PIMPL 惯例——类型名可见但不可实例化/访问内容）。
    // 放 public 使 TU 内辅助函数可读实现字段（engine 内部）；消费者无法构造。
    struct Impl;

    // 绑定监听端口（本机 127.0.0.1）；失败/Release → invalid。
    static Ipc create(std::uint16_t port);

    Ipc();  // 默认构造（空实例，invalid）；定义于 cpp（Impl 完整处）
    ~Ipc();
    Ipc(const Ipc&) = delete;
    Ipc& operator=(const Ipc&) = delete;
    Ipc(Ipc&&) noexcept;
    Ipc& operator=(Ipc&&) noexcept;

    // 注册 handler（poll 外调用；替换旧 handler）。
    void set_handler(IpcHandler handler);
    void clear_handler();

    // 每帧：accept 到 EAGAIN → 轮询 active 连接（读→处理→写响应）。
    void poll();

    bool valid() const;

private:
    explicit Ipc(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

}  // namespace tg