# 里程碑 5 计划书（综述卷）：C++20 引擎重构 + 模型无关边界 + 通用表现原语

- 计划书编号：`docs/plan-5.md`（综述/导航卷）+ `docs/plan-5.1.md` ~ `docs/plan-5.6.md`（主题分卷）
- 日期：2026-09-07
- 状态：**用户已批准（2026-09-07）→ 实施前追加决策 → 整组重新送审 PASS（2026-09-07）**，可开工。追加决策记录：协程自研（5.1 §5.2）、不引入 raylib-cpp 与风格总纲（5.1 §8.1，AGENTS 已同步）；复审附 8 条非阻塞建议，已采纳并收口 5.1 §5.2/5.4 §5 的等待者纪律与 spawn_task 归属。
- 历史：原 `docs/plan-5.old-c11.md`（C11 版「引擎运行时边界重构 M5A」，曾经历多轮 C 计划审查）已**作废归档**。本卷及其分卷是**用户拍板 C++ 化之后**的新版里程碑计划，取代旧 C11 计划。

> 本卷是整组计划书的**总纲与导航**：目的、拍板决策、范围、分卷清单、跨卷一致性契约、门禁与实施步骤总览。各分卷承载可执行细节。阅读任何分卷前应先读本卷。

## 1. 背景与拍板决策（2026-09-07）

用户基于「趁项目尚小做架构级调整」的判断，拍板以下决策（均已同步至 `AGENTS.md`「引擎实现语言决策」）：

1. **引擎改用 C++20 实现，公共 API 为纯 C++**：`namespace tg`、不透明类/RAII 资源、值类型快照；不做其它语言绑定、无需 C ABI。
2. **JSON 库改用 nlohmann/json**（替换 jansson 2.14）；schema 校验**语义不变**，仅换实现载体。
3. **通用表现能力引擎内置**：帧动画播放器（消费 `tro-animations`/实体 `animations` 帧表）与补间 Tween 由 engine 提供**执行原语**；「何时触发哪条/如何切换」仍归 game。这是对「引擎像 LÖVE2D 一样把控制权交给使用者」的**细化而非推翻**——对象模型（OOP/ECS）仍由使用者自选，引擎不规定。
4. **协程采用自研最小原语** 作为演出脚本基础（动画/Tween 完成可 `co_await`）——原定 cppcoro，实施前置验证失败（上游停留 C++17 TS 的 `experimental/coroutine`，GCC 12+ 不可编译），经用户拍板自研 `tg::task`/`tg::generator`/事件 awaiter（记录于 5.1 §5.2）。
5. **与「模型无关边界重构」（原 M5A）合并为同一里程碑**：语言迁移与边界重构一次完成，避免两次大改两次门禁。
6. **演示程序（game/）同步 C++ 化**，作为新公共 API 的 consumer 验证。
7. **不引入 raylib-cpp**：引擎内部直接调 raylib C API（raylib 本就是 C 库），公共 API 不暴露 raylib 类型，RAII 由 `tg::` 资源类自管。
8. **公共 API 风格成文约束**：引擎不用 OOP 层级（无继承/虚函数/接口类）、自由函数优先（查询/渲染/推进）、RAII 类只持资源+薄封装、值类型纯数据；game 层不受约束（详见 5.1 §8.1，已同步 AGENTS 编码规范）。

> **2026-09-07 追加决策（用户批准后、实施前）**：① 协程改自研最小原语（cppcoro 前置验证失败，5.1 §5.2）；② 不引入 raylib-cpp；③ 公共 API 风格约束成文。据此整组重新送审（见 §6 门禁）。

另确认 **docs/ 计划书可拆分为多文件**（本卷 + 分卷），单卷可独立修订、整组送审。

## 2. 目的与完成后状态

完成后（相对当前 C11 代码）：

1. `engine/` 全部为 C++20；公共头为 `.hpp`、命名空间 `tg`；历史 `TgWorld`/`TgEntity`/`tg_*` C API 与实体池等已删除（见分卷 5.6 迁移矩阵）。
2. `tro-scene`/`tro-tileset` 由 `tg::SceneAsset`（RAII，不可变/只读）解析；实体经**值类型快照** `tg::SceneEntity` 取得；`tg::Animation`/`tg::Tween` 由 engine 内置提供。
3. engine 公共 API 无运行时世界/实体/ECS 概念；tile 查询、显式渲染、watcher 通知、无 world IPC 传输齐备。
4. 演示程序（game/）与工具链（consumer smoke、ipc_smoke）迁移到 C++/新 API，验证「使用者自选 OOP/ECS、引擎模型无关」。
5. `AGENTS.md`、`CHANGELOG.md` 按实际结果更新（门禁见本卷 §7）。

## 3. 分卷导航

| 卷 | 文件 | 内容 | 状态 |
|---|---|---|---|
| 0 | `docs/plan-5.md` | 综述：决策、范围、导航、跨卷契约、门禁与步骤总览 | 本卷 |
| 1 | `docs/plan-5.1.md` | C++ API 形态与工程基线：命名空间/类型策略、nlohmann/json、自研协程原语（§5.2）、目录与公共头清单、CMake/依赖、编码规范落点 | 撰写完成 |
| 2 | `docs/plan-5.2.md` | 资产解析与场景查询：`SceneAsset`/`SceneEntity`/`LayerInfo`、schema 校验规则全集（三态/tilemap/sprite 键策略/payload 限额/path grammar/空值边界）、tile-only 查询 | 撰写完成 |
| 3 | `docs/plan-5.3.md` | 显式渲染与贴图资源：`render_scene`/`render_sprite`、palette/图集/bare、`IsWindowReady` 三段顺序、贴图缓存 RAII 与失败哨兵、相机约定 | 撰写完成 |
| 4 | `docs/plan-5.4.md` | 通用表现原语：`tg::Animation` 帧动画播放器（消费 tro-animations/animations 帧表）、`tg::Tween`/`TweenManager`、自研协程演出 awaiter（5.1 §5.2）、game 触发边界 | 撰写完成 |
| 5 | `docs/plan-5.5.md` | IPC（无 world transport/callback，连接状态机/行上限/校验/ping/serializer/Release 桩）与 watcher（后缀/NUL/分类纯函数与 seam） | 撰写完成 |
| 6 | `docs/plan-5.6.md` | game demo C++ 化与命令归属、consumer smoke/CTest、ipc_smoke 更新、迁移矩阵与历史清理（H 清单）、步骤/验证/验收 | 撰写完成 |

各分卷**自包含可执行细节**，并引用本卷共享约定；修订只动对应卷。

## 4. 范围

### 做（见各分卷细化）

- 引擎整体 C++20 化、纯 C++ 公共 API、RAII 资源管理、`tg` 命名空间。
- nlohmann/json 解析替换 jansson；schema 校验语义保持（含既有 8 轮 C 版审查沉淀：键白名单、限额、路径 grammar、空值边界等——以分卷 5.2 为准）。
- 模型无关边界：删除 `TgWorld`/`TgEntity`/实体池/按 id 改实体/spawn/despawn/engine 命令语义；实体仅作只读 descriptor 值快照。
- tile-only 查询（solid 层/层外/floor/半开区间/短路）与显式渲染（不自动遍历 game 对象）。
- 帧动画播放器与 Tween（执行原语归 engine、触发决策归 game）+ 自研协程演出 awaiter。
- watcher 只报告变化；IPC 无 world 传输 + game callback（命令归属 game）。
- game demo 与工具链同步 C++ 化；consumer smoke（OOP/ECS 两种用法）证明模型无关。
- 测试体系：无窗口单测/路径 smoke + 带窗口验证；Debug/Release 双配置。

### 不做（仍后置）

- 不在 engine 实现 ECS/OOP 基类/组件 registry/对象池/动态碰撞/game 系统（属 game 里程碑）。
- 本阶段不实现 game-side 完整玩法移植（移动/回合/战斗等仍后置，依赖本里程碑）。
- autotile/bitmask 渲染、音频、Live2D/Rive2D 接入等维持后置。
- 不修改 `trogue-orign/`、`editor/`（Godot 项目与插件 schema v2.1 保持）。

## 5. 跨卷一致性契约（各分卷必须遵守）

1. **命名与符号**：公共 API 一律 `tg::`；头文件 `.hpp`；枚举/常量命名与 `AGENTS.md`「引擎公共 API 边界（目标）」一致（如 `SceneAsset`/`SceneEntity`/`Animation`/`Tween`/`Ipc`/`Watcher`）。旧 `Tg*`/`tg_*` 仅存在于历史迁移矩阵。
2. **值类型 vs RAII**：可复制快照用值类型 struct；持有资源者（SceneAsset、贴图、Ipc、Watcher、Tween 句柄）用 RAII 类，禁用裸 new/delete 泄漏面。
3. **错误策略**：可预期失败用 `tg::expected`（vendored tl::expected 别名，见 5.1 §6，亦可用 `std::optional`）显式表达 + 日志；不在公共 API 抛裸异常作主控制流（内部 `try/catch` 兜底）。
4. **JSON 依赖**：仅 nlohmann/json；schema 校验规则实现遵循分卷 5.2（禁止静默放宽既有校验语义）。
5. **协程依赖**：自研最小原语（`tg::task`/`tg::generator`/事件 awaiter，5.1 §5.2 决策记录），数据驱动播放器不依赖协程。
6. **测试 seam**：仅供测试编译的 seam 集中声明、生产构建不导出；**权威清单与测试库/白名单契约见 5.1 §7.2**（各分卷引用之），符号门禁以其为准。
7. **限额常量**：schema/配置限额在引擎公共配置头集中定义，命名避免多义（历史教训：同一常量不承担多级语义）。
8. **历史清理（H）清单**：`AGENTS.md` 冲突措辞与旧 API 删除项的核对清单由分卷 5.6 承载；实现完成后按实际结果回填。
9. **无窗口/带窗口测试分界**：纯逻辑（解析/查询/限额/路径/协程）无窗口可测；贴图加载/渲染/窗口上下文为带窗口验证；两类的断言归属在各分卷写清。

## 6. 门禁流程（沿用 AGENTS「里程碑开工门禁」）

1. 整组计划书（综述 + 6 分卷）撰写完成。
2. 交 subagent **只读整组审查**（重点：跨卷一致性、与 AGENTS 权威边界无冲突、每卷「实现者无需再自行决定」）。
3. 停下等待审查结果（不并行开工）。
4. NOT PASS → 按意见改对应卷重送，循环至 PASS。
5. PASS → 向用户展示计划等待明确批准；批准前不写实现代码、不改 CHANGELOG。
6. 用户批准后按分卷 5.6 的步骤表实施（含代码评审、CHANGELOG/AGENTS 更新、commit message 预览与确认）。

## 7. 实施步骤总览（细节与命令在分卷 5.6）

1. 用户批准后：AGENTS.md 追加「已拍板设计决策」记录（步骤 5 只登记决策、不改现状 API 措辞）。
2. 依序实现：工程基线/API（5.1）→ 资产解析与查询（5.2）→ 渲染（5.3）→ 动画/Tween（5.4）→ IPC/watcher（5.5）→ demo/smoke/迁移（5.6）。
3. 每步 Debug/Release 构建 + 无窗口测试；渲染类带窗口验证留到 demo 可用后统一执行。
4. 代码评审（subagent，禁止自检）→ 修复循环至 PASS。
5. CHANGELOG 更新、AGENTS 按实际结果一次回填（含 H 清单 H1–H8 及本里程碑新增项）。
6. 英文 commit message 预览 → 用户确认 → 提交（不推送）。

## 8. 遗留（后续里程碑）

- game-side ECS 或 OOP 玩法移植（移动/回合/战斗/RuleEngine，依赖本里程碑的模型无关 API）。
- autotile/bitmask 渲染、音频/输入等更多 engine 低层模块（每模块保持不规定 game 对象模型）。
- 二进制资产格式（可选）。
