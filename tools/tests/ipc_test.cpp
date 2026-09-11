// ipc_test.cpp —— tg::Ipc 事件通道单测。
// Debug：进程内真实 loopback——订阅/退订全量集语义、参数校验、publish 分流、
//        filter 匹配（含缺键/数值提升/空 object）、handler 内 publish 先于响应、
//        事件超限断开、慢消费者断开、disconnect/断开清订阅、conn_id 单调、
//        无 handler 时保留命令可用。
// Release：桩行为（valid==false / publish/disconnect no-op / connections 空）。
// helper 与端口策略沿用 watcher_ipc_test.cpp 模式（双分支先例）。
#include <cstdio>
#include <string>

#include "trogue/trogue.hpp"
#include "test_util.hpp"

#ifndef TROGUE_DEBUG
// ── Release：桩行为 ──
namespace {

bool test_release_stubs() {
    bool ok = true;
    auto ipc = tg::Ipc::create(49321);
    CHECK(!ipc.valid());
    CHECK(ipc.connections().empty());
    CHECK(!ipc.disconnect(1));
    ipc.publish("Ev", tg::Json{{"k", 1}});  // no-op 不崩
    ipc.poll();
    return ok;
}

}  // namespace

int main() {
    test_release_stubs();
    std::printf("[ipc test] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}

#else  // ── Debug：真实 loopback 集成 ──

// 系统头（必须在 namespace 外）
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <optional>

namespace {

// 端口：高位 + pid 偏移（进程内唯一；watcher_ipc_test 同式，ctest 串行不冲突）
int next_port() {
    static int seq = 0;
    return 49000 + (getpid() % 500) + seq++;
}

bool connect_client(int port, int& fd) {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(static_cast<std::uint16_t>(port));
    for (int i = 0; i < 100; ++i) {  // 重试连（server 可能刚起）
        if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) return true;
        usleep(20000);
    }
    return false;
}

// recv 一行（\n 结尾）；超时兜底。返回行内容（不含\n）；失败返回空串+false。
bool recv_line(int fd, std::string& out, int timeout_ms = 3000) {
    out.clear();
    struct timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char c;
    for (;;) {
        const ssize_t n = recv(fd, &c, 1, 0);
        if (n == 1) {
            if (c == '\n') return true;
            out.push_back(c);
        } else {
            return false;  // 超时/错误/EOF
        }
    }
}

bool send_line(int fd, const std::string& line) {
    std::string s = line;
    s.push_back('\n');
    return send(fd, s.data(), s.size(), 0) == static_cast<ssize_t>(s.size());
}

// 对端已关闭且无数据 → recv 返回 0（残行 + EOF 契约的 EOF 侧）。
bool recv_eof(int fd, int timeout_ms = 3000) {
    struct timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char c;
    return recv(fd, &c, 1, 0) == 0;
}

// 短超时内不应有行（否定断言）
bool no_line(int fd) {
    std::string s;
    return !recv_line(fd, s, 300);
}

// 连接 + 首轮 poll（accept + hello）+ 读 hello
bool connect_hello(tg::Ipc& ipc, int port, int& fd, std::string& hello) {
    if (!connect_client(port, fd)) return false;
    ipc.poll();
    return recv_line(fd, hello);
}

// 发请求 → poll → 读响应行 → 解析为 JSON
bool roundtrip(tg::Ipc& ipc, int fd, const std::string& req, tg::Json& out) {
    if (!send_line(fd, req)) return false;
    ipc.poll();
    std::string line;
    if (!recv_line(fd, line)) return false;
    out = tg::Json::parse(line, nullptr, false);
    return !out.is_discarded();
}

// ── 订阅/退订：全量集回显、校验、保留名、filter 记录 ──
bool test_subscribe_basics() {
    bool ok = true;
    const int port = next_port();
    auto ipc = tg::Ipc::create(static_cast<std::uint16_t>(port));
    REQUIRE(ipc.valid());
    int c = -1;
    std::string hello;
    REQUIRE(connect_hello(ipc, port, c, hello));

    tg::Json r;
    // 基础订阅 → 回显全量集（无 filter 省略键）
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"subscribe","events":["A","B"]})", r));
    CHECK(r["ok"] == true);
    CHECK(r["data"]["events"].size() == 2);
    CHECK(r["data"]["events"][0]["event"] == "A");
    CHECK(!r["data"]["events"][0].contains("filter"));
    CHECK(r["data"]["events"][1]["event"] == "B");

    // 增量并集 + 去重
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"subscribe","events":["B","C"]})", r));
    CHECK(r["data"]["events"].size() == 3);
    CHECK(r["data"]["events"][2]["event"] == "C");

    // 带 filter 订阅（对象数组元素含 filter 键）
    REQUIRE(roundtrip(ipc, c,
                      R"({"cmd":"subscribe","events":["F"],"filter":{"entity":"p"}})",
                      r));
    CHECK(r["data"]["events"].size() == 4);
    CHECK(r["data"]["events"][3]["event"] == "F");
    CHECK(r["data"]["events"][3]["filter"]["entity"] == "p");

    // 校验错误（连接保持）
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"subscribe","events":["hello"]})", r));
    CHECK(r["ok"] == false && r["error"] == "reserved event name");
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"subscribe","events":[]})", r));
    CHECK(r["ok"] == false);
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"subscribe","events":"A"})", r));
    CHECK(r["ok"] == false);
    REQUIRE(roundtrip(ipc, c,
                      R"({"cmd":"subscribe","events":["A"],"filter":"x"})", r));
    CHECK(r["ok"] == false && r["error"] == "filter must be object");
    // 校验失败后订阅集不变
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"unsubscribe","events":[]})", r));
    CHECK(r["ok"] == true && r["data"]["events"].size() == 4);

    // 退订：按名整体移除（含 filter 变体）；空数组 no-op 回显；未订阅项 no-op
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"unsubscribe","events":["F"]})", r));
    CHECK(r["data"]["events"].size() == 3);
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"unsubscribe","events":["A","zz"]})", r));
    CHECK(r["data"]["events"].size() == 2);
    CHECK(r["data"]["events"][0]["event"] == "B");
    CHECK(r["data"]["events"][1]["event"] == "C");
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"unsubscribe","events":5})", r));
    CHECK(r["ok"] == false && r["error"] == "unsubscribe needs string array");

    ::close(c);
    return ok;
}

// ── publish 分流：只达订阅者；退订后不再收；零订阅无副作用 ──
bool test_publish_routing() {
    bool ok = true;
    const int port = next_port();
    auto ipc = tg::Ipc::create(static_cast<std::uint16_t>(port));
    REQUIRE(ipc.valid());
    int a = -1, b = -1;
    std::string hello;
    REQUIRE(connect_hello(ipc, port, a, hello));
    REQUIRE(connect_hello(ipc, port, b, hello));

    tg::Json r;
    REQUIRE(roundtrip(ipc, a, R"({"cmd":"subscribe","events":["A"]})", r));
    REQUIRE(roundtrip(ipc, b, R"({"cmd":"subscribe","events":["B"]})", r));

    // 只达订阅 a；b 无行
    ipc.publish("A", tg::Json{{"x", 1}});
    std::string line;
    CHECK(recv_line(a, line));
    CHECK(line.find("\"event\":\"A\"") != std::string::npos);
    CHECK(line.find("\"x\":1") != std::string::npos);
    CHECK(no_line(b));

    // 零订阅事件：无副作用、连接均存活
    ipc.publish("Z", tg::Json{{"y", 2}});
    CHECK(no_line(a));
    CHECK(no_line(b));

    // 退订后不再收
    REQUIRE(roundtrip(ipc, a, R"({"cmd":"unsubscribe","events":["A"]})", r));
    CHECK(r["ok"] == true && r["data"]["events"].empty());
    ipc.publish("A", tg::Json{{"x", 3}});
    CHECK(no_line(a));
    CHECK(no_line(b));

    ::close(a);
    ::close(b);
    return ok;
}

// ── filter 分流：命中/不命中/缺键/数值提升/空 object ──
bool test_filter_routing() {
    bool ok = true;
    const int port = next_port();
    auto ipc = tg::Ipc::create(static_cast<std::uint16_t>(port));
    REQUIRE(ipc.valid());
    int a = -1, b = -1, c = -1, d = -1, e = -1;
    std::string hello;
    REQUIRE(connect_hello(ipc, port, a, hello));
    REQUIRE(connect_hello(ipc, port, b, hello));
    REQUIRE(connect_hello(ipc, port, c, hello));
    REQUIRE(connect_hello(ipc, port, d, hello));
    REQUIRE(connect_hello(ipc, port, e, hello));

    tg::Json r;
    REQUIRE(roundtrip(ipc, a, R"({"cmd":"subscribe","events":["M"],"filter":{"entity":"player"}})", r));
    REQUIRE(roundtrip(ipc, b, R"({"cmd":"subscribe","events":["M"]})", r));
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"subscribe","events":["M"],"filter":{"entity":"goblin"}})", r));
    REQUIRE(roundtrip(ipc, d, R"({"cmd":"subscribe","events":["M"],"filter":{"hp":1}})", r));
    REQUIRE(roundtrip(ipc, e, R"({"cmd":"subscribe","events":["M"],"filter":{}})", r));

    std::string line;
    // ① 命中 a（player）+ 无过滤 b + 恒真空过滤 e；c 不命中；d 缺键不匹配
    ipc.publish("M", tg::Json{{"entity", "player"}});
    CHECK(recv_line(a, line) && line.find("\"event\":\"M\"") != std::string::npos);
    CHECK(recv_line(b, line));
    CHECK(no_line(c));
    CHECK(no_line(d));
    CHECK(recv_line(e, line));
    // ② 缺 entity → 仅无过滤 b 与空过滤 e；数值提升 hp=1 命中 1.0
    ipc.publish("M", tg::Json{{"hp", 1.0}});
    CHECK(no_line(a));
    CHECK(recv_line(b, line));
    CHECK(no_line(c));
    CHECK(recv_line(d, line));
    CHECK(recv_line(e, line));
    // ③ data={"entity":"goblin"}：c 单键命中；d 的 filter 需 hp（缺键 → 不匹配）
    ipc.publish("M", tg::Json{{"entity", "goblin"}});
    CHECK(no_line(a));
    CHECK(recv_line(b, line));
    CHECK(recv_line(c, line));
    CHECK(no_line(d));
    CHECK(recv_line(e, line));
    // ④ AND 同时满足：c 命中；a 仍不命中（entity 不等）
    ipc.publish("M", tg::Json{{"entity", "goblin"}, {"hp", 2}});
    CHECK(no_line(a));
    CHECK(recv_line(b, line));
    CHECK(recv_line(c, line));
    CHECK(no_line(d));
    CHECK(recv_line(e, line));

    for (int fd : {a, b, c, d, e}) ::close(fd);
    return ok;
}

// ── handler 内 publish：事件行先于响应行（时序契约） ──
bool test_publish_ordering() {
    bool ok = true;
    const int port = next_port();
    auto ipc = tg::Ipc::create(static_cast<std::uint16_t>(port));
    REQUIRE(ipc.valid());
    tg::Ipc* pip = &ipc;
    ipc.set_handler([&pip](const std::string& cmd, const tg::Json&,
                           std::optional<tg::Json>& data, std::string&) {
        if (cmd == "probe") {
            pip->publish("Ev", tg::Json{{"n", 1}});  // handler 内发布
            data = tg::Json::object();
            return tg::IpcStatus::handled;
        }
        return tg::IpcStatus::not_handled;
    });

    int c = -1;
    std::string hello;
    REQUIRE(connect_hello(ipc, port, c, hello));
    tg::Json r;
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"subscribe","events":["Ev"]})", r));
    CHECK(r["ok"] == true);

    REQUIRE(send_line(c, R"({"cmd":"probe"})"));
    ipc.poll();
    std::string l1, l2;
    CHECK(recv_line(c, l1));
    CHECK(recv_line(c, l2));
    CHECK(l1.find("\"event\":\"Ev\"") != std::string::npos);  // 第一行 = 事件
    CHECK(l2.find("\"event\":\"Ev\"") == std::string::npos);  // 第二行 = 响应
    CHECK(l2.find("\"ok\":true") != std::string::npos);

    ::close(c);
    return ok;
}

// ── 事件行超限：无兜底行、直接断开；不匹配者既不收行也不被断开 ──
bool test_oversize_disconnect() {
    bool ok = true;
    const int port = next_port();
    auto ipc = tg::Ipc::create(static_cast<std::uint16_t>(port));
    REQUIRE(ipc.valid());
    int c = -1, d = -1;
    std::string hello;
    REQUIRE(connect_hello(ipc, port, c, hello));
    REQUIRE(connect_hello(ipc, port, d, hello));
    tg::Json r;
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"subscribe","events":["Big"]})", r));
    REQUIRE(roundtrip(ipc, d, R"({"cmd":"subscribe","events":["Other"]})", r));

    ipc.publish("Big", tg::Json{{"blob", std::string(70000, 'x')}});
    CHECK(recv_eof(c));                // 匹配订阅者：断开且未发任何行
    CHECK(no_line(d));                 // 不匹配者：既不收行也不被断开
    bool d_alive = false;
    for (const auto& info : ipc.connections()) {
        if (info.events.size() == 1 && info.events[0].event == "Other") d_alive = true;
    }
    CHECK(d_alive);
    CHECK(ipc.connections().size() == 1);  // 订阅随断开清零
    CHECK(ipc.valid());                    // 引擎自身不受影响

    ::close(c);
    ::close(d);
    return ok;
}

// ── 慢消费者：不读 + 小 rcvbuf → publish 直写 EAGAIN → 断开（有界收敛） ──
bool test_slow_consumer() {
    bool ok = true;
    const int port = next_port();
    auto ipc = tg::Ipc::create(static_cast<std::uint16_t>(port));
    REQUIRE(ipc.valid());
    int s = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(s >= 0);
    int small = 1024;  // connect 前设置（内核托底 ~2.3KB）
    (void)setsockopt(s, SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(static_cast<std::uint16_t>(port));
    bool connected = false;
    for (int i = 0; i < 100 && !connected; ++i) {
        connected = connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0;
        if (!connected) usleep(20000);
    }
    REQUIRE(connected);
    ipc.poll();
    std::string hello;
    REQUIRE(recv_line(s, hello));
    // 订阅（响应留在小缓冲里不读——加速打满）
    CHECK(send_line(s, R"({"cmd":"subscribe","events":["flood"]})"));
    ipc.poll();
    std::uint64_t sid = 0;
    for (const auto& c : ipc.connections()) {
        if (!c.events.empty()) sid = c.conn;
    }
    CHECK(sid != 0);

    const std::string blob(60000, 'x');
    bool gone = false;
    for (int i = 0; i < 200 && !gone; ++i) {  // 上界 ~12MB ≥ 4MB 预算
        ipc.publish("flood", tg::Json{{"blob", blob}});
        gone = true;
        for (const auto& c : ipc.connections()) {
            if (c.conn == sid) gone = false;
        }
    }
    CHECK(gone);  // 慢消费者被断开（订阅清零，server 侧由 connections() 观察）

    ::close(s);
    return ok;
}

// ── disconnect API：定点断开 + 幂等 false + 连接 id 单调不复用 ──
bool test_disconnect_and_conn_id() {
    bool ok = true;
    const int port = next_port();
    auto ipc = tg::Ipc::create(static_cast<std::uint16_t>(port));
    REQUIRE(ipc.valid());
    int c1 = -1, c2 = -1, c3 = -1;
    std::string hello;
    REQUIRE(connect_hello(ipc, port, c1, hello));
    REQUIRE(connect_hello(ipc, port, c2, hello));

    auto conns = ipc.connections();
    REQUIRE(conns.size() == 2);
    const std::uint64_t id1 = conns[0].conn, id2 = conns[1].conn;
    CHECK(id1 != 0 && id2 != 0 && id1 != id2);

    CHECK(ipc.disconnect(id1));         // 定点断开
    CHECK(recv_eof(c1));                // 对端见 EOF
    CHECK(ipc.connections().size() == 1);
    CHECK(!ipc.disconnect(id1));        // 幂等：找不到 → false
    CHECK(!ipc.disconnect(0));          // 0 = 无效 id

    // 新连接 id 不复用（单调递增）
    REQUIRE(connect_hello(ipc, port, c3, hello));
    conns = ipc.connections();
    REQUIRE(conns.size() == 2);
    std::uint64_t id3 = 0;
    for (const auto& c : conns) {
        if (c.conn != id2) id3 = c.conn;
    }
    CHECK(id3 > id1 && id3 > id2);

    ::close(c2);
    ::close(c3);
    return ok;
}

// ── 客户端断开：服务端感知 EOF → 订阅清零 ──
bool test_client_close_clears() {
    bool ok = true;
    const int port = next_port();
    auto ipc = tg::Ipc::create(static_cast<std::uint16_t>(port));
    REQUIRE(ipc.valid());
    int c = -1;
    std::string hello;
    REQUIRE(connect_hello(ipc, port, c, hello));
    tg::Json r;
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"subscribe","events":["A"]})", r));
    CHECK(!ipc.connections().empty());

    ::close(c);
    ipc.poll();  // service_slot 读到 EOF → close_slot
    CHECK(ipc.connections().empty());
    return ok;
}

// ── 无 game handler：保留命令仍可用（传输层自有状态） ──
bool test_no_handler_reserved() {
    bool ok = true;
    const int port = next_port();
    auto ipc = tg::Ipc::create(static_cast<std::uint16_t>(port));
    REQUIRE(ipc.valid());  // 不调用 set_handler
    int c = -1;
    std::string hello;
    REQUIRE(connect_hello(ipc, port, c, hello));
    tg::Json r;
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"subscribe","events":["A"]})", r));
    CHECK(r["ok"] == true && r["data"]["events"].size() == 1);
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"connections"})", r));
    CHECK(r["ok"] == true && r["data"]["count"] == 1);
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"ping"})", r));
    CHECK(r["ok"] == true && r["data"]["pong"] == true);
    // 非保留命令 → command not handled（无 handler）
    REQUIRE(roundtrip(ipc, c, R"({"cmd":"status"})", r));
    CHECK(r["ok"] == false && r["error"] == "command not handled");

    ::close(c);
    return ok;
}

}  // namespace

int main() {
    const bool r1 = test_subscribe_basics();
    const bool r2 = test_publish_routing();
    const bool r3 = test_filter_routing();
    const bool r4 = test_publish_ordering();
    const bool r5 = test_oversize_disconnect();
    const bool r6 = test_slow_consumer();
    const bool r7 = test_disconnect_and_conn_id();
    const bool r8 = test_client_close_clears();
    const bool r9 = test_no_handler_reserved();
    std::printf("[ipc test] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return (::tg_test::g_failures == 0 && r1 && r2 && r3 && r4 && r5 && r6 &&
            r7 && r8 && r9)
               ? 0
               : 1;
}

#endif  // TROGUE_DEBUG
