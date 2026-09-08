# 里程碑 5 分卷 6：game demo、consumer smoke、迁移矩阵、实施步骤与验收

> 前置阅读：综述及其余分卷。本卷是把前 5 卷落地的收口：game demo C++ 化与新命令归属、consumer smoke/CMake/工具、旧 C11 → 新 C++ 迁移矩阵与历史文档清理（H 清单）、分步实施、验证验收、文件变更清单与门禁步骤 3–7。

## 1. game demo C++ 化（作为新 API 的 consumer 验证）

- `game/` 全部迁到 C++20，仅使用 `trogue/*.hpp` 公共 API；不依赖任何历史 C 头。
- demo 保留能力：加载场景、WASD/方向移动（game 逻辑）、相机、HUD、热重载（watcher+F5+IPC reload）、IPC 调试命令、截图、quit；**动画/Tween 用 5.4 播放器示范**（如简单 idle/walk 切换 + 移动补间）。
- 命令归属表（**全部由 game handler 实现**，engine 只传 ping）：

| 命令 | owner | 实现/状态 |
|---|---|---|
| ping | engine | 传输层直接回（见 5.5） |
| help/status | game | demo 命令表 + 场景/计数/uptime |
| list_entities/get_entity/query/set/spawn/despawn | game | 基于 game 自有对象（非引擎实体） |
| layers/solid_at/get_tile | game | 查 asset（tile 查询 API，5.2 §4）；solid_at 可含 game 对象 |
| reload | game | candidate load→重取快照→帧外 swap |
| screenshot | game | 排队，帧后 ExportImage |
| log | game | TraceLog |
| quit | game | 置标志，poll 后退出主循环 |

- 这些命令语义与 wire 保持兼容旧 `tools/ipc_smoke.py`（见 §3），证明是 app policy 而非 engine contract。

## 2. consumer smoke / 测试体系 / CMake

- 沿用 5.1 §7：`tools/` 仅当 `TROGUE_BUILD_CONSUMER_SMOKES AND BUILD_TESTING`。
- smoke 目标（无窗口，链接 `trogue_engine`）：
  - `trogue_oop_client_smoke`：自建 OOP 风格对象，import 场景 descriptor、跑 tile 查询、构建并播放一个内联动画集/补间（虚拟时钟）、创建 Ipc 断言 Debug/Release 桩行为。
  - `trogue_ecs_client_smoke`：自建极简 ECS（Position 组件等）消费同一套 engine API，证明模型无关。
- 路径/解析单测目标（无窗口）：schema 拒绝规则全集、路径 grammar、限额、watcher 分类、Ipc 状态机（本地 client 线程或脚本）。
- working directory = `${CMAKE_SOURCE_DIR}`；`add_test` 注册；Debug/Release 各自 `ctest --test-dir <build>`。
- **Release 验证**：smoke 中 Ipc::create 返回 invalid、Watcher invalid；渲染调用不触 GPU（无窗口程序不建窗口）。

## 3. tools/ipc_smoke.py 更新

- 保留对 **Debug demo** 的端到端断言；hello/响应用持久 line reader（沿已有修正：一次 recv 多行不得丢）。
- 命令仍走 game handler 实现的兼容 wire；脚本启动路径统一 `./build/bin/trogue`。
- Release 不跑此脚本（不启 TCP）。
- 若新增命令（动画/tween 观测）随 demo 加，本卷不强制；冒烟基线 = 原有断言在 game handler 下全通过。

## 4. 迁移矩阵：旧 C11 → 新 C++

> 目标：最终 `engine/` 无历史 C 公共符号；`include/trogue/` 仅 `.hpp`。

| 旧（C11，将被删除） | 新（C++） | 归属 |
|---|---|---|
| `world.h/world.c`、`TgWorld`、`TgEntity`、`TgTileLayer`、`tg_world_*`、实体池 | 删除；无运行时世界/实体 | engine 不再拥有 |
| `tg_scene_load/reload` | `SceneAsset::load` + game reload coordinator | engine 一次性加载；game 交换 |
| `tg_tileset_load/destroy`、公开 `TgTileset` 字段 | asset 私有图集资源（5.3） | engine 私有 |
| `tg_render_world/...` | `render_scene`/`render_sprite`（5.3） | engine 显式原语 |
| `tg_ipc_start(port, world)`、engine 命令 switch、`tg_ipc_take_screenshot` 等 | `Ipc`（5.5）；截图/退出等归 game | game |
| `tg_watcher_poll(w,name,cap)` | `Watcher::poll()` → optional<string>（5.5） | engine 通知 |
| `tg_parse_hex_color`/路径 helper | detail 工具（5.2） | engine 私有 |
| 资产帧动画透传不消费 | `AnimationPlayer`/`TweenManager`（5.4） | engine 通用原语 |
| C11 `game/src/main.c` 及命令 | C++ game demo（§1） | game |
| `tools/ipc_smoke.py` | 更新（§3） | 工具 |
| 源文件 `.c/.h` | `.cpp/.hpp`（5.1 §4） | engine/game/tools |

- 实现顺序约束：每删一个旧符号即更新对应新消费者，保持任一提交可构建（或按里程碑内小步提交，禁止长期双轨）。

## 5. 历史文档清理（H 清单，AGENTS 同步）——已完成（2026-09-07）

> H1–H10 全部逐项回填落实完毕（H6 的「处理结果」=「清单已完成使命」），落实明细已写入 AGENTS.md 对应章节（架构分层/API 边界、IPC 命令归属、schema 动画消费、语言/依赖/目录、CHANGELOG 模块、历史实现阶段记录）；本节清单一并收尾删除。

## 6. 实施步骤（用户批准后；含门禁 3–7）

1. AGENTS 追加「已拍板设计决策」记录段（不动现状措辞）。
2. **5.1**：建 CMake/目录/公共头骨架、nlohmann 引入 + 自研协程原语（`trogue/coro.hpp`，5.1 §5.2 决策记录）、基础值类型、伞头；删 world.h/world.c；最小 consumer 编译通过。
3. **5.2**：detail JSON 校验/路径/颜色工具 → `SceneAsset` 解析与查询 → schema 拒绝单测、查询单测。
4. **5.3**：Texture/缓存/render 原语 → 带窗口 demo 最小渲染接入前先与 5.6 demo 配合，此处先提供实现与无窗口安全断言。
5. **5.4**：动画集结构校验接入 load → AnimationPlayer/TweenManager/协程等待 → 无窗口虚拟时钟单测。
6. **5.5**：Ipc/Watcher C++ 实现与 seam → 状态机/边界测试；Release 桩。
7. **5.6 §1–3**：game demo C++ 化接入全部原语与命令；ipc_smoke 更新；consumer smoke/CTest。
8. 全量验证（§7）→ **subagent 代码评审** → 修复循环至 PASS（评审前不碰 CHANGELOG）。
9. CHANGELOG 更新、AGENTS 一次回填（含 H1–H10 各「处理结果」列，H6 完成后删本清单）→ **先询问用户是否写 commit message**（AGENTS 流程步骤 6）；如需则给出英文 commit message 预览待用户确认 → 用户确认后提交（不推送）。

## 7. 验证与验收

- **构建**：Debug/Release 双配置、`-Wall -Wextra -Wpedantic` 零告警；consumer smoke option 默认 ON 可关（关闭验收：不产 smoke target/测试）。
- **无窗口测试**：schema 拒绝全集、查询边界、路径、限额、watcher 分类、Ipc 状态机边界、动画/Tween 虚拟时钟序列（5.2/5.4/5.5 要点汇总执行）。
- **带窗口验证**（demo）：demo/test 场景渲染截图、图集/palette/bare、动画播放抽帧、热重载后旧 asset 析构安全、贴图缺失每路径一次日志、截图落盘。
- **IPC 端到端**：`tools/ipc_smoke.py` 全断言通过（响应来自 game handler）；多连接/连包/`\r\n`/超长等扩展断言。
- **符号门禁**：公共头 grep 无 `TgWorld/TgEntity/tg_world_` 等历史符号；生产库导出符号仅公共 API；测试库相对生产库多出符号 = **5.1 §7.2 seam 白名单**（`render_test_stats`、asset_id seam、5.5 定义者），其余一律禁止。
- **文档门禁**：AGENTS/CHANGELOG 按 §5/§6 更新；`git diff --check`；临时文件清理。
- **回归（写实）**：`assets/scenes/demo.json`（palette 模式，无 tileset JSON）与 `test.json`/`tile_map_layer.json`（图集模式，各依赖其 tileset JSON）在新 parser 下全部可加载；`assets/scenes/soldier_animated_sprite_2d.json`（bare + 内嵌 7 动画 43 帧）为**显式可加载+动画可播**回归项。注意 `tools/ipc_smoke.py` 的 layers/solid_at/get_tile wire 断言基于 **palette 的 demo.json**（`tileset=null`），不覆盖图集层 wire——图集场景的 `layers.tileset` 字段经 game handler 由 `LayerInfo.tileset_name` 输出，需在 demo 级补充一条图集断言或记录为已知覆盖缺口（实现时在 ipc_smoke 注释写明）。

## 8. 文件变更清单（预期）

删除：`engine/include/trogue/world.h tileset.h`、`engine/src/world.c scene.c tileset.c`、旧 game C 源等（详随实现定，写入 CHANGELOG）。

新增/修改（示意，以 5.1 §4 布局为准）：
`engine/include/trogue/*.hpp`（config/types/scene/render/animation/tween/hotreload/ipc/coro/trogue 伞）、`engine/src/**`、`game/src/**`（C++）、`tools/**`（smoke/单测/CMake/ipc_smoke.py 更新）、顶层与各 `CMakeLists.txt`、`AGENTS.md`、`CHANGELOG.md`。

不修改：`trogue-orign/`、`editor/`（资产格式 v2.1 不变；可能仅新增/校验 assets 中动画素材是否满足严格 schema，若发现问题列独立修复项汇报用户，不静默改）。

## 9. 遗留（下一里程碑）

- game-side 完整玩法（ECS 或 OOP）移植；RuleEngine 事件管线；autotile 渲染；音频/输入模块；二进制资产格式。
- **独立 tro-animations 文件加载**：本里程碑只消费 scene 内嵌动画集（5.4 §2 范围定案）；`AnimationSet::load(path)`（tro-animations v1，剥 `format`/`version`）与独立动画素材接入留待后续。
- 计划书整组在实现后按 H6 收尾归档。
