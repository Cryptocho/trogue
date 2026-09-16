# BACKLOG —— 已定待办与评估队列

> 本文件收纳**已分诊但尚未排入里程碑**的改进项。与 Roadmap 的分工：
> Roadmap 是「已立里程碑 / 已交付」的主线清单；BACKLOG 是「已确认要做，或需先
> 评估」的缓冲区。条目升级为主线时走正常里程碑门禁（`docs/plan-*.md` + 审查），
> 从本文件移除并在 Roadmap 登记。
>
> 来源：Agent实际使用engine完整完成游戏开发（2026-09-14）。
> 分诊日期：2026-09-14。该探针的总体结论是「引擎运行时 0 bug，缺口在内容管线与
> 模板 QA」——因此立即项**全部落在模板与文档**，不动 `engine/` 公共 API。

---

## 已落地（2026-09-14，见 CHANGELOG）

- **A1** 图形/音频官方立场声明 → `engine/include/trogue/render.hpp` + 模板 `AGENTS.md`。
- **A2** `LoadImageFromScreen` 平台约束 → `render.hpp`（`render_scene_to_png` 附近）+ 模板 `game/src/main.cpp` 截图分支注释。
- **A3** 修复模板 `tools/gen_font.py`（顶层导入 + `--font` 描述 + `--index`）。
- **A4** 模板 IPC 输入校验规范（`AGENTS.md`）+ 每条命令负向路径断言（`tools/ipc_smoke.py`）+ handler 去裸 `.get<T>()`。
- **B1 T1** `tg::Random::draws()`（只读原始抽取计数）。T2/T3 明确不采纳。
- **B3（采纳部分）** 修工具 + 文档澄清（即 A3）；**不采纳**「升为一等标准模块」。

---

## B. 评估队列

### B2. `Ipc::publish` 返回送达情况（反馈 C9）——**维持现状**

- 当前「慢消费者/超限即断开、无出站队列」是**刻意设计的自愈模型**。要「统计丢弃」
  需引入出站队列，与既有设计冲突；返回瞬时写状态信息量极低。除非出现第二个消费
  方提出具体可观测需求，否则维持现状（丢弃不可观测属已知语义）。
- 触发条件：出现第二个消费方、且提出具体可观测需求时重议。

---

## C. 平台队列（2026-09-16 分诊）

### C1. 无 inotify 平台的自动热重载（目录快照轮询后端）

- 现状：`tg::Watcher` 在无 inotify 的平台为安全 no-op（`create` invalid），
  热重载靠 F5 / IPC `reload` 手动触发。功能闭环成立，但少了"改完资产自动生效"。
- 触发条件：有人在 macOS 上实际抱怨需要被动监听时再评估。
- **先不做的取舍**（不只是依赖成本问题）：
  - 轮询需每帧或定时遍历目录取 `last_write_time`/大小做 diff，与事件驱动的语义有差
    （延迟下限 = 轮询间隔；大目录需设规模上限），并且要在引擎里引入一份目录状态；
  - 若要复原"变的是哪个文件"，事件路径（inotify）天然提供 basename，轮询路径靠 diff
    推断——两条路径的语义需要显式对齐（同样 150ms 防抖 + 纯函数分类可复用）。
- 第三方方案的成本核对（若将来仍考虑引入）：
  - libuv 的限制是**目录级** `uv_fs_event` 在 macOS 拿不到文件名（kqueue 后端不报；
    其 ChangeLog 有 `filename arg to uv_fs_event_cb can be NULL` 与
    `document specific macOS fs_event behavior` 条目），且 `UV_FS_EVENT_STAT`/
    `UV_FS_EVENT_WATCH_ENTRY` 在头文件里写明 "currently not implemented yet on any
    backend"；kqueue **逐文件** vnode 监听（`<sys/event.h>` + kevent，零新依赖）其实
    能定位到触发文件，代价是目录枚举与 rename/moved_to 维护；
  - 任何"事件循环/额外线程"型方案与本项目「单线程、无锁、主循环每帧推进」的纪律冲突。

### C2. Windows 原生支持

- 现状：非目标（Agent-first 的运行环境为 Linux/WSL/macOS，均为 unix）。
- 已知阻断点（交叉编译扫描发现，与 hotreload 不同源）：`engine/src/ipc.cpp` 在
  `TROGUE_DEBUG` 下使用 `arpa/inet.h`/`netinet/*`/`sys/socket.h` 等 POSIX socket 头，
  Windows 目标下不存在；需要 winsock（`winsock2.h`/`ws2tcpip.h`）后端与 CMake 平台分支。
- 热重载侧已不是阻断点：`engine/src/hotreload.cpp` 的判定按"该目标是否有 inotify 头"，
  Windows 目标自动落到桩（可编译、可运行）。
- 触发条件：真的需要在 Windows 原生上跑引擎时再评估（WSL 已覆盖该需求）。

---

## D. 消费者探针观察（2026-09-16，两个范式范例用公共 API 实现时发现）

> 来源：`template/game/examples/` 的 `swarm`（ECS 风格）与 `platformer`（OOP 风格）在
> **只用公共头**的前提下实现完整可玩游戏。均为「可用但不够顺手」级别的摩擦，不阻断开发；
> 是否动手需按「引擎能力线」标准评估（机制性、确定性、可无头测试）。

### D1. `tg::Json` 别名落在 `ipc.hpp`

- 现状：`using Json = nlohmann::json;` 定义在 `engine/include/trogue/ipc.hpp`。纯逻辑
  模块（不依赖 raylib、不涉及 IPC）想拼一份 tro-scene JSON 时，只能 include 整个 IPC 头。
- 候选：把别名下移到 `types.hpp`（或 `scene.hpp`）。属公共 API 增补（非破坏），但需先
  确认与 `TROGUE_DEBUG=OFF` 的桩语义无牵连（别名本身与 DEBUG 无关）。

### D2. `SceneAsset` 只有 JSON 文本入口

- 现状：`load(path)` / `load_json(text)` 之外没有「按层/tile 缓冲直接构造」的公共入口。
  程序生成场景必须自己拼 JSON 字符串（`scene_gen`、内存建场景都如此）；无窗口纯逻辑
  路径要么同样拼 JSON，要么像 `swarm` 那样自建 `SolidGridView` 掩码绕开资产。
- 候选：评估结构化建场景入口的必要性；不得与「资产是只读快照」的定位冲突。

### D3. 碰撞视图/层约定的易错面

- `SolidGridView` 是 9 字段聚合体 + 裸 `const uint8_t* mask`：聚合初始化按声明序，将来
  加字段会静默错位；视图按值搬容器会悬垂（RAII 的 `SolidGrid` 只能从 asset 层物化）。
- **solid 层索引是隐式契约**：game 与建场景工具靠「层 1 是 solid」的约定对齐，错位只能
  在 `SolidGrid::load` 返回 error 时才发现；可考虑场景侧暴露 solid 层索引查询。
- `rect_hits_solid` 对非正尺寸/非有限参数返回 `error`（不是 `clear`）：只判 `== solid`
  无碍，但「把 error 当可通行」是踩得到的坑，文档可显式点明。

### D4. 刚生成的实体没有「立即探地」

- 现状：`grounded` 只在 `kinematic_step` 内派生，`reset`/spawn 后未跑一步时恒为 false；
  公共头没有等价的「对当前 box 做一次探地」（`is_solid_at` 是点查询，而探地是底边下方
  1px 的**矩形**查询，语义不同）。
- 影响：手感代码与测试容易假设「落地即 grounded」而写错（范例测试已踩到）。
- 候选：评估是否导出一次性的探地谓词；若不做，至少可在 `collision.hpp` 写明该差异。
