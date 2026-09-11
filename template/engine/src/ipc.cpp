// ipc.cpp —— JSON-lines 传输、事件通道与 callback 分发。
//
// 状态机：每 poll = accept 阶段（循环至 EAGAIN；新 fd 只写 hello 不
// recv；满 8 关闭；hello 失败关不占槽）→ 轮询阶段（固定 slot 序逐行处理）。
// 行分帧：线上字节计数含 \r 上限 kIpcLineMax=\n 不算；剥 \r\n 再解析；
// 发送只发 LF。请求：解析失败/非 object/缺 cmd → 固定 invalid request
// 不关连接；传输层保留命令（ping + subscribe/unsubscribe/connections）
// 先于 handler 拦截、不可被 game 覆盖；其余交 handler（handled 必须置 object
// data，否则 internal error；未注册/not_handled → command not handled）。
// 响应序列化：构造→序列化→发送；行超限 → 换兜底错误行、**连接保持**
// （仅兜底行自身仍超限才 close 不发字节，绝无半行）。
// 事件通道：publish 非阻塞直写订阅连接；事件行超限**无兜底行** →
// 断开该事件全部 filter 匹配订阅者（发无 event 键的兜底行会破坏判别式，禁止）；
// EAGAIN/写失败 → close_slot（慢消费者自愈）。断开即订阅清零：对端关闭/写失败/
// 主动 disconnect 三条路径统一走 close_slot。
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

#include <algorithm>
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
// 注意：部分写后遇 EAGAIN 时已进内核的字节不收回——对端将见「残行 + EOF」
// （文档契约）。
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

// 序列化 + 发送（响应统一出站路径）。失败（含 EAGAIN）→ false，
// 调用方关连。**超限语义（响应独有）**：换兜底错误行、连接保持——只有兜底行
// 自身仍超限才 false（事件路径不经过这里，见 Ipc::publish 的分叉注释）。
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
        // 超限：换兜底文本（连接保持）；兜底行自身仍超 → close 不发部分
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
    // 订阅记录：(事件名, filter)；filter null = 无过滤。
    struct Sub {
        std::string event;
        Json filter;
    };

    int listen_fd = -1;
    std::array<int, kMaxClients> conn;               // fd；-1 = 空闲
    std::array<std::uint64_t, kMaxClients> conn_id;  // 连接身份；0 = 空闲
    std::array<std::string, kMaxClients> inbuf;
    std::array<std::vector<Sub>, kMaxClients> subs;  // 订阅表（按连接槽位）
    std::uint64_t next_conn_id = 0;                  // 单调递增，断开不复用
    IpcHandler handler;

    Impl() {
        conn.fill(-1);
        conn_id.fill(0);
    }

    // 断开唯一出口：对端关闭/写失败/主动 disconnect 全走这里——订阅清零在此
    // 单点收口（「断开即订阅清零」）。
    void close_slot(int idx) {
        if (conn[idx] >= 0) ::close(conn[idx]);
        conn[idx] = -1;
        conn_id[idx] = 0;
        subs[idx].clear();
        inbuf[idx].clear();
    }
    void close_all() {
        for (int i = 0; i < kMaxClients; ++i) close_slot(i);
    }
};

// ════════════════════ 核心处理 ════════════════════

namespace {

// ── 事件通道工具 ──

// 订阅集 → wire JSON（对象数组；无 filter 者省略 filter 键——null↔省略 双向转换）。
Json subs_to_json(const std::vector<Ipc::Impl::Sub>& subs) {
    Json arr = Json::array();
    for (const auto& s : subs) {
        Json o{{"event", s.event}};
        if (!s.filter.is_null()) o["filter"] = s.filter;
        arr.push_back(std::move(o));
    }
    return arr;
}

// filter 匹配：filter 逐键与 data 顶层等值（AND；空 object = AND 空集 = 恒真）。
// 缺键 → 不匹配：探测必须用 find()——非 const operator[] 会向 data 插入 null 键，
// 把「缺键不匹配」静默变成「null == null 匹配」并污染 data（契约钉死）。
bool filter_match(const Json& filter, const Json& data) {
    for (auto it = filter.begin(); it != filter.end(); ++it) {
        const auto hit = data.find(it.key());
        if (hit == data.end() || !(*hit == it.value())) return false;
    }
    return true;
}

// 槽位是否对 (event, data) 感兴趣：存在记录同名且（无 filter 或 filter 匹配）。
bool slot_interested(const Ipc::Impl& im, int idx, std::string_view event,
                     const Json& data) {
    for (const auto& s : im.subs[idx]) {
        if (event != s.event) continue;
        if (s.filter.is_null() || filter_match(s.filter, data)) return true;
    }
    return false;
}

// 发送成功包络 / 错误行；写失败 → 关槽（订阅通道命令与响应同慢消费者语义）。
void send_ok(Ipc::Impl& im, int idx, Json data) {
    if (!send_packet(im.conn[idx], Json{{"ok", true}, {"data", std::move(data)}}))
        im.close_slot(idx);
}
void send_err(Ipc::Impl& im, int idx, const char* msg) {
    if (!send_packet(im.conn[idx], Json{{"ok", false}, {"error", msg}}))
        im.close_slot(idx);
}

// events 数组校验：bad = 缺失/非数组/元素非非空字符串；empty = 空数组。
enum class EventsArr { ok, bad, empty };
EventsArr check_events_arr(const Json& req) {
    const auto it = req.find("events");
    if (it == req.end() || !it->is_array()) return EventsArr::bad;
    if (it->empty()) return EventsArr::empty;
    for (const auto& e : *it)
        if (!e.is_string() || e.get_ref<const std::string&>().empty())
            return EventsArr::bad;
    return EventsArr::ok;
}

// subscribe（传输层保留命令）：订阅记录 = (事件名, filter) 二元组；同对去重、
// 幂等；同名不同 filter 并存。任何校验失败整条拒绝（不做部分订阅）。
void handle_subscribe(Ipc::Impl& im, int idx, const Json& req) {
    if (check_events_arr(req) != EventsArr::ok) {
        send_err(im, idx, "subscribe needs non-empty string array");
        return;
    }
    for (const auto& e : req["events"]) {
        if (e.get_ref<const std::string&>() == "hello") {  // 保留名（整条拒绝）
            send_err(im, idx, "reserved event name");
            return;
        }
    }
    Json filter = Json(nullptr);  // null = 无过滤
    if (req.contains("filter")) {
        if (!req["filter"].is_object()) {
            send_err(im, idx, "filter must be object");
            return;
        }
        filter = req["filter"];
    }
    std::vector<Ipc::Impl::Sub>& subs = im.subs[idx];
    for (const auto& e : req["events"]) {
        Ipc::Impl::Sub s{e.get_ref<const std::string&>(), filter};
        bool exists = false;
        for (const auto& r : subs) {
            if (r.event == s.event && r.filter == s.filter) { exists = true; break; }
        }
        if (!exists) subs.push_back(std::move(s));  // 去重保序
    }
    send_ok(im, idx, Json{{"events", subs_to_json(subs)}});
}

// unsubscribe（传输层保留命令）：按事件名整体移除全部 filter 变体；空数组 =
// no-op 仍回显；未订阅项 = no-op；"hello" 不校验（订不上，退订恒 no-op）。
void handle_unsubscribe(Ipc::Impl& im, int idx, const Json& req) {
    if (check_events_arr(req) == EventsArr::bad) {
        send_err(im, idx, "unsubscribe needs string array");
        return;
    }
    std::vector<Ipc::Impl::Sub>& subs = im.subs[idx];
    for (const auto& e : req["events"]) {
        const auto& name = e.get_ref<const std::string&>();
        subs.erase(std::remove_if(subs.begin(), subs.end(),
                                  [&name](const Ipc::Impl::Sub& s) {
                                      return s.event == name;
                                  }),
                   subs.end());
    }
    send_ok(im, idx, Json{{"events", subs_to_json(subs)}});
}

// connections（传输层保留命令）：列出全部连接（含未订阅者）与其订阅集。
void handle_connections(Ipc::Impl& im, int idx) {
    Json arr = Json::array();
    int count = 0;
    for (int i = 0; i < kMaxClients; ++i) {
        if (im.conn[i] < 0) continue;
        arr.push_back(Json{{"conn", im.conn_id[i]},
                           {"events", subs_to_json(im.subs[i])}});
        ++count;
    }
    send_ok(im, idx, Json{{"connections", std::move(arr)}, {"count", count}});
}

// 处理一行（JSON 请求）→ 写响应；出错关闭槽。
void handle_line(Ipc::Impl& im, int idx, const std::string& line) {
    // fd 为快照：handler 内 publish 可能因写失败把**本连接** close_slot
    // （重入角落）。此时响应 send 以陈旧 fd 失败 → 末尾 close_slot(idx) 被
    // 空槽守卫挡成 no-op——语义即「响应不再发出，对端见事件行/残行 + EOF」。
    // 该因果链由 service_slot 前后 conn[idx] 检查 + close_slot 守卫闭环，勿改。
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

    // ping：传输层内置（一次精确比较；不可被 handler 覆盖）
    if (cmd == "ping") {
        if (!send_packet(fd, Json{{"ok", true},
                                  {"data", {{"pong", true}, {"version", 1}}}}))
            im.close_slot(idx);
        return;
    }

    // 订阅通道保留命令：先于 game handler——订阅表为传输层自有状态。
    if (cmd == "subscribe") {
        handle_subscribe(im, idx, req);
        return;
    }
    if (cmd == "unsubscribe") {
        handle_unsubscribe(im, idx, req);
        return;
    }
    if (cmd == "connections") {
        handle_connections(im, idx);
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
    // handler 是 game 代码：异常不得穿透到主循环杀进程（兜底纪律）。
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
        // 连接身份：先分配再写 hello（hello 失败也会消耗一个 id——与「断开不复用」
        // 同向，仅跳号无副作用）。
        im.conn_id[slot] = ++im.next_conn_id;
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

// ── 事件通道 ──

void Ipc::publish(std::string_view event, const Json& data) {
    if (!impl_) return;  // 无效实例 no-op
    if (!data.is_object()) {
        TraceLog(LOG_WARNING, "[ipc] publish(%.*s): data 非 object，跳过",
                 static_cast<int>(event.size()), event.data());
        return;
    }
    // 行对所有匹配订阅者相同 → 构造 + 序列化一次。dump 抛异常（如非法 UTF-8，
    // type_error.316）→ 跳过本事件：此时未写任何字节，不断开无辜订阅者（与超限
    // 路径区分）；void 签名不得让异常穿透公共 API。
    std::string line;
    try {
        line = Json{{"ok", true}, {"event", std::string(event)}, {"data", data}}.dump();
    } catch (...) {
        TraceLog(LOG_WARNING, "[ipc] publish(%.*s): 序列化失败，跳过",
                 static_cast<int>(event.size()), event.data());
        return;
    }
    if (line.size() > kMaxLine - 1) {
        // 事件行超限：无兜底行（发出无 event 键的行会破坏判别式）→ 断开该事件
        // 全部 filter 匹配订阅者（不匹配者既不收行也不被断开）。
        TraceLog(LOG_WARNING, "[ipc] 事件行超限（%zu 字节），断开匹配订阅者",
                 line.size());
        for (int i = 0; i < kMaxClients; ++i) {
            if (impl_->conn[i] >= 0 && slot_interested(*impl_, i, event, data))
                impl_->close_slot(i);
        }
        return;
    }
    line.push_back('\n');
    for (int i = 0; i < kMaxClients; ++i) {
        if (impl_->conn[i] < 0) continue;
        if (!slot_interested(*impl_, i, event, data)) continue;
        // 直写；EAGAIN/写失败 → 断开（慢消费者自愈）。重入安全：断开的可能正是
        // 当前被 handle_line 服务的连接——响应路径由 fd 快照注释所述因果链兜住。
        if (write_full(impl_->conn[i], line.data(), line.size()) != 0)
            impl_->close_slot(i);
    }
}

bool Ipc::disconnect(std::uint64_t conn_id) {
    if (!impl_) return false;
    for (int i = 0; i < kMaxClients; ++i) {
        if (impl_->conn[i] >= 0 && impl_->conn_id[i] == conn_id) {
            impl_->close_slot(i);  // 订阅随 close_slot 单点清零
            return true;
        }
    }
    return false;
}

std::vector<IpcConnInfo> Ipc::connections() const {
    std::vector<IpcConnInfo> out;
    if (!impl_) return out;
    for (int i = 0; i < kMaxClients; ++i) {
        if (impl_->conn[i] < 0) continue;
        IpcConnInfo info;
        info.conn = impl_->conn_id[i];
        info.events.reserve(impl_->subs[i].size());
        for (const auto& s : impl_->subs[i]) {
            IpcSubscription is;
            is.event = s.event;
            is.filter = s.filter;  // null = 无过滤（快照保持引擎内表示）
            info.events.push_back(std::move(is));
        }
        out.push_back(std::move(info));
    }
    return out;
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
void Ipc::publish(std::string_view /*event*/, const Json& /*data*/) {}
bool Ipc::disconnect(std::uint64_t /*conn_id*/) { return false; }
std::vector<IpcConnInfo> Ipc::connections() const { return {}; }
bool Ipc::valid() const { return false; }

}  // namespace tg

#endif  // TROGUE_DEBUG
