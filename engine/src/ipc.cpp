// ipc.cpp —— JSON-lines 传输与 callback 分发（plan-5.5 §2）。
//
// 状态机（§2.1）：每 poll = accept 阶段（循环至 EAGAIN；新 fd 只写 hello 不
// recv；满 8 关闭；hello 失败关不占槽）→ 轮询阶段（固定 slot 序逐行处理）。
// 行分帧（§2.2）：线上字节计数含 \r 上限 kIpcLineMax=\n 不算；剥 \r\n 再解析；
// 发送只发 LF。请求（§2.3）：解析失败/非 object/缺 cmd → 固定 invalid request
// 不关连接；ping 一次精确比较不可覆盖；其余交 handler（handled 必须置 object
// data，否则 internal error；未注册/not_handled → command not handled）。
// 序列化统一（§2.4）：构造→序列化→发送，失败兜底 internal error，仍失败
// close 不发字节（绝无半行）；handler data 超限关连接。
//
// TROGUE_DEBUG=OFF：编译为桩（create invalid / 全操作 no-op，API 形状不变）。
#include "trogue/ipc.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "trogue/config.hpp"  // kIpcMaxClients / kIpcLineMax（行上限）
#include "raylib.h"           // TraceLog

#ifdef TROGUE_DEBUG

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <utility>
#include <vector>

namespace tg {

// ════════════════════ 内部工具（匿名 ns） ════════════════════

namespace {

constexpr int kMaxClients = kIpcMaxClients;          // 8
constexpr std::size_t kMaxLine = kIpcLineMax;        // 65536（含 \r，不含 \n）

void set_nonblock(int fd) {
    const int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);  // 失败容忍（尽力）
}

// 完整写：EINTR 重试；EAGAIN 返回 1；其他错误 -1；完成 0。
int write_full(int fd, const char* data, std::size_t len) {
    std::size_t off = 0;
    while (off < len) {
        const ssize_t n = send(fd, data + off, len - off, MSG_NOSIGNAL);
        if (n > 0) { off += static_cast<std::size_t>(n); continue; }
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 1;
            return -1;
        }
        return -1;  // n==0
    }
    return 0;
}

// 序列化 + 发送（统一出站路径；§2.4）。失败（含 EAGAIN）→ false，调用方关连。
bool send_packet(int fd, const Json& obj) {
    std::string line;
    try {
        line = obj.dump();
    } catch (...) {
        try {
            line = Json{{"ok", false}, {"error", "internal error"}}.dump();
        } catch (...) {
            return false;
        }
    }
    if (line.size() > kMaxLine - 1) {
        // 超限：换兜底文本；仍超 → close 不发部分（handler data 超限关连）
        try {
            line = Json{{"ok", false}, {"error", "response too large"}}.dump();
        } catch (...) {
            return false;
        }
        if (line.size() > kMaxLine - 1) return false;
    }
    line.push_back('\n');  // 发送只发 LF
    return write_full(fd, line.data(), line.size()) == 0;
}

// 行读取（分帧）：从 buf 提出一条完整行（剥行尾 \r\n）。返回状态。
enum class ReadLine { got, wouldblock, overflow };  // overflow=超限（关连接）
ReadLine take_line(std::string& buf, std::string& out) {
    const std::size_t nl = buf.find('\n');
    if (nl == std::string::npos) {
        if (buf.size() >= kMaxLine) return ReadLine::overflow;  // 无 LF 已超限
        return ReadLine::wouldblock;
    }
    if (nl > kMaxLine) return ReadLine::overflow;               // 行体超限
    out.assign(buf, 0, nl);
    buf.erase(0, nl + 1);  // 剥 \n
    if (!out.empty() && out.back() == '\r') out.pop_back();  // 剥 \r
    return ReadLine::got;
}

// hello 无需等窗口/响应：写失败 → false（调用方 close 不占槽）
bool send_hello(int fd) {
    return send_packet(fd, Json{{"ok", true}, {"event", "hello"},
                                {"data", {{"protocol", "tro-ipc"}, {"version", 1}}}});
}

}  // namespace

// ════════════════════ Ipc::Impl ════════════════════

struct Ipc::Impl {
    int listen_fd = -1;
    std::array<int, kMaxClients> conn;      // fd；-1 = 空闲
    std::array<std::string, kMaxClients> inbuf;
    IpcHandler handler;

    Impl() { conn.fill(-1); }

    void close_slot(int idx) {
        if (conn[idx] >= 0) ::close(conn[idx]);
        conn[idx] = -1;
        inbuf[idx].clear();
    }
    void close_all() {
        for (int i = 0; i < kMaxClients; ++i) close_slot(i);
    }
};

// ════════════════════ 核心处理 ════════════════════

namespace {

// 处理一行（JSON 请求）→ 写响应；出错关闭槽。
void handle_line(Ipc::Impl& im, int idx, const std::string& line) {
    const int fd = im.conn[idx];
    Json req;
    try {
        req = Json::parse(line, nullptr, false);
    } catch (...) {
    }
    if (req.is_discarded() || !req.is_object() || !req.contains("cmd") ||
        !req["cmd"].is_string() ||
        req["cmd"].get_ref<const std::string&>().empty()) {
        if (!send_packet(fd, Json{{"ok", false}, {"error", "invalid request"}}))
            im.close_slot(idx);
        return;
    }
    const std::string& cmd = req["cmd"].get_ref<const std::string&>();

    // ping：传输层唯一内置（一次精确比较；不可被 handler 覆盖）
    if (cmd == "ping") {
        if (!send_packet(fd, Json{{"ok", true},
                                  {"data", {{"pong", true}, {"version", 1}}}}))
            im.close_slot(idx);
        return;
    }

    if (!im.handler) {
        if (!send_packet(fd, Json{{"ok", false}, {"error", "command not handled"}}))
            im.close_slot(idx);
        return;
    }
    std::optional<Json> data;
    std::string error;
    IpcStatus st = IpcStatus::not_handled;
    // handler 是 game 代码：异常不得穿透到主循环杀进程（5.1 §6 兜底纪律）。
    // 兜底回 internal error；连接保持，供后续请求继续服务。
    try {
        st = im.handler(cmd, req, data, error);
    } catch (...) {
        st = IpcStatus::error;
        error = "internal error";
    }
    if (st == IpcStatus::handled) {
        if (!data || !data->is_object()) {
            if (!send_packet(fd, Json{{"ok", false}, {"error", "internal error"}}))
                im.close_slot(idx);
            return;
        }
        if (!send_packet(fd, Json{{"ok", true}, {"data", *data}})) im.close_slot(idx);
        return;
    }
    if (st == IpcStatus::error) {
        if (!send_packet(fd, Json{{"ok", false}, {"error", error}}))
            im.close_slot(idx);
        return;
    }
    // not_handled
    if (!send_packet(fd, Json{{"ok", false}, {"error", "command not handled"}}))
        im.close_slot(idx);
}

// accept 阶段：循环到 EAGAIN。新 fd 只写 hello（不 recv）。
void accept_phase(Ipc::Impl& im) {
    for (;;) {
        // 找空闲槽
        int slot = -1;
        for (int i = 0; i < kMaxClients; ++i) {
            if (im.conn[i] < 0) { slot = i; break; }
        }
        const int fd = accept(im.listen_fd, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // 本 poll 完毕
            // 其他监听错误：记录并永久关闭监听（已有连接继续服务完）
            TraceLog(LOG_WARNING, "[ipc] accept 失败，监听关闭（errno=%d）", errno);
            ::close(im.listen_fd);
            im.listen_fd = -1;
            break;
        }
        if (slot < 0) {
            // 满 8 → 立即 close 且不发 hello
            ::close(fd);
            continue;
        }
        set_nonblock(fd);
        im.conn[slot] = fd;
        if (!send_hello(fd)) {
            // hello 写失败：close 不占 slot，继续 accept
            im.close_slot(slot);
            continue;
        }
    }
}

// 服务一个连接槽：读完整可读数据 → 逐行处理。
void service_slot(Ipc::Impl& im, int idx) {
    const int fd = im.conn[idx];
    if (fd < 0) return;
    std::array<char, 4096> buf;
    for (;;) {
        const ssize_t n = recv(fd, buf.data(), buf.size(), 0);
        if (n > 0) {
            im.inbuf[idx].append(buf.data(), static_cast<std::size_t>(n));
            // 分帧处理所有完整行
            std::string line;
            for (;;) {
                const ReadLine r = take_line(im.inbuf[idx], line);
                if (r == ReadLine::got) {
                    if (!(im.conn[idx] >= 0)) return;  // handler 内被关闭防御
                    handle_line(im, idx, line);
                    if (im.conn[idx] < 0) return;      // 处理后已关
                } else if (r == ReadLine::overflow) {
                    im.close_slot(idx);  // 超限：close 不发响应
                    return;
                } else {
                    break;  // wouldblock：等更多
                }
            }
        } else if (n == 0) {
            im.close_slot(idx);  // 对端关闭
            return;
        } else {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;  // 下 poll
            im.close_slot(idx);
            return;
        }
    }
}

}  // namespace（匿名：处理函数结束）

// ════════════════════ 公共接口 ════════════════════

Ipc::Ipc() = default;
Ipc::Ipc(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Ipc::~Ipc() {
    if (impl_) {
        if (impl_->listen_fd >= 0) ::close(impl_->listen_fd);
        impl_->close_all();
    }
}
Ipc::Ipc(Ipc&&) noexcept = default;
Ipc& Ipc::operator=(Ipc&&) noexcept = default;

Ipc Ipc::create(std::uint16_t port) {
    auto im = std::make_unique<Impl>();
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return Ipc{};  // invalid
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        listen(fd, /*backlog=*/8) < 0) {
        ::close(fd);
        return Ipc{};
    }
    im->listen_fd = fd;
    return Ipc(std::move(im));
}

void Ipc::set_handler(IpcHandler h) {
    if (!impl_) return;  // 无效实例：no-op（不保存）
    impl_->handler = std::move(h);
}
void Ipc::clear_handler() {
    if (impl_) impl_->handler = nullptr;
}

void Ipc::poll() {
    if (!valid()) return;
    // ① accept 阶段
    if (impl_->listen_fd >= 0) accept_phase(*impl_);
    // ② 轮询阶段（固定 slot 序）
    for (int i = 0; i < kMaxClients; ++i) {
        if (impl_->conn[i] >= 0) service_slot(*impl_, i);
    }
}

bool Ipc::valid() const { return impl_ && impl_->listen_fd >= 0; }

}  // namespace tg

#else  // ── TROGUE_DEBUG=OFF：Release 桩（API 形状一致，安全 no-op） ──

namespace tg {

// 桩实现也需要完整 Impl（unique_ptr 析构点要求完整类型）
struct Ipc::Impl {};

Ipc::Ipc() = default;
Ipc::Ipc(std::unique_ptr<Impl>) {}
Ipc::~Ipc() = default;
Ipc::Ipc(Ipc&&) noexcept = default;
Ipc& Ipc::operator=(Ipc&&) noexcept = default;

Ipc Ipc::create(std::uint16_t /*port*/) { return Ipc{}; }  // 恒 invalid
void Ipc::set_handler(IpcHandler) {}
void Ipc::clear_handler() {}
void Ipc::poll() {}
bool Ipc::valid() const { return false; }

}  // namespace tg

#endif  // TROGUE_DEBUG