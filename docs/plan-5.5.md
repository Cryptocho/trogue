# 里程碑 5 分卷 5：IPC（无 world 传输）与 watcher

> 前置阅读：综述、5.1。本卷把原 C 计划已定案（多轮审查沉淀）的 IPC 连接状态机 / 行上限 / 校验 / ping 不可覆盖 / serializer 兜底，以及 watcher 的后缀 / NUL / 分类语义，落到 C++ `tg::Ipc` / `tg::Watcher`。**engine 只做传输与 callback 分发，不拥有任何业务命令语义**；命令归属 game（5.6）。

## 1. `tg::Ipc` 目标形态

```cpp
// ipc.hpp
using Json = nlohmann::json;   // 注释：类型别名（仅 ipc 头需要）

enum class IpcStatus { handled, not_handled, error };

using IpcHandler = std::function<IpcStatus(
    const std::string& cmd, const Json& request,
    std::optional<Json>& data,   // 输出：handled 时置 data
    std::string& error)>;        // 输出：error 时置原因

class Ipc {
 public:
  static Ipc create(std::uint16_t port);      // Release/失败 → invalid（valid()==false）
  // 传输层只实现 ping；其余合法请求交 handler 或未注册时回 command not handled
  bool set_handler(IpcHandler);               // 注册在 poll 外；替换旧 handler
  void clear_handler();
  void poll();                                // 每帧：accept 到 EAGAIN → 轮询 active
  bool valid() const;
  // 禁止在 handler 内 clear/destroy/递归 poll
};
```

- `Ipc` 为 RAII 资源类（不可拷贝、可移动；持有监听 fd、连接槽、缓冲、handler）。
- handler 内 `data` 只在其调用期有效；engine 在包络后释放。
- handler 捕获 game 状态；**不可**保存 request 引用跨调用。

## 2. 传输契约（沿用 C 版定案，落 C++）

### 2.1 连接状态机（每次 poll）

1. **accept 阶段**：循环 accept 至 `EAGAIN/EWOULDBLOCK`；每个新 fd 只做 slot 检查 + 写 hello，**不 recv**；满 8 → 立即 close 且不发 hello；hello 写失败 → close 不占 slot 继续 accept；`EINTR` 重试；其他监听错误 → 记录并**永久关闭监听**（已有连接继续服务完）。
2. **轮询阶段**：按固定 slot 序处理 active：逐行读 → 解析 → 处理 → 写响应。新连接 hello 后**本 poll 不再读其首行**（下一 poll 首次 recv）。同一次 recv 多行在同一 poll 顺序处理完。
3. client recv/send `EAGAIN` → 留待下 poll；`EINTR` 重试；其他 → close 释放 slot。

### 2.2 行分帧与上限

- JSON-lines；**线上字节计数**（含 `\r`）上限 `kIpcLineMax=65536`，`\n` 不计；行尾 `\r\n` 在分帧层剥 `\r` 后再解析（engine 不依赖 parser 容忍 `\r`）；发送端只发 LF。
- 接收逐字节状态（沿用定案）：cnt 达 65536 后再收到非 `\n` → 超限：close、丢弃、不发响应、记日志。
- 边界测试（沿用）：65536+LF 分帧接受；65537 无换行关闭；65535+CRLF 剥后接受；超限后任意字节关闭。

### 2.3 请求处理

- 解析失败 / 非 object / 缺 `cmd`（非空 string）→ 固定 `{"ok":false,"error":"invalid request"}`，**不关连接**继续后续行。
- **`ping`**：engine 仅做一次 `cmd=="ping"` 精确比较；命中直接回 `{"ok":true,"data":{"pong":true,"version":1}}`；**不可被 handler 覆盖、无需 handler**。
- 其余合法请求 → handler。**handler 成功（handled）必须置 `data` 为 JSON object**（沿用 C 版「HANDLED 必须留下非 NULL object」）；data 非 object / 未置 → engine 视为错误并回固定 `{"ok":false,"error":"internal error"}`（不落半成品）。未注册 handler 或返回 `not_handled` → 固定 `command not handled`。
- engine **无业务命令 switch**（grep/负测试证明：注册记录命令名的 handler，逐一发 help/status/.../未知 cmd，确认都进 handler）。

### 2.4 响应与 serializer（统一状态机）

- 所有出站响应（ping、成功、各类错误）同一路径：构造包络 → 序列化(≤上限) → 发送。
- 构造/序列化失败 → 不发字节 → 尝试固定 `{"ok":false,"error":"internal error"}` 兜底 → 仍失败 → close 不发任何字节；**绝无半行**。
- error 文本超长截断（不构成失败）；handler data 超限 → 关闭连接不发部分。

### 2.5 Release 桩

- `TROGUE_DEBUG=OFF`：`Ipc::create` 返回 invalid；`valid()==false`；`poll/set/clear` 安全 no-op；不保存 userdata、不启动 TCP。公共头与符号形状与 Debug 一致。

## 3. `tg::Watcher` 目标形态

```cpp
class Watcher {
 public:
  static Watcher create(std::string_view dir);  // 监听目录；无效/非 Linux → invalid
  bool valid() const;
  std::optional<std::string> poll();   // 报告一个去抖后的合法 .json basename（裸名，无目录前缀）
  void shutdown();
};
```

- C 版 `name/cap` 参数问题由 `std::string` 返回值消除（不再有容量不足语义）；参数错误类别不复存在，只有「有效 watcher 无事件 → nullopt」与「无效 watcher → nullopt」。
- 防抖 150ms、尾沿补触发沿用；合并 pending 为单个最新 basename。
- 返回前对 basename 复核安全（无 `/`、非 `.`/`..`、无 NUL、长度 < `kNameMax`）。
- **path 拼接归 game**：game 用自身固定前缀 `assets/scenes` + `/` + basename 组 load 路径；engine 不拼。

### 3.1 basename 分类纯函数（detail，供单测）

```cpp
namespace detail {
// 输入 inotify 名称区字节与长度（含尾部 NUL/padding），输出合法 .json basename 或空
std::optional<std::string> classify_event_name(const char* zone, std::size_t len);
}
```

- 语义（沿用定案）：
  - `len==0` → 丢弃（nullopt）；
  - 以 len 为上界扫描终止 NUL（strnlen 逻辑）：无终止 → 丢弃；终止后 padding 忽略；
  - 名称含中段 NUL、`/`、等于 `.`/`..`、长度超限 → 丢弃；
  - 后缀：仅精确 ASCII 小写 `.json`（大小写不折叠，长度 ≥6）→ 报告；否则忽略（合法但不报告）。
  - 不做 UTF-8 判定（不透明字节）。
- `Watcher::poll()`（Debug 实现）读 inotify 事件后调用该函数，报告非空结果、丢弃其余；Release/无效 → nullopt。

## 4. 测试要点（无窗口为主）

- detail::classify_event_name：全分支表单测（len0/中段 NUL+padding/无终止/超长/.与..//、大小写后缀、合法+padding、`.json` 边界）。
- Ipc：无窗口集成——起 Ipc（Debug），用本地 socket 客户端脚本/进程发：hello 时序（首行下 poll）、多行连包、`\r\n`、65536/65537 边界、invalid request、ping 无 handler 可用、handler not_handled/error/data、未注册命令、超长 data 关连、监听错误路径（由 seam/注入或进程级覆盖，能测则测，不能则记录人工项）。
- 用 C++ 内起线程做测试 client，或复用 Python `tools/ipc_smoke.py`（5.6 更新）；Debug 才可连。
- Release：Ipc invalid + 桩 no-op；Watcher invalid nullopt。

## 5. 本卷决策清单

| # | 决策 |
|---|---|
| 1 | Ipc RAII、handler 为 std::function、data/error 出参、每帧 poll |
| 2 | 状态机/行上限/`\r\n`/ping/不覆盖/未注册/未接接整表沿用定案 |
| 3 | serializer 统一 + internal error 兜底，绝无半行 |
| 4 | 命令语义 0 个在 engine（仅 ping），负测试保证 |
| 5 | Watcher 返回 optional<string>，容量语义消解；分类纯函数可单测 |
| 6 | Release/非 Linux 桩（valid false / 安全 no-op） |
