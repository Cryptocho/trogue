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
- **C3** 相机视口级场景渲染 → `tg::render_scene(asset, std::optional<Rect> viewport)` overload +
  `RenderStats::culled_tiles` 字段（CPU-only，段② 前累加）+ 4 个新 TEST_BODY + swarm 迁移
  （512×512 程序化地图从 20 fps 恢复 60 fps 上限）。流式 chunk 拆分划归 sim 侧职责，
  `SceneSpec`+`load_json` 已暴露，不开新引擎 API。

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

## D. 消费者探针观察（2026-09-16）——**已全部落地**（见 CHANGELOG）

> 来源：`template/game/examples/` 的 `swarm`（ECS 风格）与 `platformer`（OOP 风格）在
> **只用公共头**的前提下实现完整可玩游戏时暴露的 4 项摩擦。四项均已按「引擎能力线」
> 标准处理：机制性、确定性、可无头测试的进引擎，其余明确不采纳并记理由。

- **D1 已落地**：`tg::Json` 别名改为在 `scene.hpp` 声明（`ipc.hpp` 保留自己那份——两处独立
  声明，避免 ipc↔scene 包含耦合）。纯逻辑模块拼 JSON 不必再 include IPC 头。判据 = 只
  include `trogue/scene.hpp` 的编译期断言单测，删别名即编译失败。
- **D2 已落地（采纳类型化写路径）**：`tg::SceneSpec`/`SceneLayerSpec`/`TilesetRef` +
  `SceneAsset::create(spec)`（序列化后仍走 `load_json`，校验单一来源）+ `scene_spec_to_json`。
  **不采纳**把实体镜像成 `std::vector<SceneEntity>`：`SceneEntity` 是读路径快照、不含
  `animations`，镜像会把含动画的实体静默丢字段；实体在 `SceneSpec` 中按 schema 原样携带 JSON。
  四个消费方已改用（`tools/scene_gen`、模板内置场景、`examples/common/scene_make.hpp`、
  `game` 的 `genmap`），其中 `genmap` 的 wire 契约与 `scene_gen` 的产物字节均回归确认未变。
- **D3 已落地**：`SolidGrid::create(谓词建格)`（非资产来源掩码；`stride == width`、
  `layer_id == -1` 使 `set_tile` fail-loud；`refresh` 会整体替换掩码，故明文禁止用于该形态）、
  `SceneAsset::solid_layer_indices()`（把「层 1 是 solid」从 game/tool 的散落约定变成查询）、
  `collision.hpp` 显式写明三态纪律「`error`（参数非法）≠ 可通行」。**不采纳**
  `SolidGridView::from_mask`：与 `create` 职责重叠且无真实消费方。
- **D4 已落地**：导出 `probe_grounded`（视图 / one_way / asset 三重载，返回
  `solid|clear|error`；one_way 命中同样报 `solid`，参数非法先判 `error`），并让
  `kinematic_step` 的探地改调它——探针语义只有一份实现。**不采纳**导出 `probe_wall_dir`：
  无消费方，留在实现内部同样保证单一实现。
