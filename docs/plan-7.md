# 里程碑 7：tg::Ipc 事件通道（subscribe / publish / disconnect，inspector 式可观测传输）

- 计划日期：2026-09-09
- 前置：里程碑 5（C++ 引擎迁移）、里程碑 6（最小闭环）已完成；依赖系统包就绪
- 参考：`AGENTS.md`「IPC 协议：tro-ipc v1.1」「引擎公共 API 边界」；`engine/src/ipc.cpp` 现状

## 1. 目的与范围

**目的**：为 Agent 提供**异步事件推送通道**，使 `tg::Ipc` 从纯请求-响应升级为 inspector 式
可观测传输：Agent 可以在长连接上订阅具名事件并持续接收推送（`event` 行），同时短连接
RPC 保持零干扰。**本里程碑只交付通道本身（engine 传输层），不定义、不实现任何具体
game 事件**——`MoveStarted`/`MoveFinished` 等仅为协议形态示例（曾用于讨论），不属于
本里程碑范围；未来 game 需要什么事件（移动、ECS 组件变更等）由 game 自行注册与发布。

**范围（本里程碑实现）**：

1. engine `tg::Ipc` 新增（全部在 `TROGUE_DEBUG` 段内，Release 桩 API 形状一致）：
   - 连接身份：accept 时分配**单调递增 `conn_id`**（`uint64`，从 1 起，0 保留为无效值；
     断开后不复用，避免 game 持旧 id 误杀新连接）。
   - **订阅表**：按连接槽位存储订阅的事件名集合；`close_slot` 统一清空——对端关闭、
     写失败、game 主动断开三条路径都走它，「**断开即订阅清零**」自动成立。
   - **引擎保留命令**（与 `ping` 同档：`handle_line` 内先于 game handler 拦截，不可被
     game 覆盖；无需 game handler 即可用）：
     - `subscribe {events:[...], filter?}` / `unsubscribe {events:[...]}`（详见 §3.2；
       subscribe 可选 `filter`：对事件 data 顶层字段等值匹配，实现**单实体观测**
       ——ECS 调试场景的核心能力，见 §2 决策表）；
     - `connections`：列出连接与其订阅集（详见 §3.2）。
   - **`publish(event, data)`**：公有方法，向订阅了该事件的连接广播一行
     `{"ok":true,"event":E,"data":{...}}`（详见 §3.3）。
   - **`disconnect(conn_id)`**：公有方法，game 层可调用——主动断开指定连接并自动清订阅。
   - **`connections()`**：公有方法，返回连接快照 `std::vector<IpcConnInfo>`，供 game
     枚举后再定点断开。
2. game 层**唯一**触碰：IPC `events` 目录命令（按既定草图「由 handler 转发给 game」）——
   `main.cpp` 的 handler 新增一条命令，返回 game 当前的事件注册表；本里程碑注册表为空
   `{"events":[]}`（机制就位，未来 game 注册事件时只改 game 这张表）。**不触碰任何
   玩法代码**（move/tween 等保持原样，用户已拍板：那是 game 层测试脚手架，不动）。
3. 测试与文档：
   - engine 无窗口单测（进程内真实 loopback socket，见 §6）；
   - `tools/ipc_smoke.py` 增加通道协议断言（不依赖任何 game 事件）；
   - `AGENTS.md` IPC 章节增补事件节与脚本约定（tro-ipc v1.2，协议版本仍为 1，只增不改）。

**明确不做**：
- 任何具体 game 事件（MoveStarted/MoveFinished/组件变更等——仅为示例，未实现）；
- 事件回放/持久化（晚接入的订阅者收不到历史事件，靠「先订阅后触发」纪律）；
- 通配符订阅 `*`（Agent 可先查 `events` 再全订，协议保持最小）；
- 每连接出站队列（用户拍板：直写 + EAGAIN 即断开，见 §3.3）；
- wire 层 `disconnect` 命令（断开是 game 层函数 + 客户端自己关连接，不暴露命令）。

## 2. 语义决策记录（2026-09-09 讨论拍板）

| 决策点 | 结论 |
|--------|------|
| 慢消费者（订阅了但停止读取） | **直写 + EAGAIN 即断开**：沿用现有 `send_packet` 语义（非阻塞写不进 → close 槽位），订阅随之清零；零新增状态，永不阻塞主循环 |
| game 层断开 API 形状 | `connections()` 快照 + `disconnect(conn_id)` 定点断开，**外加 IPC `connections` 查询命令**（Agent 也能看到谁在订阅） |
| game 玩法代码 | **零改动**：move/tween 是 game 层测试脚手架，不统一、不重构；事件负载坐标等细节留待 game 真正注册事件时再定 |
| 事件目录归属 | `events` 命令由 handler 转发给 game（game 返回注册表，当前为空）；engine 对事件名保持无知（不校验、不注册） |
| 事件行形态 | 与既有 hello 问候同构：`{"ok":true,"event":"<名>","data":{...}}`；判别式 = **响应永远不含 `event` 键**（老客户端零感知） |
| 事件超限语义 | **与响应超限分叉**：响应超限 = 换兜底错误行、连接保持（现状）；事件超限 = **无兜底行、直接断开**（向订阅连接发无 `event` 键的兜底行会破坏判别式，禁止） |
| 单实体观测（ECS 调试） | `subscribe` 增加可选 `filter`：对事件 `data` **顶层字段做 JSON 等值匹配**（多键 AND，缺键不匹配）；engine 只做键值相等判断、不识 `entity` 等任何键的语义（2026-09-09 拍板：避免「每实体挂日志组件」式洪水，wire 上只留关心的行） |

## 3. 设计要点

### 3.1 连接身份与订阅表

- `Ipc::Impl` 槽位结构扩展：`conn[i]`（fd）旁新增 `conn_id[i]`（0 = 空闲）与
  `subs[i]`（订阅记录向量，元素 = (事件名, filter) 二元组，见 §3.2/§3.4；去重保序）。
  固定 8 槽架构不变，无动态分配。
- accept 成功时 `++next_conn_id` 写入槽位；`close_slot` 清 `conn_id=0` + `subs.clear()`
  ——三个断开路径（`service_slot` 读到 EOF/错误、`handle_line` 写失败、`disconnect`）
  全部经由 `close_slot`，订阅清理单点收口。
- `publish` 的慢消费者断开也走 `close_slot` → 「断开即订阅清零」对所有路径成立。

### 3.2 引擎保留命令（ping 档，先于 game handler）

- `subscribe`：req `{"cmd":"subscribe","events":["A","B"],"filter":{...}?}`。
  - 校验：`events` 必须为**非空数组且元素全为非空字符串**（空数组/缺失/类型不对 →
    `{"ok":false,"error":"subscribe needs non-empty string array"}`，连接保持）；
    保留名 `"hello"` 拒绝（`error:"reserved event name"`，整条请求拒绝不做部分订阅）；
    `filter` 可选，出现则必须为 object（否则 `error:"filter must be object"`），
    作用于本条请求的**全部**事件名。
  - 语义：订阅记录 = **(事件名, filter)** 二元组（filter 缺省 = 无过滤）；同对去重、
    幂等；同名不同 filter 的记录并存。响应 = 操作后该连接的**全量订阅集**
    `{"ok":true,"data":{"events":[{"event":"A"},{"event":"B","filter":{...}}]}}`
    （对象数组，无 filter 者省略该键；自描述，便于脚本断言）。
  - **filter 匹配语义（单实体观测）**：对事件 `data` **顶层字段做 JSON 等值匹配**
    （nlohmann 深比较、键序无关；多键 = AND；`data` 缺该键 → 不匹配——缺键探测用
    `find()`/`contains()`，**禁用非 const `operator[]`**（会向 data 插入 null 键，把
    缺键变假匹配并污染 data）；数字等值 = **数值相等**（nlohmann 跨类型提升比较，
    `1 == 1.0`）；空 object `{}` = AND 空集 = 恒真，作为独立记录与「无过滤」并存）。
    engine 不理解任何键的语义（`entity` 只是 game 约定的普通字段）。
- `unsubscribe`：req `{"cmd":"unsubscribe","events":[...]}`（无 filter 参数）——
  按事件名**整体移除**该名的全部记录（含所有 filter 变体）。校验比 subscribe 宽：
  **仅空数组与 `"hello"` 两处放宽**，其余同（缺失 `events` 键/非数组/元素非字符串
  仍报错）：
  - 空数组 → **no-op、回显当前集、不报错**（「没有要退订的东西」不是客户端错误）；
    要清空订阅必须**显式列出当前集内的名字**（可从 `subscribe` 回显或 `connections`
    取得）——不做「空数组 = 全清」的隐式语义；
  - 未订阅的项 → no-op（幂等，不报错）；`"hello"` 不做保留名校验——它本就订不上，
    退订恒为 no-op。
- `connections`：req `{}`；响应 `{"ok":true,"data":{"connections":[{"conn":N,
  "events":[<订阅对象，形态同 subscribe 回显>]}],"count":N}}`（含未订阅连接，
  `events` 为空数组）。
- 校验细节与错误文案风格对齐既有命令（如 `"move needs integer dx/dy"`）。

### 3.3 publish（game 调用，engine 广播）

- 签名：`void publish(std::string_view event, const Json& data);`
  - `data` 必须为 JSON object（进包络 `data` 字段）；非 object 时 TraceLog 警告并跳过
    （防御性，不产生坏行、不崩溃）。
- **出站路径与响应分叉（审查修正）**：publish **不复用 `send_packet`**——该函数超限时
  会把行替换为 `{"ok":false,"error":"response too large"}` 发出且**连接保持**，这对事件
  是判别式漏洞（向订阅连接发无 `event` 键的假响应行 → 客户端误分类、请求-响应错位）。
  publish 自行构造事件行：`dump()` 后 `> kMaxLine-1` → TraceLog 警告 +
  **`close_slot` 断开、不发任何行**（事件没有可降级的兜底行；超限为整行一次性判定，
  即该事件的全部 **filter 匹配**订阅者同批断开，不匹配者既不收行也不被断开）；
  未超限 → 底层
  `write_full` 直写。实现上从 `send_packet` 拆出共用的 `send_line(fd, 行)` 底层即可，
  两条路径的超限策略刻意不同（见 §2 决策表）。`dump()` 抛异常（如非法 UTF-8，
  `type_error.316`）→ try/catch → TraceLog 警告 + **跳过本事件**（此时未写任何字节，
  不断开无辜订阅者，与超限路径区分；`void` 签名不得让异常穿透公共 API）。
- 投递：遍历 8 槽，`subs[i]` 中存在「同名 且 (无 filter 或 filter 逐键等值匹配
  data)」的记录者按上路径直写；**非阻塞**，EAGAIN/写失败 →
  `close_slot`（慢消费者自愈，TraceLog 记录）。零订阅者时为一次空扫描，开销可忽略。
- 时序：publish 在调用瞬间写出。若在 game handler 执行期内发布，事件行先于该连接的
  响应行写出——单连接「订阅→发命令→读行」模式下 Agent 先读到 event 再读到
  response，顺序符合直觉（文档明示，§6 有顺序断言）。
- **重入角落（语义钉死）**：若事件写失败断开的**恰是当前正在服务的连接**（handler 内
  publish + 该连接是订阅者 + 恰逢 EAGAIN），则该命令的响应不再发出（对端见事件行/
  残行 + EOF）。现有 `service_slot` 处理前后的 `conn[idx]` 检查与 `close_slot` 空槽守卫
  已覆盖此路径——实现时在 `handle_line` 的 fd 快照处加注释说明，**禁止再「修」**。
- 线程纪律：**仅主线程**调用（与 `poll()`/`tick()` 同域，单线程无锁，架构约定不变）。

### 3.4 disconnect 与 connections（game 层 API）

- `bool disconnect(std::uint64_t conn_id);`：按 id 找槽 → `close_slot`；找不到返回
  false（幂等安全）。game 可先 `connections()` 枚举再定点断开（如清场、踢除残留
  监听连接）。
- `std::vector<IpcConnInfo> connections() const;`；公共值类型（纯数据，符合编码规范）：

  ```cpp
  struct IpcSubscription {        // 订阅记录快照
      std::string event;
      Json filter;                // null = 无过滤（wire 上省略 filter 键，引擎内
  };                              //   null↔省略 双向转换）；否则 object（§3.2）
  struct IpcConnInfo {
      std::uint64_t conn = 0;     // 0 = 无效（与「空闲槽」哨兵一致）
      std::vector<IpcSubscription> events;
  };
  ```
- wire 层的 `connections` 查询命令（§3.2）与这两个 API 无依赖关系：命令由 engine
  直接回答，不经过 game。

### 3.5 events 目录（game 命令）

- `main.cpp` handler 新增：`events` → 返回 game 事件注册表（本里程碑为空常量表
  `{"events":[]}`，元素形态预留 `{name, when, data}` 三字段文档）；`help` 同步登记
  `subscribe`/`unsubscribe`/`connections`/`events` 四条。
- engine 不感知注册表内容；`publish` 不校验事件名是否已注册（传输层无知，语义归 game）。
  注册表条目形态预留 `{name, when, data}`；game 应写明各事件 `data` 携带哪些**可过滤
  字段**（如 `entity`）——filter 的可用键由 game 文档化，engine 不校验。

### 3.6 Release 桩

- `TROGUE_DEBUG=OFF`：`publish`/`disconnect` no-op、`connections()` 返回空表、保留命令
  不可达（监听 socket 本就不存在）；API 形状与 Debug 完全一致，现有桩结构直接扩展。
- 桩行为**可测**（跟随 `watcher_ipc_test` 先例）：`ipc_test` 用 `#ifndef TROGUE_DEBUG`
  分支在 Release 下验证——invalid 实例上 `publish`/`disconnect` 不崩、`disconnect`
  返回 false、`connections()` 为空、`valid()==false`；两配置 ctest 目标数保持对称。

### 3.7 脚本约定（写入 AGENTS.md，非代码）

- **短连接 RPC** → 零订阅零干扰（`echo '{"cmd":"turn"}' | nc -q1 127.0.0.1 48764`）。
- **长连接订阅** → 专脚本读行：有 `event` 键 = 事件（hello 问候同形，跳过未知事件名），
  否则 = 响应。
- **单连接端到端模式**：同一连接 `subscribe → 发命令 → 循环读行`，先订阅后触发无竞态；
  handler 内发布的事件先于响应到达（§3.3）。
- **断开即订阅清零**；监听脚本必须带 `--duration/--count` 出口，防残留连接占满 8 槽。
- **断开的对端形态**：断开前最后可能是**残缺行（无 `\n`）+ EOF**（`write_full` 部分写
  入内核后才遇 EAGAIN 断开，已进内核的字节不收回）；读端把残行丢弃、视作断开即可
  （这是文档契约，不是巧合）。
- **判别式按顶层 `event` 键判断**（subscribe 回显/connections 元素内的嵌套 `event`
  键不是事件行）；收窄某实体的过滤 = 整名退订再重订更窄 filter（v1 无单独收缩某个
  filter 变体的命令）。

## 4. 文件与模块结构（目标）

```
engine/include/trogue/ipc.hpp   # IpcConnInfo + publish/disconnect/connections 声明（Debug/桩双形状）
engine/src/ipc.cpp              # conn_id、订阅表、保留命令、publish、close_slot 单点清订阅
game/src/main.cpp               # 仅 +events 目录命令与 help 登记（玩法代码零改动）
tools/tests/ipc_test.cpp        # 新增：通道单测（Debug 真实 loopback + Release 桩分支，
                                #   同 watcher_ipc_test.cpp 的双分支模式）
tools/ipc_smoke.py              # 新增通道协议断言组
docs/plan-7.md                  # 本文件
AGENTS.md / CHANGELOG.md        # 流程末尾更新
```

## 5. 实施步骤（按开发流程门禁 3–7）

1. 写计划书（本文件）→ 交 subagent 审查 → **PASS 才开工**；
2. 用户批准计划书；
3. 实现：
   a. `engine/include/trogue/ipc.hpp`：`IpcConnInfo` + 三个公有方法声明 + 注释（含
      「仅主线程」「断开即清订阅」「EAGAIN 即断开」契约）；顶部「唯一例外：ping」
      说明同步改写（保留命令 = ping + subscribe/unsubscribe/connections）；
   b. `engine/src/ipc.cpp`：槽位结构扩展、`close_slot` 单点清理、`subscribe`/
      `unsubscribe`/`connections` 保留命令、`publish`/`disconnect`/`connections` 实现
      （publish 出站路径与 `send_packet` 分叉，见 §3.3；`handle_line` fd 快照处加重入
      说明注释）、Release 桩同步；顺手修正文件头与 `send_packet` 内「超限关连」的
      过时注释（实际语义 = 换兜底行、连接保持）；
   c. `game/src/main.cpp`：`events` 命令（空注册表）+ `help` 登记；
   d. `tools/tests/ipc_test.cpp` + `tools/CMakeLists.txt`：通道单测（见 §6；
      `#ifndef TROGUE_DEBUG` 双分支，两配置均注册）；
   e. `tools/ipc_smoke.py`：协议断言组（见 §6）；
   f. 构建验证 Debug + Release（零 `-Wall -Wextra -Wpedantic` 告警），ctest 两配置
      全绿且目标数对称（9/9），smoke 全绿；
4. **subagent 检查**未提交代码（本流程规定禁止自检）；
5. 检查通过后更新 `CHANGELOG.md` + `AGENTS.md`（IPC 章节 v1.2 增补、公共 API 边界
   `tg::Ipc` 条目、架构分层图 ipc 行补「事件通道」、脚本约定含残行+EOF 契约、
   已知坑、Roadmap）；
6. 询问用户是否写 commit message → 英文预览等待确认；
7. 确认后提交全部变更。

## 6. 验证与验收

**单测（`tools/tests/ipc_test.cpp`，无窗口，进程内真实 loopback；helper 与端口策略
沿用 `watcher_ipc_test.cpp` 现成模式——`49000 + getpid()%500` 偏移 + 客户端重连）**：

- Debug 分支用例：订阅/退订响应与全量集语义；参数校验错误（subscribe 空数组/非字符串/
  保留名；unsubscribe 空数组 no-op）；`publish` 只达订阅者（多连接分流断言）；
  `unsubscribe` 后不再收到；客户端断开后 `connections()` 反映清零；`disconnect(conn_id)`
  后对端读到 EOF 且订阅清零；**handler 内 publish 顺序断言**（handler 捕获 `Ipc*` 在
  返回 handled 前 publish，客户端第一行含 `event` 键、第二行才是本次命令的响应）；
  事件超限（构造 >64KB data）→ 该连接被断开且**未收到任何行**；**filter 分流断言**
  （同事件名三连接：无过滤/命中过滤/不命中过滤 → 仅前两者收到；`data` 缺键 → 仅
  无过滤者收到；同名不同 filter 记录并存；unsubscribe 按名移除全部 filter 变体）；
  零订阅 publish 无
  副作用；引擎无 handler 时保留命令仍可用；
- 慢消费者用例：连接后不读 + 客户端 `SO_RCVBUF` 压小（内核托底 ~2.3KB）+ 批量
  publish 大事件；断开的观察通道 = 慢 fd 自身 recv 到 EOF，或另一条控制连接查
  `connections()` 计数回落；publish 循环上界按总字节 ≥4MB 设（远超内核 sndbuf），
  有界内断言必收敛；
- Release 分支（`#ifndef TROGUE_DEBUG`）：invalid 实例 `publish`/`disconnect` 不崩、
  `disconnect` 返回 false、`connections()` 为空、`valid()==false`。

**冒烟（`tools/ipc_smoke.py`，跑在真实 game 上，不依赖 game 事件）**：

- `subscribe`/`unsubscribe`/`connections` 的响应包络与校验错误；
- `events` → 空注册表；`help` 含四条新命令；
- 连接 A 订阅后，连接 B（短连接）RPC 不受任何干扰；A 关闭后 `connections` 计数回落；
- 原 30 项断言零回归。

**构建**：Debug + Release（`-DTROGUE_DEBUG=OFF`）零告警；ctest 两配置全绿。

## 7. 文件变更清单（预期）

- 新增：`docs/plan-7.md`、`tools/tests/ipc_test.cpp`
- 修改：`engine/include/trogue/ipc.hpp`、`engine/src/ipc.cpp`、`game/src/main.cpp`
  （仅 events/help）、`tools/ipc_smoke.py`、`tools/CMakeLists.txt`、
  `AGENTS.md`、`CHANGELOG.md`（流程末尾）

## 8. 遗留 / 风险

- **慢消费者断开语义**：突发大量事件可断开暂停读取的监听端（Agent 端表现为残行
  + EOF，重连即可）；未来如需要可加有界出站队列（已列为明确不做）。
- **无事件回放**：订阅前发生的事件不可补收；以「先订阅后触发」纪律 + 单连接端到端
  模式规避（文档明示）。
- **8 连接上限共享**：订阅连接与 RPC 连接同池；残留监听器会挤占槽位，靠脚本出口
  约定 + game 层 `disconnect`/`connections()` 兜底。
- 事件负载字段坐标等语义：待 game 真正注册第一批事件时随事件文档一起定义（本里程碑
  不涉及）。
- filter 仅支持 `data` **顶层字段**等值匹配（嵌套字段、数值比较/正则不在 v1）；
  数字等值 = 数值相等（nlohmann 跨类型提升，`1 == 1.0`）；game 约定 id 类可过滤
  字段用**字符串**（规避 >2⁵³ 大整数跨类型比较的精度坑）。
