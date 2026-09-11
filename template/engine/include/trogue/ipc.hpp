#pragma once
// ipc.hpp —— 无 world 的 JSON-lines 传输、事件通道与 callback 分发。
//
// engine 只做传输、事件通道与 callback 分发，**不拥有任何业务命令语义**（传输层
// 保留命令：`ping` 与 `subscribe`/`unsubscribe`/`connections`——订阅表为传输层
// 自有状态，同样不含业务语义）。命令语义归属 game（demo handler 实现）。
//
// Ipc 为 RAII 资源类（不可拷贝、可移动；持有监听 fd、连接槽、订阅表、handler）。
// handler 内 data 只在其调用期有效；engine 在包络后释放。handler 捕获 game
// 状态；不可保存 request 引用跨调用。
//
// 事件通道：game 调用 publish() 向订阅连接直写一行
// `{"ok":true,"event":E,"data":D}`（判别式：响应永远不含顶层 event 键）。仅主线程
// 调用（与 poll 同域，单线程无锁）。**断开即订阅清零**——对端关闭/写失败/主动
// disconnect 三条路径统一走 close_slot。慢消费者（停止读取）写遇 EAGAIN 即被断开
// （自愈；无出站队列）。断开时对端最后可能见「残缺行（无 \n）+ EOF」。
//
// Debug（TROGUE_DEBUG）：完整 TCP 实现。Release：create 返回 invalid，
// valid()==false，poll/set/clear/publish/disconnect 安全 no-op（API 形状与 Debug
// 一致）。

#include <cstdint>   // std::uint64_t / std::uint16_t
#include <functional>
#include <memory>     // std::unique_ptr
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>  // 类型别名（仅本头需要）

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

// 订阅记录快照（值类型）。
struct IpcSubscription {
    std::string event;
    Json filter;  // null = 无过滤（wire 上省略 filter 键，引擎内 null↔省略 双向
                  // 转换）；否则 object——对事件 data 顶层字段做 JSON 等值匹配
                  // （多键 AND、缺键不匹配、数字按数值相等），engine 不识任何
                  // 键的语义
};

// 连接快照（值类型）。
struct IpcConnInfo {
    std::uint64_t conn = 0;  // 0 = 无效；连接 id 自 accept 起单调递增、断开不复用
    std::vector<IpcSubscription> events;
};

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

    // ── 事件通道（仅主线程调用，与 poll/tick 同域） ──

    // 向订阅了 event 的连接广播一行 {"ok":true,"event":E,"data":D}（非阻塞直写；
    // 收件人 = 存在「同名且无 filter 或 filter 匹配 data」记录的连接）。
    // data 必须为 object：非 object / 序列化失败 → TraceLog 警告并跳过本事件
    // （不写任何字节）；行超 64KB 上限 → 断开该事件全部 filter 匹配订阅者（事件
    // 无兜底行，防破坏 event 判别式）；EAGAIN/写失败 → 断开该连接（慢消费者自愈）。
    void publish(std::string_view event, const Json& data);

    // 主动断开指定连接（自动清订阅）；找不到 → false（幂等安全）。
    bool disconnect(std::uint64_t conn_id);

    // 连接快照（含未订阅连接；events 为该连接当前订阅集）。
    std::vector<IpcConnInfo> connections() const;

    bool valid() const;

private:
    explicit Ipc(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

}  // namespace tg
