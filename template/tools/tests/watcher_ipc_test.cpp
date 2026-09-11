// watcher_ipc_test.cpp —— Watcher::classify_event_name 全分支单测 + Release 桩行为
// + Debug Ipc 真实 socket 集成测试（plan-5.5 §4）。
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "trogue/trogue.hpp"
#include "test_util.hpp"

#ifndef TROGUE_DEBUG
// Release：Ipc/Watcher 桩验证（valid==false / poll==nullopt / no-op）
namespace {

bool test_release_stubs() {
    bool ok = true;
    // Ipc 桩
    auto ipc = tg::Ipc::create(48764);
    CHECK(!ipc.valid());
    ipc.set_handler([](const std::string&, const tg::Json&, std::optional<tg::Json>&,
                       std::string&) { return tg::IpcStatus::not_handled; });
    ipc.clear_handler();
    ipc.poll();  // 安全 no-op
    // Watcher 桩
    auto w = tg::Watcher::create("assets/scenes");
    CHECK(!w.valid());
    CHECK(!w.poll().has_value());
    w.shutdown();
    return ok;
}

}  // namespace

int main() {
    test_release_stubs();
    std::printf("[watcher/ipc test] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return ::tg_test::g_failures == 0 ? 0 : 1;
}

#else  // ── Debug：classify_event_name + Ipc 集成 ──

// 系统头（必须在 namespace 外）
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fstream>
#include <sys/stat.h>

// tgz 前向声明（分类纯函数；定义于 engine/src/hotreload.cpp，无公共头）
namespace tg::detail {
std::optional<std::string> classify_event_name(const char* zone, std::size_t len);
}

namespace {

using tg::detail::classify_event_name;  // 全限定使用（前向声明见上）

// wrap：模拟 inotify 名称区 = name + NUL + padding；zone 长度 = 名称区容量。
// classify_event_name 扫终止 NUL，返回名称或 nullopt。
std::optional<std::string> classify(const std::string& name, std::size_t zone_len) {
    std::string zone = name;
    zone.push_back('\0');                       // 终止 NUL
    zone.append(zone_len, 'X');                 // padding（任意字节）
    return classify_event_name(zone.data(), zone.size());
}

bool test_classify() {
    bool ok = true;
    // 合法 .json（带 padding NUL）
    CHECK(classify("a.json", 32) == std::string("a.json"));
    CHECK(classify("scene 1.json", 64) == std::string("scene 1.json"));
    // 无终止（区满无 NUL）→ 丢弃：构造 zone 无终止（len==容量）
    {
        std::string zone("demo.json.demo.json.demo.json.demo");  // 无 NUL
        // 名称区无终止：len 指向无 NUL 区域
        auto r = classify_event_name(zone.data(), zone.size());
        CHECK(!r.has_value());
    }
    // len==0 → 丢弃
    CHECK(!classify_event_name("", 0).has_value());
    // 含 / → 丢弃
    CHECK(!classify("dir/a.json", 32).has_value());
    // . 与 ..
    CHECK(!classify(".", 8).has_value());
    CHECK(!classify("..", 8).has_value());
    // 超长（≥ kNameMax）
    CHECK(!classify(std::string(64, 'a') + ".json", 128).has_value());
    // 后缀：非 json 不报告；大写不折叠；长度不足 6
    CHECK(!classify("a.txt", 16).has_value());
    CHECK(!classify("A.JSON", 16).has_value());
    CHECK(!classify("x.jsonx", 16).has_value());
    // 恰好 .json（名字 = ".json"？长度 5 → 前缀不足）
    CHECK(!classify(".json", 16).has_value());
    return ok;
}

// ── Ipc 集成测试：本地 socket 客户端 ──

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

// recv 一行（\n 结尾）；超时兜底。返回行内容（不含\n）；失败返回空串+ok=false。
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
            return false;  // 超时/错误
        }
    }
}

bool send_line(int fd, const std::string& line) {
    std::string s = line;
    s.push_back('\n');
    return send(fd, s.data(), s.size(), 0) == static_cast<ssize_t>(s.size());
}

bool test_ipc_basic() {
    bool ok = true;
    // 用固定高位端口避免冲突：48764 是默认，测试换个 49300+ 动态取
    // 简化：随机偏移（进程内唯一）
    static int seq = 0;
    const int port = 49000 + (getpid() % 500) + seq++;
    auto ipc = tg::Ipc::create(static_cast<std::uint16_t>(port));
    REQUIRE(ipc.valid());

    // 注册 handler：把命令名记录进向量；对 help 返回 handled
    int handled_cmds = 0;
    ipc.set_handler([&](const std::string& cmd, const tg::Json&,
                        std::optional<tg::Json>& data, std::string&) {
        ++handled_cmds;
        if (cmd == "boom") {           // 模拟 game handler 抛异常（兜底测试）
            throw std::runtime_error("handler boom");
        }
        if (cmd == "help") {
            data = tg::Json::object();  // handled 必须 object
            data->emplace("commands", tg::Json::array());
            return tg::IpcStatus::handled;
        }
        return tg::IpcStatus::not_handled;
    });

    int cfd = -1;
    REQUIRE(connect_client(port, cfd));

    // 首 poll：accept + hello（本 poll 不再读首行）
    ipc.poll();
    // 第二 poll：读客户端发来的第一行
    // （客户端此刻还没发；只验证 hello 已收到）
    // 读 hello（可能已在本机缓冲）
    std::string hello;
    if (!recv_line(cfd, hello)) { CHECK(false); }
    CHECK(hello.find("\"event\":\"hello\"") != std::string::npos);

    // 发 ping → 应回 pong（engine 内置，不经过 handler）
    CHECK(send_line(cfd, R"({"cmd":"ping"})"));
    ipc.poll();
    std::string pong;
    if (!recv_line(cfd, pong)) { CHECK(false); }
    CHECK(pong.find("\"pong\":true") != std::string::npos);

    // help → handler handled
    CHECK(send_line(cfd, R"({"cmd":"help"})"));
    ipc.poll();
    std::string help;
    if (!recv_line(cfd, help)) { CHECK(false); }
    CHECK(help.find("\"ok\":true") != std::string::npos);
    CHECK(handled_cmds == 1);

    // status → handler not_handled → command not handled
    CHECK(send_line(cfd, R"({"cmd":"status"})"));
    ipc.poll();
    std::string st;
    if (!recv_line(cfd, st)) { CHECK(false); }
    CHECK(st.find("command not handled") != std::string::npos);
    CHECK(handled_cmds == 2);  // 也经过 handler

    // 未知命令 → 也进 handler
    CHECK(send_line(cfd, R"({"cmd":"zzz"})"));
    ipc.poll();
    std::string z;
    if (!recv_line(cfd, z)) { CHECK(false); }
    CHECK(z.find("command not handled") != std::string::npos);
    CHECK(handled_cmds == 3);

    // handler 抛异常 → 兜底 internal error，连接继续可用（不杀进程）
    CHECK(send_line(cfd, R"({"cmd":"boom"})"));
    ipc.poll();
    std::string boom;
    if (!recv_line(cfd, boom)) { CHECK(false); }
    CHECK(boom.find("\"error\":\"internal error\"") != std::string::npos);
    CHECK(send_line(cfd, R"({"cmd":"ping"})"));
    ipc.poll();
    std::string p_boom;
    if (!recv_line(cfd, p_boom)) { CHECK(false); }
    CHECK(p_boom.find("\"pong\":true") != std::string::npos);

    // invalid request：非 object
    CHECK(send_line(cfd, R"(not json)"));
    ipc.poll();
    std::string inv;
    if (!recv_line(cfd, inv)) { CHECK(false); }
    CHECK(inv.find("invalid request") != std::string::npos);
    // 连接仍有效（继续可用）
    CHECK(send_line(cfd, R"({"cmd":"ping"})"));
    ipc.poll();
    std::string p2;
    if (!recv_line(cfd, p2)) { CHECK(false); }
    CHECK(p2.find("\"pong\":true") != std::string::npos);

    ::close(cfd);
    return ok;
}

// ── Watcher 集成（真实 inotify）：建临时目录 → 写 demo.json → 防抖后报告 ──
bool test_watcher_integration() {
    bool ok = true;
    const std::string dir = "build/watcher_tmp";
    ::mkdir(dir.c_str(), 0755);
    auto watcher = tg::Watcher::create(dir);
    REQUIRE(watcher.valid());

    // 写合法 .json：触发 inotify CREATE + CLOSE_WRITE
    {
        const std::string p = dir + "/demo.json";
        std::ofstream f(p); f << "{}";
    }
    // 防抖：先 poll 收事件（窗口内不报告），等 180ms 后再 poll 应报告
    // （尾沿补触发：poll 无事件但窗口已过 → 报告 pending）
    bool reported = false;
    std::string name;
    for (int i = 0; i < 30; ++i) {
        usleep(30000);  // 30ms 间隔，共 ~600ms（覆盖 150ms 防抖）
        auto r = watcher.poll();
        if (r) {
            name = *r;
            reported = true;
            break;
        }
    }
    CHECK(reported);
    CHECK(name == "demo.json");

    // 非 json 文件不报告
    {
        const std::string p = dir + "/other.txt";
        std::ofstream f2(p); f2 << "x";
    }
    reported = false;
    for (int i = 0; i < 30; ++i) {
        usleep(30000);
        auto r = watcher.poll();
        if (r) { reported = true; break; }
    }
    CHECK(!reported);   // .txt 不报告

    watcher.shutdown();
    return ok;
}

}  // namespace

int main() {
    test_classify();
    const bool r1 = test_ipc_basic();
    const bool r2 = test_watcher_integration();
    std::printf("[watcher/ipc test] checks=%d failures=%d\n", ::tg_test::g_checks,
                ::tg_test::g_failures);
    return (::tg_test::g_failures == 0 && r1 && r2) ? 0 : 1;
}

#endif  // TROGUE_DEBUG