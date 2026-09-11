# trogue 项目概览

> **本文件是项目的唯一权威文档。** 先写文档理清设计，再动代码；每完成一个阶段立即更新本文件对应章节。

## 项目目标

> **交付物是 trogue 引擎本身，不是任何一款游戏。** 成功标准 = 使用者能像用 LÖVE2D 一样，用 trogue 从零写出一款 2D 游戏，而无需自行重造渲染、资产、动画、补间、传输等通用机制——而不是某一款游戏被做得多完整。

**trogue** 是一个基于 raylib 的轻量、通用的 2D 游戏引擎库（C++20），服务三个支撑目标：

1. **schema-first**：资产是固定格式的 JSON（`tro-*`）。Godot 只是可替换的视觉数据生产前端，Agent 也可以直接生成运行时资产；游戏运行时不依赖 Godot。
2. **Agent-first**：Agent 负责把游戏实现、构建、生成场景、导出资产、运行验证和迭代调试串成闭环；人主要负责讨论游戏设计，以及在需要视觉判断时使用 Godot 做标注。
3. **轻量与可扩展**：引擎实现语言为 **C++20**，公共 API 为**纯 C++**（namespace + 不透明类型 + RAII）；运行时只依赖 raylib + nlohmann/json；渲染层薄，未来接入 Live2D/Rive2D 等外部 API 时不与引擎核心耦合。

### 交付物与验证台（2026-09-11 拍板，重新对齐早期目标）

> 本节修正开发过程中的**目标漂移**：早期路线图一度把「移植 trogue-orign 玩法」当成交付物，使 game 层玩法系统挤占了引擎能力的推进主线。现重新对齐——**引擎优先，game 是探针**。

- **引擎（`engine/`）是唯一交付物**：路线图主线 = 按「功能准入判据」逐项补齐引擎的通用能力，而不是把某款游戏做完整。
- **`game/` 是引擎能力的验证台（reference consumer），不是交付物**：
  - 它的意义是「为每项引擎能力提供一个真实、可运行、可 E2E 验证的消费方」——**边做引擎边用它验证**，而不是把某款游戏做完整。
  - game 层已长出的玩法系统（回合、AI、RuleEngine、战斗）是**验证副产品**；只要验证目的达成，它们可被替换或丢弃。
  - 因此「把某款游戏做完整」不是目标；「引擎被验证为像 LÖVE2D 一样可用」才是。
- **判据一致性（路线图自检）**：Roadmap 每一项都应能回答「它补齐/验证了引擎的哪项通用能力」。若某项只能回答「它让某款游戏更好玩」，它就只是 `game/` 的可选内容，不进引擎、不进主线。

### 引擎实现语言决策（2026-09-07 拍板）

- **引擎用 C++20 实现，公共 API 是纯 C++**（不做其它语言绑定，无需 C ABI）：`namespace` 组织、不透明类句柄、值类型快照、RAII 管理资源生命周期；放弃 C11 版本（历史实现降级见「文档有效性与历史实现降级」）。
- **JSON 库改用 nlohmann/json**（替换 jansson）：schema 校验语义（格式/限额/深度/键白名单/路径 grammar）不变，只是实现载体改变；旧 jansson 实现的解析规则作为历史记录保留。
- **通用表现能力引擎内置**：无论使用者的对象模型是 OOP、ECS 还是两者并存，「帧动画播放」「补间 Tween」都是任何游戏需要的通用表现原语，因此由 **engine 内置**，而不是留给每个 game 重复实现。具体边界见「引擎公共 API 边界」与「tro-animations v1」：engine 提供**播放执行原语**（帧采样、fps/loop、补间、时间轴推进、完成/帧事件回调），game 决定**何时触发哪条动画/哪个补间、如何做状态切换**。
- 协程（C++20 coroutine）用于**演出脚本**：采用**自研最小协程原语**（`trogue/coro.hpp`：`tg::task`/`tg::generator`/事件 awaiter，约 300–400 行 header-only，见「编码规范」「依赖」）作为基础，engine 的补间/动画完成可被 `co_await`，便于 game 编排顺序演出；协程是 API 形态，不是 engine 内部的唯一实现方式（数据驱动播放器为主）。**选型记录**：原定 cppcoro，实施前置验证失败——上游 master 停留在 C++17 TS 的 `experimental/coroutine`（GCC 12 起移除），GCC 15 下不可编译；2026-09-07 拍板自研，零第三方协程依赖。具体封装以计划书（`docs/plan-5.1.md` §5.2）与实现为准。
- **引擎内部直接调 raylib C API，不引入 raylib-cpp**（2026-09-07 拍板）：raylib 本就是 C 库，函数薄而清晰；公共 API 不暴露 raylib 类型，RAII 由 `tg::` 资源类自管，无需 OOP 封装层（见「依赖与环境」）。
- 上述语言与通用能力决策与 M5A 合并为一个里程碑（见 Roadmap 与 `docs/plan-5.md`）；该里程碑于 2026-09-07 通过计划审查与用户批准，并已按 `docs/plan-5.md` 落地（见 Roadmap）。

## Agent-first 开发模型与 Godot 职责边界

> 本节是当前项目的架构约束：**Godot 是可选的视觉资产标注/预览工具，不是游戏运行时、玩法编辑器或开发流程的必经步骤。** 人与 Agent 的职能分离优先于对 Godot 功能的完整覆盖。

### 角色分工

| 角色 | 职责边界 |
|------|----------|
| 人 | 讨论游戏目标、规则、体验和美术方向；在机器难以替代视觉判断时，用 Godot 做必要的视觉标注、编排和确认 |
| Agent | 理解设计、决定实现路径、编写引擎与游戏代码、生成/修改场景和测试资产、调用导出器、运行验证、观察 IPC/截图并迭代，直到目标引擎能力被实现并验证（`game/` 为探针，非交付物） |
| Godot | 提供可视化资源整理、TileSet/Terrain 标注、动画帧编排、场景预览和少量 metadata 标注；不承载游戏玩法 |
| scene_exporter | 将 Godot 中实际需要的视觉数据编译为稳定的 `tro-*` 运行时资产；负责校验、资源复制、确定性输出和机器可读诊断 |
| trogue engine/game | 运行游戏；引擎提供低层数据、渲染、查询与**通用表现原语**（帧动画播放器、补间 Tween），游戏层负责 OOP/ECS、输入、状态、AI、战斗、玩法以及「何时播放哪条动画/哪个补间、如何切换」 |

### 权威性与依赖方向

- **游戏设计意图**来自人与 Agent 的讨论；可执行规则的唯一实现位置是 `game/` 与必要的 `engine/` 代码，不从 Godot 节点树或 Godot 脚本推导玩法。
- **运行时资产契约**是 `tro-*` schema 及其 JSON 产物。`assets/` 中的 JSON 是引擎消费的直接输入，也是 Agent 自动化验证的对象。
- **Godot 源文件**（`.tscn`、`.tres`、导入资源）只是可选的上游创作输入，不能成为运行时依赖，也不自动等价于 ECS 实体、组件、系统或状态机。
- **`tro-scene.entities` 是通用 spawn descriptor**：它描述场景中放置对象的初始数据、空间属性和视觉资源，不是 ECS 专属实体定义，也不是 engine 的运行时实体池。engine 只把它作为场景资产中的只读描述暴露给调用方；game 可以把它导入自己的 OOP 对象、ECS 组件，也可以完全忽略它。当前 API（里程碑 5 已实现）通过调用方拥有的 copy-out 快照（`tg::SceneEntity` 值类型）取得 descriptor，不暴露 asset 内部可写指针。
- **统一 spawn descriptor 是已拍板的资产边界**：无论对象最终由 game 的 OOP、ECS 还是其他用户自定义模型接管，都使用同一种 `entities[]` 描述格式；不为 ECS 另造一套场景实体 schema，也不把 ECS 组件名写进 `tro-scene`。
- **`solid` 是通用导入提示，不是 engine 运行时策略**：它表示描述中的初始空间属性，但不等价于添加 ECS `Solid` 组件，也不自动进入 engine 的实体碰撞集合。game 完全决定是否把它导入 OOP 碰撞对象、ECS 组件或忽略；engine 只对 tile 层提供低层静态查询。
- **统一 spawn descriptor 不等于统一运行时模型**：同一描述可由 game 映射到 OOP 或 ECS，但只能由 game 的一个明确所有者维护可变位置与生命周期；engine 不保存第二份运行时实体状态。
- **引擎不规定游戏架构**：`TgWorld`、`TgEntity`、ECS registry、组件、系统和对象生命周期都属于使用者的 `game/`；engine 只提供资源、场景资产、绘制原语、tile 查询、窗口/输入底层和 IPC 传输能力。一个 game 可以选择 OOP、ECS，或两者并存。
- **Agent 可以绕过 Godot**：对于规则明确的场景、测试关卡、随机地牢、出生点和简单资产，优先直接生成或修改 `assets/` 中的 `tro-*` 文件。
- **Agent 也可以无界面调用 Godot**：需要利用 Godot 资源系统时，由 Agent 生成/修改 Godot 源文件并通过 headless 导出；人不必打开编辑器。
- **人只在确有必要时介入**：例如视觉构图、帧归类、地形连接关系或美术取舍无法由 Agent 可靠判断时。完成判断后，Agent 继续负责导出、接入、测试和迭代。

### Agent-first 开发闭环

1. 人描述游戏目标、机制或视觉需求，Agent 澄清约束和验收标准。
2. Agent 判断需求应由代码、直接 JSON、Godot headless 导出，还是一次必要的人工视觉标注完成；不得默认把人或 Godot 放进链路。
3. Agent 实现引擎/游戏代码，或生成最小可运行的场景与测试资产。
4. 需要 Godot 数据时，Agent 调用 `scene_exporter` 导出；不需要时直接使用 `tro-*` JSON。
5. Agent 启动游戏，通过 IPC、日志、结构化查询和截图观察结果，必要时修改代码或资产并热重载。
6. Agent 完成回归验证；只有遇到未决的设计选择或视觉判断时才请求人确认，然后继续完成剩余工作。

### 插件扩展原则

- **需求驱动**：只有实际游戏开发出现视觉数据阻塞时，才扩展 Godot 插件；不追求一开始覆盖 Godot 的全部功能。
- **运行时优先**：先定义游戏层/引擎真正消费的最小数据和稳定 schema，再决定 Godot 提取哪些信息。
- **最小映射**：插件只导出视觉资产、布局和显式标注，不导出玩法逻辑、运行时状态或 Godot 行为语义。
- **Agent 可调用**：新增能力必须优先考虑 headless、确定性、可校验和机器可读错误；编辑器菜单是辅助入口，不是唯一入口。
- **双路径**：能由 Agent 直接生成的内容不应强制经过 Godot；Godot 路径只在它能显著降低视觉标注成本时存在。
- **明确损失**：不支持或有损转换必须 warning/error 说明，不得静默伪造完整兼容。

### 明确不属于 Godot/scene_exporter 的内容

输入、移动、碰撞规则、AI、战斗、回合、ECS 组件与系统、OOP 游戏对象、动画状态切换、相机行为、UI 业务逻辑、音频播放逻辑、存档、网络同步和运行时事件，均由游戏层或引擎运行时负责。Godot/插件可以提供这些系统所需的图片、帧表、布局提示和标注数据，但不实现或决定它们。

> **Tween 边界细化（2026-09-07 拍板）**：上段中「Tween」指的是**玩法决策层**（谁在何时对什么对象起哪些补间、如何衔接），不属于 Godot/插件。Tween 的**执行原语**（推进、插值、缓动、完成回调、协程等待）由 engine 内置提供（见「引擎实现语言决策」）。Godot/插件只提供素材与标注，不生成玩法补间。

### 当前架构结论

- **引擎交付、game 验证**：路线图主线是引擎通用能力的补齐；game 层只在「验证某引擎能力」时演进，本身不是交付物。
- `tro-scene` 可以由 Agent 直接生成；场景搭建不要求人打开 Godot。
- **通用表现原语由 engine 提供**（语言决策已拍板）：帧动画播放器消费 `tro-animations`/实体 `animations` 帧表，Tween 提供数值/位置/颜色的补间；engine 负责**推进与采样**，game 决定**触发、切换、组合**。M4 的「导出但暂不播放」是历史事实，C++ 里程碑起由引擎内置播放器消费（见 Roadmap）。
- C++/RAII 里程碑的目标运行时是 `tg::SceneAsset` 资产对象 + game-owned OOP/ECS；engine 不根据 `type` 判断 player，不保留运行时实体位置，不把 descriptor `solid` 自动加入 engine 碰撞，也不提供实体池。
- scene reload 是 game 的 candidate load/import/swap 流程；engine 只提供一次性 asset load、tile-only query、显式绘制、watcher 通知和通用表现原语。
- `scene_exporter` 按真实开发阻塞逐步扩展；`tro-archetype`、全量 manifest、完整 UI 导出等不因“可能有用”而预先纳入当前范围。
- “不打开 Godot 也能继续开发、生成场景、运行和测试”是 Agent-first 能力的验收标准之一。

## 与 trogue-orign 的关系

- `trogue-orign/` 是**只读参考 + 验证载体**：原 LÖVE2D 回合制 Roguelike（ECS + RuleEngine）。它的用途是**压测引擎**——用一款真实游戏验证 trogue 能否复现 LÖVE2D 级别的开发与运行工作流；**移植它本身不是项目目标**，其玩法系统不进入引擎交付范围。禁止修改该目录。
- 其 Godot 导出插件（`tools/addons/tileset_exporter/tileset_exporter.gd`）产出的 tileset JSON（bitmask、custom_data）是资产管线的上游，对接方式见 [移植路线](#移植路线trogue-origin--trogue)。

## 架构分层

```
┌──────────────────────────────────────────────┐
│      Game / App Layer (game/src/ 使用者代码)    │
│  OOP 对象或 ECS、输入、规则、碰撞策略、相机、HUD │
│  场景 descriptor 导入、运行时状态、IPC 命令语义  │
│  动画/Tween 的触发与状态切换决策               │
│  地形指派与程序生成（噪声/生物群系 = 玩法决策）  │
├──────────────────────────────────────────────┤
│         trogue_engine (engine/ 可复用静态库)    │
│  scene_asset  tro-* 资产解析与只读 descriptor    │
│  tileset     图集资源与 tile 区域                │
│  render      tile 层/显式 sprite 绘制原语        │
│  collision   tile 层查询（调用方选择如何使用）   │
│  animation   帧动画播放器（消费 tro-animations）│
│  tween       数值/位置/颜色补间执行原语          │
│  terrain     autotile 匹配表 TerrainTable/pick_tile│
│  hotreload   文件变化通知（不自行替换游戏状态）   │
│  ipc         JSON-lines 传输/事件推送/callback 分发│
├──────────────────────────────────────────────┤
│      raylib 6.0（窗口/GLFW/OpenGL）+ nlohmann/json│
└──────────────────────────────────────────────┘
```

设计约定：
- **引擎是交付物、game 是验证台（2026-09-11 拍板）**：`engine/` 是唯一交付物，其能力范围由「功能准入判据」逐项裁定；`game/` 是引擎能力的参考消费方与验证台，其玩法代码不构成交付内容（详见「项目目标」）。
- **使用者拥有运行时模型**：引擎公共 API 不定义 `TgWorld`、`TgEntity`、ECS registry、component、system 或对象生命周期；game 可以选择 OOP、ECS，或两者并存。
- **通用表现原语归 engine、触发决策归 game**：engine 的 `animation`/`tween` 模块只负责按数据推进与采样（fps/loop/补间/缓动/回调）；「何时播哪条、何时切换、怎么组合」由 game 决定。这既保证任何对象模型都开箱即用，又不侵犯玩法控制权。
- **功能准入判据（2026-09-11 拍板）**：引擎只收**机制性、确定性、可无头测试**的执行原语（帧采样、补间、bits→tile id 选择、JSON 校验、watcher、IPC）；音频总线/混音、shader 管理、粒子等**美学/玩法决策载体**由 game 直调 raylib 实现（沿「游戏概念不得流入引擎」的反方向流动）。公共 API 薄到能完整装进 Agent 上下文——使用者熟悉 raylib 甚于本引擎 API，能用 raylib 直达的不进引擎。
- **场景资产不是游戏世界**：引擎加载的是不可变/只读的 `tg::SceneAsset` 与 `tg::SceneEntity` 快照，只描述 tilemap、资源和通用 spawn descriptor；运行时对象由 game 自己定义和维护。
- **绘制采用显式输入**：引擎绘制 tile 层和调用方传入的 sprite/变换，不隐式遍历或修改 game 对象；descriptor 的 `type`、`solid` 不触发引擎玩法分支。
- **碰撞是低层查询，不是规则系统**：引擎提供 tile 层查询；是否把 descriptor 或 OOP/ECS 对象纳入碰撞、如何处理动态碰撞，由 game 决定。
- **单线程**：ipc/watcher/tween/animation 的推进在主循环每帧调用，无锁，状态确定性好（AI 调试可预期）。
- **DEBUG no-op**：`TROGUE_DEBUG=OFF` 时 ipc/hotreload 编译为桩，API 形状不变，release 零开销。

### 引擎公共 API 边界（权威）

> **C++/RAII 已实现（2026-09-07 里程碑 5 完成）**：以下边界即当前实现形态；符号名以 `engine/include/trogue/*.hpp` 为准。历史 C API（`tg_*`/`Tg*`）已整体删除，见「文档有效性与历史实现降级」。

- `tg::SceneAsset` 是**不可变/只读资产对象**（RAII 管理生命周期），不是世界容器；它拥有 tile 层、tileset、背景、descriptor 与（若资产内嵌）动画帧表数据。公共头不暴露 tiles 缓冲区、GPU 对象或可写资源指针。
- 层与 tileset 不作为公共类型暴露；实体与层信息通过**值类型快照**（`tg::SceneEntity`、`tg::LayerInfo`）返回。任何返回的内部字符串引用只在 asset 存活期间有效，跨 reload 必须复制。
- `tg::SceneEntity` 是 schema 的通用值快照，不是 `TgEntity`；引擎不得提供按 id 改位置、spawn、despawn 或按 `type` 分支的通用运行时 API。
- tile 查询（`is_solid_at`/`rect_hits_solid`/`tile_at`）只查显式标记 solid 的 tile 层，返回可区分的错误/清除/实体；descriptor 的 `solid` 只作 game 导入提示，不自动加入引擎碰撞集合。
- 渲染：`tg::render_scene` 只绘制 tile 层；sprite/色块由 game 显式调用绘制原语，传入快照/变换/tint。对象排序、相机与 UI 属 game。
- **动画**：`tg::AnimationSet`（只读动画集视图）+ `tg::AnimationPlayer`（播放器）消费实体 `animations`/tro-animations 帧表，提供 play/stop/seek/速度/loop、暂停/恢复与查询（`paused()`）、帧事件与完成回调、`co_await` 完成；**它输出当前帧的视觉描述（贴图/region/offset/tint），不自动 draw、不绑定实体生命周期**。实体 descriptor 的 `animations` 由 asset 解析为可查询的动画集；game 把播放器绑定到自己的对象并决定触发/切换。
- **Tween**：`tg::TweenManager` 提供 float/`Vec2`/`Color` 补间执行原语（`TweenSpec` 时长/缓动/延迟/循环、on_update/on_complete、`wait()` 协程等待）；game 决定补间对象、目标值与触发。engine 不把 Tween 与任何实体或系统耦合。
- **Autotile**：`tg::TerrainTable`（tro-tileset terrain 数据的只读匹配表，`tg::load_terrain_table` 加载）+ `tg::pick_tile`（无状态纯函数：8 方向 pattern → tile id；确定性评分降级 + 同分取最小 id，语义对齐 Godot 评分匹配）。它输出 tile id 供 game 拼装场景；**地形指派、程序生成、动态改图的触发归 game**，engine 不保存地形状态、不做扩散式重排（复刻 AnimationPlayer 边界模式）。
- **内存加载**：`SceneAsset::load_json(text, name)` 与 `load(path)` 同一解析/校验路径（程序生成场景的一等公民入口；name 进错误诊断；tileset/texture 仍按 assets/ 约定读盘，plan-12 §4.4）。
- `tg::Ipc` 不持有 scene/world 指针；只负责 JSON-lines 分帧、响应顺序、包络、**事件通道**（传输层保留命令 `subscribe`/`unsubscribe`/`connections` + `publish`/`disconnect`/`connections()` API，语义见「IPC 协议」）与 game callback。命令语义由 game 注册和实现；事件是纯传输——engine 不识事件名与 filter 键的任何语义。
- `tg::Watcher` 只报告监听目录内安全的 `.json` basename 变化；game 决定何时加载新资产、是否 reconcile、如何保留或删除运行时状态。

> **2026-09-11 新增（里程碑 15）**：
> - **渲染可观测性**：`tg::RenderStats` + `tg::render_stats()`/`render_reset_stats()`（公共只读快照：参数失败/窗口检查/贴图尝试三项计数；供 Agent/调试断言「渲染确实发生」及失败类别）。
> - **离屏渲染**：`tg::render_scene_to_png(asset, w, h, path)`（把 tile 层渲染到离屏 FBO 并导出 PNG；恒等相机，**不含实体/HUD**；需 GL 上下文（隐藏窗口即可）；离屏取像不翻转→内部 `ImageFlipVertical`）。含实体的完整帧截图由 game 自建离屏区间（demo 的 `capture_offscreen_png` 是范例）。
> - **批量 tile 查询**：`tg::tile_grid`（某层一块 tile 值，行主序；层外写 -1）/ `tg::solid_mask`（全部 solid 层可走性合成掩码）；区域以 **tile 坐标**表达（非像素）。
> - **协程推进**：`tg::TaskRunner`（启动/回收 `tg::task<>` 的容器；**不**每帧重 resume 挂起协程——等待由事件同步驱动；析构不隐式 cancel，调用方需显式 `cancel_all()`）。补全了「引擎返回 `task` 却无推进器」的缺口。

> `world.c`/`TgWorld`/`TgEntity`/`tg_*` 等历史 C API 已在 **里程碑 5（2026-09-07）** 整体删除/迁移到 `game/`（见「文档有效性与历史实现降级」），引擎不再拥有这些类型；不得再以兼容名义把它们引回 engine。

### 文档有效性与历史实现降级

- 本节「架构分层」「设计约定」「引擎公共 API 边界」以及里程碑 5 新增的 opaque asset、tile-only query、显式 render、无 world IPC 和 watcher 语义，是当前权威运行时边界。
- 下方阶段 2/3/M4 与里程碑 4（M4）的实现记录、旧代码示例和旧 IPC 命令表是历史兼容资料：它们记录当时已经验证过的实现，不得被解释为当前（里程碑 5）目标 API。
- 历史资料中出现的 `TgWorld`、`TgEntity`、`tg_world_*`、engine 实体池、实体 `solid` 自动参与 engine 碰撞、engine entity y-sort、`type=="player"` reload 分支和 engine-owned `list_entities/spawn/reload/screenshot/quit`，已在里程碑 5 完成删除或迁移到 `game/` 的旧实现。
- 若历史资料与当前架构边界冲突，以当前架构边界和最新通过的里程碑计划为准；实现前必须先把冲突写入文档并解决，不允许用兼容为理由把旧 runtime model 留回 engine。

## 目录结构

```
trogue/
├── AGENTS.md              # 本文件（唯一权威文档）
├── CHANGELOG.md           # 变更记录（Unreleased 格式见开发流程章节）
├── CMakeLists.txt         # 顶层聚合（add_subdirectory engine + game，C++20）
├── engine/                # trogue_engine 库（自包含，可整体取走复用/拆库）
│   ├── CMakeLists.txt     # 依赖查找 + TROGUE_DEBUG option + 库定义
│   ├── include/trogue/    # 目标公共头：trogue.hpp(伞) config.hpp scene.hpp render.hpp
│   │                      #   animation.hpp tween.hpp terrain.hpp hotreload.hpp ipc.hpp（.hpp，C++）
│   └── src/               # 目标：scene_asset.cpp render.cpp animation.cpp tween.cpp terrain.cpp
│                          #   hotreload.cpp ipc.cpp + 私有资源模块
├── game/                  # 游戏层（引擎消费方；游戏概念禁止流入 engine/）
│   ├── CMakeLists.txt     # 可执行 trogue + anim_viewer（输出到 build/bin/）
│   └── src/               # main.cpp + anim_viewer.cpp + 游戏自有模块（引擎能力验证台，非交付物）
├── assets/                # 游戏资产（引擎按 CWD assets/ 约定读取）
│   ├── scenes/            # demo.json（手写示例）+ test.json/tile_map_layer.json（Godot 导出）
│   ├── animations/        # tro-animations v1 独立动画资产（导出产物）
│   ├── tilesets/          # tro-tileset 导出产物
│   └── textures/          # 导出时自动拷贝的贴图
├── editor/                # Godot 4.7 可选视觉标注/预览项目（可由人或 Agent headless 使用）
│   └── addons/scene_exporter/  # 导出插件 v4（菜单 + headless，v2.1 schema + 动画）
├── docs/                  # 里程碑计划书（plan-<M>.md，开工前闭环送审）+ 历史归档 history.md
├── tools/
│   ├── ipc_smoke.py       # IPC 冒烟测试（61 项断言）
│   ├── scene_gen.cpp      # 离线场景生成 CLI（pixellab 管线数据流 C 机制半，复用 pick_tile）
│   └── tests/             # 无窗口单测 + OOP/ECS consumer smoke（CTest）
├── pixellab/              # PixelLab MCP → tro-* 转换层（上游资产管线，与 editor/ 平级；见「PixelLab 资产管线」）
├── template/             # 新游戏项目模板（最小自包含骨架；见「项目模板」）
│   ├── scripts/          # sync_from_source.sh（安装/更新：临时克隆上游 → 铺到目标项目）
│   ├── engine/ pixellab/ editor/ tools/   # 快照（权威源=本仓库，勿在模板内手改）
│   ├── game/             # 起步游戏骨架（内置内存场景；模板自有）
│   └── AGENTS.md README.md CMakeLists.txt # 模板自有
├── build/  build-release/ # 构建产物（gitignore）
├── reference/             # 引擎源码参考副本（gitignore；Godot 4.7.2 + raylib 6.0，查证行为用，见「依赖与环境」）
└── trogue-orign/          # 只读参考（gitignore）
```

> 目录结构已按 **C++ 里程碑（2026-09-07 完成）** 落地：公共头为 `engine/include/trogue/*.hpp`（含 `coro.hpp`）、私有实现 `engine/src/*.cpp`、测试与 consumer smoke 收敛于 `tools/`。历史 C 目录/文件见「历史实现阶段记录」。

## 开发命令

```bash
# Debug 构建（TROGUE_DEBUG 默认 ON，含 IPC + 热重载）
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build

# 运行（必须从项目根目录，资产路径相对 CWD）
./build/bin/trogue [--scene assets/scenes/demo.json] [--port 48764]

# 动画查看器（帧动画触发/切换验证台；独立 IPC 端点 48765，见 IPC 节）
./build/bin/anim_viewer [--scene assets/scenes/soldier_animated_sprite_2d.json] [--port 48765] [--zoom 3]

# Release 构建（IPC/热重载为 no-op 桩）
cmake -B build-release -S . -DCMAKE_BUILD_TYPE=Release -DTROGUE_DEBUG=OFF

# IPC 冒烟测试（先起服再跑）
python3 tools/ipc_smoke.py
```

### 依赖与环境（Gentoo）

> **依赖管理（2026-09-07 拍板）**：第三方库**不 vendored 进仓库**（不使用 `third_party/` 子模块——源码不属于本项目，其提交历史会污染 Git 日志，且本工程不关注上游更新）。依赖来源二选一：
> 1. **系统包管理器**（优先，本环境为 Gentoo emerge）：配好 `find_package` 探测即可；
> 2. **CMake FetchContent**（仅当系统包缺失且无法安装时）：按需拉取、CLI 标准分发，虽不入 Git 历史但列入依赖说明。
>
> **缺依赖处理流程（2026-09-07 拍板）**：Agent **不得自行安装系统依赖**（emerge/apt 需要 sudo 且属环境维护，不是 Agent 职责）。构建/配置时发现缺依赖（`find_package` 失败、链接缺符号、版本不符）→ **立即通知用户**并给出具体缺失项与建议命令；用户安装完成后继续。不要临时安装、不要改系统、不要绕过。

| 依赖 | 版本 | 说明 |
|------|------|------|
| C++ 编译器 | C++20 | 引擎实现语言（需支持 coroutine：GCC 12+/Clang 15+，或同能力编译器） |
| raylib | 6.0 | 渲染/窗口。**坑**：`+system-glfw` 的 raylib 要求系统 glfw 同时含 X11+Wayland 后端，glfw 的 `X` USE flag 必须开启（即使只用 Wayland 会话），否则链接报 `undefined reference to glfwGetX11Window`。**不引入 raylib-cpp**：引擎内部直接调 raylib C API（raylib 本就是 C 库，函数薄而清晰），RAII 由 `tg::` 资源类自管；公共 API 不暴露 raylib 类型 |
| nlohmann/json | 3.11+ | JSON 解析（资产 + IPC）；替换 jansson（C++ 里程碑起） |
| tl::expected | 1.x | 错误载体（`tl/expected.hpp` + CMake 包 `tl-expected` → target `tl::expected`）；`types.hpp` 的 `tg::expected`/`ErrorOr` 别名依赖它，随 `trogue_engine` PUBLIC 传播（2026-09-07 起系统包，原 submodule vendored 方案已废弃） |
| 协程原语 | 自研（`trogue/coro.hpp`，header-only） | `tg::task`/`tg::generator`/事件 awaiter，演出脚本 `co_await`；零第三方协程依赖（选型记录见「引擎实现语言决策」） |
| inotify | 内核 | 热重载文件监听，无额外依赖 |

> **2026-09-07 环境事故记录**：构建曾因 raylib 被 `emerge --depclean` 清理（未加入 world 包集合）而整体失败，同时发现 glfw 缺 `X` USE flag。教训：依赖变化由用户维护（world 集合 + USE flags），Agent 只负责在 CMake 侧给出清晰的探测与报错。修复：用户将 raylib 加入 world 并安装；glfw 需加 `X` 后重编。

> **Godot 行为查证约定（2026-09-09 拍板）**：需要确认 Godot 引擎行为语义（渲染公式、API 行为、编辑器数据模型等）时，**以本地源码为准**：Godot 4.7.2-stable 完整源码已下载至 `reference/godot-4.7.2-stable/`（gitignore，不入仓库；来源 `https://github.com/godotengine/godot/archive/refs/tags/4.7.2-stable.tar.gz`，与 editor/ 实际使用的 Godot 版本一致），直接 grep/阅读实现；官方文档用 browser-mcp 查看 `https://docs.godotengine.org/en/stable/`。不得凭记忆或旧版本资料推断 Godot 语义。

> **raylib 行为查证约定（2026-09-10 拍板）**：raylib 6.0 完整源码已放置于 `reference/raylib/`（gitignore，不入仓库）；需要确认 raylib API 行为/渲染语义时直接 grep/阅读本地源码，不得凭记忆推断。

> **PixelLab MCP 使用约定（2026-09-10 拍板）**：凡调用 PixelLab MCP 工具（素材生成/动画/像素转换等），**必须先查看其文档**：`https://api.pixellab.ai/mcp/docs`，以文档为准确认参数与行为，不得凭记忆猜测。

## 资产规范：tro-scene v2.1

> **当前架构迁移说明（里程碑 5 之前的历史实现已降级）**：本章的 JSON 字段语义仍是资产格式契约；其中涉及 `TgWorld`、`TgEntity`、engine 实体池、实体自动碰撞、engine y-sort 或演示程序命令归属的旧措辞，只描述 M4 及更早的历史实现，不是目标 API。里程碑 5 后的权威运行时边界以本文件「架构分层」和「引擎公共 API 边界」为准：scene entity 是只读 descriptor，`solid` 只是 game 导入提示，实体运行时模型和命令归属由 game 决定。

> v2.1 相对 v2 **只增可选字段，格式 `version` 仍为 2**（向后兼容，v2 资产零迁移）：新增实体 `animations`（动画帧表，C++ 里程碑起由引擎内置 `tg::AnimationPlayer` 消费）、bare 纯实体场景、独立 tro-animations v1 资产、sprite 查找扩展到 AnimatedSprite2D。权威模式定义见下方「字段与语义规则（权威）」。

场景文件放在 `assets/scenes/*.json`。手写示例（palette 模式）见 `assets/scenes/demo.json`；Godot 导出示例（图集模式）见 `assets/scenes/test.json`；动画素材导出产物见下方「tro-animations v1」。

```json
{
  "format": "tro-scene",
  "version": 2,
  "meta":   { "name": "forest", "background": "#101018" },
  "tilemap": {
    "tile_width": 16, "tile_height": 16,
    "tilesets": [
      { "name": "floor",     "path": "tilesets/floor.json" },
      { "name": "obstacles", "path": "tilesets/obstacles.json" }
    ],
    "layers": [
      { "name": "ground", "width": 60, "height": 60, "solid": false,
        "origin": [0, 0], "tileset": "floor", "tiles": [0, 0, -1, ...] }
    ]
  },
  "entities": [
    { "id": "player", "type": "player", "x": 48.0, "y": 48.0, "w": 16, "h": 16,
      "color": "#e94560", "z": 0,
      "sprite": { "texture": "textures/player.png" } },
    { "id": "goblin_1", "type": "goblin", "x": 160.0, "y": 112.0, "w": 16, "h": 16,
      "sprite": { "tileset": "obstacles", "tile": 3 } }
  ]
}
```

### 字段与语义规则（权威）

| 规则 | 说明 |
|------|------|
| 版本 | format 必须为 "tro-scene" 且 version 必须为 2；其他值拒绝载入（v2 为破坏性升级，不读 v1） |
| 坐标系 | 像素，原点 = tilemap 左上角，y 向下；实体 x/y 为**左上角** |
| 三态模式 | ① `tilesets` 存在 → 图集模式；② 有 `palette` 无 `tilesets` → palette 模式（tiles = 调色板索引，语义同 v1）；③ 两者皆无（且 `tilesets` 未写）→ **bare 纯实体场景**，仅当 `layers` 为空 `[]` 或缺省（三个条件同时成立）才合法。图集与 palette **互斥**，同时出现拒绝载入；「无 tilesets/palette 但有层」「有 palette 又有 tilesets」拒绝载入 |
| tilesets | 1..8 项 `{name, path}`，path 相对 assets/；name 场景内唯一；每个 tileset 的 tile 尺寸必须与场景 tile_width/height 一致 |
| 层 tileset | tilesets 非空时每层必填 `tileset`（引用 name）；palette 模式下层不得携带该字段。Godot 一层混用多个贴图组时，导出插件自动拆为多个输出层（首组沿用层名、其余加 `_组序号` 后缀），不再要求一层一贴图 |
| tiles | 行主序一维数组，长度必须 = width×height；`-1`=空；图集模式值域 `[0, 所引 tileset.count)`，palette 模式 `[0, palette_count)` |
| solid 层 | 参与 engine 的 tile-only 查询（C++ API：`tg::is_solid_at` / `tg::rect_hits_solid`，见「引擎公共 API 边界」）；**层矩形之外 = 该层无数据 = 不阻挡**；地图边界由关卡自身绘制的边墙表达 |
| origin | 可选 `[ox, oy]` 像素（可负）：层左上角的世界偏移，Godot 负坐标 cell 由它表达 |
| 实体 id | 必填、场景内唯一；asset loader 遇重复 id 直接拒绝；runtime object 的冲突处理由 game 自己定义，不由 engine 自动追加 `_N` |
| 实体 type | 默认 `"unknown"`；schema 只把它作为不透明的 archetype/spawn 标识，不定义玩法。engine 不读取或分支处理 `type`；演示应用如需 `player` 规则，只能由 `game/` 自己实现 |
| 实体 z | 可选 number（缺省 0，浮点取整）：通用视觉层级提示，作为 descriptor 提供给 game；engine 不自动排序或解释它 |
| 实体 solid | 可选 bool（缺省 false）：通用的初始空间/导入提示；engine tile-only 查询不读取它，也不自动把实体 AABB 加入碰撞。game 接管对象时可自行决定是否导入 OOP/ECS 碰撞组件；仅 `true` 字面量生效，其余值按缺省 false 处理 |
| 实体 sprite | 可选对象，两种形态互斥：图集形态 `{"tileset": name, "tile": id}`（name 必须在场景 tilesets 中；不接受 region/offset，写了被忽略）或独立贴图形态 `{"texture": "textures/x.png", "region": [x,y,w,h]?, "offset": [ox,oy]?}`；region 缺省整图，offset 缺省 `[0,0]`；绘制锚点 = 实体 x/y + offset，贴图按原始像素尺寸绘制（不缩放） |
| 实体 animations | 可选对象（v2.1）：动画帧表 `{"textures": [贴图路径索引表], "animations": [{"name": ..., "fps": N, "loop": bool, "frames": [{"texture": 索引, "region": [x,y,w,h]?, "offset": [ox,oy]?}]}]}`；`textures` 路径相对 assets/ 且去重，帧经索引引用；region 缺省整图，offset 缺省 `[0,0]`；结构同「tro-animations v1」。引擎已消费（C++ 里程碑起由 `tg::AnimationSet`/`tg::AnimationPlayer` 播放，见「引擎公共 API 边界」）；历史 C 阶段曾透传不消费 |
| 实体 props | 可选 object（v1.1 起预留字段）：Godot 侧实体 metadata 的自由透传——导出器把非保留名 metadata 收进该对象，逐实体玩法标注/移植初值的载体；引擎只校验为 object、**忽略不存**（只携带不解释，语义由 game 导入 spawn descriptor 时自行决定）；未来按需消费（扩展 `SceneEntity` 快照），不预做 API。非 object 拒绝载入；实体序列化字节限额照常约束它 |
| color | `#rrggbb` 或 `#rrggbbaa`，缺省白色；有 sprite 时作染色 tint（缺省白 = 原样绘制），无 sprite 时为色块颜色 |
| 渲染顺序 | engine 只保证 tile 层按资产数组序绘制；entity descriptor 不由 engine 自动绘制或排序。game 自己决定显式 sprite 的调用顺序、y-sort、z-sort 和实体色块绘制 |
| 校验 | format/version 不符、tiles 长度或值域不对、tileset 引用不存在、尺寸不一致、三态组合不合法（图集+palette 同现、无 tilesets/palette 但有层）→ 拒绝载入并保留旧场景 |
| 限额 | layers ≤4，tilesets ≤8，palette ≤32，scene descriptors 的数量上限由 engine asset capacity 定义；不定义 runtime entity pool；实体名 63 字节 |

### tro-tileset v2

```json
{
  "format": "tro-tileset", "version": 2,
  "texture": "textures/floor.png",
  "tile_width": 16, "tile_height": 16,
  "columns": 16, "rows": 16,
  "terrain_sets": [ { "mode": "sides", "terrains": [ { "name": "Terrain 0", "color": "#ffdb00" } ] } ],
  "tiles": [
    { "id": 0, "col": 1, "row": 3, "terrain_set": 0, "terrain": 0,
      "peering_bits": { "bottom_side": 0, "right_side": 0 },
      "custom_data": { "Ground": true } }
  ]
}
```

- **`tiles[]` 数组顺序即 tile id**（0..N-1），id 是 tro-scene tiles 引用的稳定键（TileSet 里增删 tile 会导致 id 漂移——重导出场景即可，约定单次编辑会话内 tileset+scene 成对重导）。
- **多格 tile 与原点（只增可选字段，`version` 仍为 2，旧资产零迁移；2026-09-09）**：`tiles[]` 条目可选 `size_in_atlas: [w,h]`（各 ∈ [1,4096]，缺省 `[1,1]`——tile 覆盖的图集格子数，region = `(col*tw, row*th, sw*tw, sh*th)`）、`texture_origin: [x,y]`（int 可负，缺省 `[0,0]`——Godot 纹理原点透传）、`y_sort_origin: y`（int，缺省 `0`——Godot y-sort 排序键偏移透传）。校验：数组长度 2 且元素为 int，origin/sort 绝对值 ≤65536；region 越界不在 load 期校验（与 col/row 同——load 不读纹理文件）。导出插件非缺省才写（单格 tile 零 diff）；margins/separation 非 0 图集不支持，插件 warning（明确损失）。
- **tile 绘制语义（对齐 Godot 4.7.2，绘制位置 dest 左上 = cell 中心 − region.size/2 − texture_origin）**：1×1 且 origin=0 时精确退化为「格子左上角」。多格 tile 逻辑上仍只占一个 cell（solid 查询/tile_at 语义不变）；同层多格 tile 重叠覆盖次序 = 行主序扫描序（Godot 关闭 y-sort 时也不逐 tile 保证次序）。`y_sort_origin` 引擎解析存储、**暂不消费**（无逐 tile y-sort，未来按需消费/暴露公共查询）；实体图集形态 sprite 的 Godot 居中摆放由 game 经 `SpriteDesc.offset` 自行表达。
- texture 单贴图；**一个场景的多张贴图由 tro-scene v2 的 `tilesets` 数组表达**（每贴图一个 tro-tileset JSON）。
- `terrain_sets`：Godot terrain set 透传（`mode`: sides / corners / corners_and_sides）；`peering_bits` 仅导出该 mode 用到的邻位、值 = terrain 序号（未连接的邻位省略）。引擎解析 + 校验并消费：autotile 选择器（`tg::load_terrain_table`/`tg::pick_tile`）按 peering_bits 把地形 pattern 确定性映射为 tile id（plan-12）。
- `custom_data` 透传，引擎忽略。

### tro-animations v1

独立动画资产（`assets/animations/*.json`），结构 = 实体 `animations` 字段（见「字段与语义规则」），供动画素材脱离场景独立复用：

```json
{
  "format": "tro-animations", "version": 1,
  "textures": ["textures/Soldier_Attack01.png", "..."],
  "animations": [
    { "name": "attack01", "fps": 6, "loop": false,
      "frames": [ { "texture": 0, "region": [0, 0, 100, 100] } ] }
  ]
}
```

- 由 scene_exporter v4 从 AnimatedSprite2D 导出（菜单「Export tro-animations...」/ headless `animations=`）；flat 单帧贴图（无动画节点）也可导出为 1 动画 1 帧。
- 当前（C++ 里程碑起）由引擎内置 `tg::AnimationSet`/`tg::AnimationPlayer` 播放器消费（见「引擎公共 API 边界」）；历史 C11 阶段曾透传不消费。

### v1 → v2 迁移（破坏性）

- 手写场景：`version` 改 2；用了 v1.1 tileset 单字段的改写为 `tilesets` 数组 + 层引用；palette 场景仅改 version。
- Godot 导出场景：全部由 scene_exporter v3 重导出，无需手改。
- **v2 → v2.1 零迁移**：v2.1 只增可选字段与 bare 形态，v2 资产原样可读（`version` 仍为 2）。

## PixelLab 资产管线（pixellab/，plan-13）

> **定位**：PixelLab MCP（外部像素美术生成服务）→ tro-* 运行时资产的**上游转换层**，与 `editor/`（Godot 导出管线）平级——都是「上游创作输入 → assets/ 中的 tro-*」。引擎与 game 运行时零 PixelLab 概念；本层是纯离线工具（Python，依赖仅 Pillow + stdlib）+ `tools/scene_gen.cpp`（复用引擎 `pick_tile`）。

- **MCP 调用纪律**：调用任何 PixelLab 工具前先查官方文档 `https://api.pixellab.ai/mcp/docs`；批量生成前 `get_balance`；pro 模式必须走 confirm_cost 报价流程（先报价 → 用户确认 → 再调）。全局 skill `pixellab-mcp`（`~/.agents/skills/`）承载操作指南。
- **数据流**（三条，产物全部落 `assets/`）：
  - **A 角色/动画**：`create_character`/`animate_character` → Agent 保存 `get_character` 响应为元数据 JSON → `python3 pixellab/pxlab.py import-character --meta <json> --name <n> [--fps 8] [--loop walk,idle]` → `assets/textures/pixellab/<n>.png`（spritesheet，每 clip 一行）+ `assets/animations/<n>.json`。fps/loop 为显式参数（PixelLab 不提供）。
  - **B Wang 瓦片集**：`create_topdown_tileset`（16-tile 4×4，standard）→ 保存 metadata JSON（`.../metadata` 端点，含每 tile `corners`+`bounding_box`）→ `pxlab.py import-tileset --meta <json> --image-url <png URL> --lower <名> --upper <名>` → `assets/tilesets/pixellab/<n>.json`（corners mode + peering_bits + 归池 terrain）+ 贴图。
  - **C 地图**：`get_map` ASCII terrain 网格 → `pxlab.py import-map --grid <文件|-> --tileset <tro-tileset> --scene <名> --out <assets 相对路径>` → 顶点采样（`mapping.vertex_corners`）→ 候选池 = **顶点 pattern 的归池**（`mapping.pool_of_vertices`，与 tile 归池同规则；不用格自身 terrain——少数角格会落错池降级）→ `tools/scene_gen`（引擎 `pick_tile` 按该池烤 tile id + `load_json` 回读自检）→ `assets/scenes/<n>.json`（tiles 烤死）。
- **映射三要素（W2 实测锁定，fixture `pixellab/fixtures/`）**：① PixelLab tile `corners{NW,NE,SW,SE}`（字面枚举 lower/upper）↔ 引擎 4 角位（NW→top_left 等）；② 归池 = 多数角（≥3 upper → upper 池，平分归 lower）——16/16 组合精确命中零降级；③ 顶点采样 = 四邻格（含自身）多数投票，平分取 self 优先。单测 `pixellab/tests/`（入 ctest）。
- **确定性**：同输入重跑产物 byte-identical（已验证）；`assets/pixellab_manifest.json` 按 (源类型, 源 id) upsert 记录来源 URL + 产物 sha256；`pxlab.py verify` 复核。下载 URL 可能过期——**产物 + sha256 为权威**，重导入需重新提供 MCP 元数据。
- **明确损失**：25-tile（transition_size=1.0）4×8 Wang 集、tile_size 非 16/32、spritesheet 边 > 4096px、图像尺寸 <8px → 一律拒绝导入并报错，不静默伪造兼容。像素网格检测只做**整数倍放大还原**（检测到 ≥2× 则还原真实网格）；未检测到 = 按原生图接受（无法用块一致性证明非整数倍放大，不做拒绝）——这是检测能力边界，非静默伪造。
- **独立 tro-animations 无运行时加载器**：引擎只消费场景实体**内嵌** `animations`（动画集名 = entity id）；`assets/animations/*.json` 是转换中间产物，消费时把 `textures`/`animations` 两键内嵌进场景实体（同构 `assets/scenes/soldier_animated_sprite_2d.json`）。
- **双路径不变**：规则明确的资产仍直接手写 tro-*；PixelLab 路径只在需要美术生成力时使用。

## 项目模板（template/，plan-14）

> **目的**：让「用 trogue 从零自主开发一个游戏」可复制——`template/` 是一个**最小**自包含项目骨架，复制它即得到能构建、能运行、能被 Agent 迭代的新游戏起点。

- **内容（最小起点，不含框架自用测试）**：`engine/`、`pixellab/`（仅转换脚本）、`editor/`、`tools/scene_gen.cpp` 的 **vendored 快照** + **起步游戏** `game/`（窗口/场景渲染/WASD 移动/热重载/IPC 基础命令）+ 模板自有 `CMakeLists.txt`/`.gitignore`/`README.md`/`AGENTS.md`/`tools/CMakeLists.txt`/`tools/ipc_smoke.py`。
- **不含本仓库的测试套件与 fixture**：`tools/tests/`（引擎单测）与 `pixellab/tests/`、`pixellab/fixtures/` 是本仓库验证 trogue 引擎自用，**不进模板**（游戏项目另建自己的测试）。
- **权威源**：vendored 文件的权威源是**本仓库**；模板内 vendored 文件禁止手改，上游更新后在仓库内重跑 `template/scripts/sync_from_source.sh` 刷新（维护者模式：engine/pixellab/editor 整目录替换 + tools 逐文件；永不触碰模板自有文件）。
- **派生与更新（一份脚本）**：`template/scripts/sync_from_source.sh` 随模板分发给派生项目——在**空目录**运行=新建项目（铺入模板并剥离 `README.md`/引导脚本）；在**已有项目根**运行=更新引擎（从上游临时克隆取快照，只刷新 vendored 集合，保留 `game/`/`assets/`/项目自有文件）。在源仓库的 `template/` 内运行=维护者模式（源仓库根 → 模板快照）。上游 URL/分支可用 `--url`/`--ref` 覆盖（私有库可传带凭证 URL；日志内凭证自动遮盖）；`--source <dir>` 用本地源仓库代替克隆。
- **验证**：空目录运行→独立副本构建零告警、起服 + `tools/ipc_smoke.py` 全过；已有项目运行→`game/`/`README.md` 不被覆盖、engine 被刷新。
- **非目标**：不改 engine 公共 API / tro-* schema；不把 roguelike 玩法或框架测试带入模板；不做参数化脚手架；不自动建 git。
- **遗留**：模板 `AGENTS.md` 的 schema 段落与根 `AGENTS.md` 双份维护（模板顶部已声明权威源）；模板 `tools/CMakeLists.txt`/`ipc_smoke.py` 随上游变化需手工跟进。

## 热重载规范

- **监听**：`assets/scenes/*.json` 的 CLOSE_WRITE/MOVED_TO/CREATE/MODIFY（inotify），150ms 防抖抑制编辑器原子保存连发。非 Linux 为 no-op。
- **当前语义**：watcher 只报告文件名，engine 只提供一次性资产加载（`tg::SceneAsset::load`，历史 C 为 `tg_scene_asset_load`，已删）；game 负责 candidate load/import/swap、按自己的 OOP/ECS policy 保留或删除对象状态。engine 不按 `type` 判断 player，也不决定 ECS 状态。
- **历史过渡语义**：M4 及更早的演示应用曾按 `type=="player"` + id 保留位置；这是已降级的历史实现，不是当前 schema/API 契约。
- **投影约束**：若 game 把对象的 sprite/动画帧显式绘制到 engine，game 对象仍是唯一可变权威；engine asset 只提供 tile 层、视觉资源与表现原语采样，不保存对象投影状态。
- **失败安全**：资产加载失败不修改旧 asset（C++ 用返回 `expected`/空 optional + 日志，历史 C 返回 NULL）；game candidate import/swap 失败也保留旧 asset 与旧 game state。
- 手动触发：F5、watcher、IPC `reload` 的合并、节流和 candidate coordinator 属于 game。

## IPC 协议：tro-ipc v1（仅 DEBUG 构建）

> **版本号说明（消除误读）**：wire 协议版本号**恒为 1**——`hello` 的 `data.version` 与 `ping` 响应的 `version` 均为 `1`，此值为能力探测契约、**只增不改**。下方 `v1.1`/`v1.2` 指**文档修订号**（协议只增内容的记录），**不是** wire 版本、不在线上传输。客户端按 `version:1` 判兼容即可。

> **当前边界（里程碑 5 落地）**：本节命令表最初是历史 demo wire protocol，保留作兼容迁移参考；当前 engine 只提供无 world 的 JSON-lines transport/callback，命令语义、实体快照、截图和退出状态全部由 `game/src/main.cpp` 的 game handler 实现，engine 不再拥有或理解实体命令。

> v1.1 在 v1 基础上**只增不改**：新增观测命令（query_entities/layers/solid_at/get_tile）与实体快照字段（z/solid/sprite/v）；包络、传输、既有命令语义不变，协议版本号仍为 1（老客户端不受影响）。设计目标：非视觉 Agent 不看截图也能摸清实体与地形。

> v1.2 在 v1.1 基础上**只增不改**（2026-09-09，里程碑 7）：新增**事件通道**——传输层保留命令 `subscribe`/`unsubscribe`/`connections`、game 层 `events` 目录命令、engine `publish`/`disconnect`/`connections()` API；包络、传输、既有命令语义不变，协议版本号仍为 1（老客户端不受影响）。设计目标：Agent 免轮询的 inspector 式可观测（语义见「事件通道语义」）。

> **anim_viewer 独立端点（2026-09-10，里程碑 11）**：`game/src/anim_viewer.cpp` 是与 demo 平级的 engine 消费者，自带 `tg::Ipc` 实例监听 **48765**（`--port` 可改），命令语义与上方 demo 命令表无关：`status`（scene/clip/clip_index/frame/paused/playing/zoom/fps）、`anim`（`op`: `toggle_pause`｜`next_clip`｜`play`+`clip`，响应=操作后 status 同构数据）、`screenshot`（`path?`，缺省 `anim_view_<时间戳>.png`）、`quit`、`help`。IPC op 与键鼠（任意键暂停/恢复、左键轮转 clip）**共用同一组动作函数**——E2E 走 IPC 即覆盖触发逻辑本体。

- TCP `127.0.0.1:48764`（`--port` 可改），仅本机可达。
- JSON-lines：每行一个请求对象，每行一个响应。
- 响应包络：成功 `{"ok":true,"data":{...}}`；失败 `{"ok":false,"error":"..."}`。
- 接入问候：`{"ok":true,"event":"hello","data":{"protocol":"tro-ipc","version":1}}`。
- 最多 8 并发连接；单行上限 64KB，超长断开。
- 命令字段直接平铺在请求对象里（非嵌套 args）。

### 命令表

| 命令 | 参数 | data |
|------|------|------|
| `ping` | — | `{pong:true, version}` |
| `subscribe` | `events`（非空字符串数组）；可选 `filter`（object） | `{events:[{event, filter?}]}`（订阅后该连接的**全量订阅集**，对象数组；无 filter 者省略键）。保留名 `hello` 整条拒绝 |
| `unsubscribe` | `events`（字符串数组；空数组 = no-op 回显；按名整体移除含全部 filter 变体） | `{events:[{event, filter?}]}`（回显当前订阅集） |
| `connections` | — | `{connections:[{conn, events:[...]}], count}`（连接快照，含未订阅连接） |
| `help` | — | `{commands:[...]}` |
| `events` | — | `{events:[{name, when, data, filter}]}`（game 事件注册表；plan-9 起 6 事件实表：StateChanged/MoveSucceeded/AbilityUsed/DamageDealt/EntityDied/TurnEnded，可 filter 字段=entity/source/target，逐条目文档化） |
| `status` | — | `{scene, reloads, entities, phase, fps, uptime_s, port}` |
| `list_entities` | — | `{entities:[{id,type,x,y,w,h,color, z?, transform:{visual,moving}, sprite?, hp?:[cur,max], ai?:{state,target}, anim?:{clip,frame}}], count}`（z≠0 才出现；hp/ai 仅战斗原型，plan-9；anim 仅动画集被绑定的实体，plan-10；transform 为 inspector 视图，见下方说明） |
| `get_entity` | `id` | `{entity:{...}}`（字段同 list_entities） |
| `query_entities` | 半径模式 `x` `y` `radius` 必填；或矩形模式 `rect:[x,y,w,h]`（同时提供时**半径模式优先**）；可选 `type` 过滤 | `{entities:[...], count}`；radius 按实体中心距查询点距离 ≤ radius，rect 按 AABB 相交 |
| `set_entity` | `id`，`x?` `y?` `color?` | `{entity:{...}}`（改后快照） |
| `spawn` | `x` `y` 必填；`id?` `type?` `w?` `h?` `color?` | `{entity:{...}}` |
| `despawn` | `id` | `{despawned:true}` |
| `layers` | — | `{layers:[{name,width,height,solid,origin,tileset,tiles}], count}`（tileset=null 表 palette 模式；tiles=非空 tile 数） |
| `solid_at` | `x` `y`（像素） | `{solid}`（仅判定 solid tile 层；实体不参与，见「引擎公共 API 边界」） |
| `get_tile` | `x` `y`（像素） | `{tiles:[{layer,value}]（仅非空格）, solid}` |
| `reload` | — | `{reloaded:true, reloads:N}` |
| `genmap` | `seed` 必填 int；`w`/`h` 可选（缺省 40，∈[1,64]） | `{generated:true, seed, w, h, nonempty, reloads}`（程序生成地图：game 噪声指派 → pick_tile → load_json → swap；同 seed 同尺寸逐位一致，plan-12 §4.5；生成后 watcher/F5/reload 会以 scene_path 覆盖之——预期行为） |
| `screenshot` | `path?`（缺省 `screenshot_<时间戳>.png`） | `{path, ok, w, h, bytes}`；**同步**——响应返回时文件已落盘（game 层离屏 FBO 渲染完整帧，不依赖屏幕缓冲；`--headless` 下同样可用） |
| `log` | `msg` | `{logged:true}`（打印进引擎日志） |
| `quit` | — | `{bye:true}`（引擎退出主循环） |

实体快照字段说明：`z`/`sprite`/`anim` 仅在有意义时出现（z≠0、有贴图、该实体动画集被绑定）；`sprite` 图集形态为 `{tileset:<名字>,tile:<id>}`，独立贴图形态为 `{texture, region?, offset?}`；`transform` 为 inspector 视图（`visual` 当前绘制位置——静止时精确等于逻辑格像素 x/y，`moving` 是否在移动 tween 中，见「数值精度纪律」）；`anim` 为帧动画状态（clip 名 + 当前帧索引，随播放推进，plan-10）。

> **transform 视图（2026-09-09 拍板，inspector 式可观测性）**：实体快照含 `transform: {visual:[vx,vy], moving}`——`visual` 为当前绘制位置（玩家移动中为引擎 tween 插值浮点值，静止时**精确等于**逻辑格像素 x/y；其余实体恒等于逻辑位置），`moving` 为是否在移动动画中。设计目的：非视觉 Agent 凭"静止时 visual==x/y"即可**数值发现**插值残差/截断/逻辑-视觉失步类渲染问题（实测：曾因指数趋近 lerp 残差 + `DrawRectangle(int)` 截断产生恒定错位与抖动，单测与冒烟均不暴露，人眼才发现）。**game 层可扩展**：`game/src/main.cpp` 的 `Demo::extra_entity_fields`（`std::function`）可在快照上追加任意字段——未来 ECS 组件观察（如组件列表/属性）走同一注入点，不修改引擎与 wire 包络。

### 事件通道语义（v1.2，2026-09-09 里程碑 7）

- **事件行**：`{"ok":true,"event":"<名>","data":{...}}`——与 hello 问候同构。**判别式（按顶层键）**：响应永远不含顶层 `event` 键；长连接读行时「有顶层 event 键 = 事件，否则 = 响应」（subscribe 回显元素内的嵌套 `event` 键不是事件行）。
- **订阅模型**：订阅记录 = (事件名, filter) 二元组，按连接存储于 engine 传输层（保留命令，不经 game handler）；同名不同 filter 并存，同对去重幂等。filter 为纯 JSON **顶层字段等值匹配**（多键 AND、`data` 缺键不匹配、数字按数值相等 `1==1.0`、空 object `{}` 恒真）——engine 不理解任何键的语义，「事件 data 携带哪些可过滤字段（如 `entity`，建议字符串 id）」由 game 在 `events` 注册表文档化。
- **publish / 断开**：game 在主线程任意点调 `tg::Ipc::publish(event, data)`（非阻塞直写，永不阻塞主循环）。**断开即订阅清零**：对端关闭、写失败、game 主动 `disconnect(conn)` 三条路径统一收口。慢消费者（订阅了但停止读取）写遇 EAGAIN 即被断开（自愈，无出站队列）。
- **超限语义分叉**：响应行超限 = 换兜底错误行、连接保持；**事件行超限 = 无兜底行、直接断开该事件的全部 filter 匹配订阅者**（发无 event 键的兜底行会破坏判别式）。
- **时序**：publish 在调用瞬间写出；game handler 执行期内发布的事件行**先于**该连接的响应行到达（单连接模式下先读到事件再读到响应，顺序符合直觉）。
- **脚本约定**：
  - 短连接 RPC → 零订阅零干扰（`echo '{"cmd":"turn"}' | nc -q1 127.0.0.1 48764`）；
  - 长连接订阅 → 专脚本持续读行：顶层有 `event` 键 = 事件（hello 同形，未知事件名跳过），否则 = 响应；
  - **单连接端到端模式**（推荐）：同一连接 `subscribe → 发命令 → 循环读行`，先订阅后触发无竞态；
  - 监听脚本必须带 `--duration`/`--count` 出口（防残留连接占满 8 槽）；收窄过滤 = 整名退订再重订更窄 filter。

### 使用示例

```bash
# 手工探索
echo '{"cmd":"status"}' | nc -q1 127.0.0.1 48764

# Python（参考 tools/ipc_smoke.py 的 recv_line 带缓冲行读取——
# 注意 hello 与后续响应可能连包，必须按行切分而不是整块 recv）
python3 tools/ipc_smoke.py
```

**已知坑**（都在开发中实际踩过）：
1. raylib 的 `TakeScreenshot()` 会给路径强拼 CWD 前缀，绝对路径会被破坏；本项目用 `LoadImageFromScreen()` + `ExportImage()` 替代（src/main.c）。
2. 测试脚本必须按行解析 TCP 流（见上）。
3. 遗留引擎进程会占用 IPC 端口导致新实例 bind 失败（日志有提示）；清理用 `pkill -x trogue`（`-f` 模式会误杀自身 shell）。
4. 事件通道断开前，对端最后可能是**残缺行（无 `\n`）+ EOF**（部分写进内核后 EAGAIN 断开，已进内核字节不收回）；读端把残行丢弃、视作断开即可。
5. 订阅连接与 RPC 连接共享 8 槽上限，残留监听器会挤占槽位——监听脚本必须带 duration/count 出口；game 层可用 `connections()`/`disconnect(conn)` 定点清场。

## AI Agent 调试工作流

0. **文件工具纪律（2026-09-09 拍板，多次踩坑教训）**：修改任何文件前必须先用 **read 工具**读它——禁止用 bash（`cat`/`head`/`sed -n` 等）代替读取。edit/write 工具以 read 工具的读取记录为准：未经 read 读过或读后文件已变更（含自己经 bash 改动过），edit/write 一律拒绝（「edit requires reading first」「file changed since it was read」）。已多次因此报错中断，此为硬性前置步骤。
1. 起服：`cmake --build build && (./build/bin/trogue > /tmp/trogue_run.log 2>&1 &)`
2. 冒烟：`python3 tools/ipc_smoke.py`（44 项断言全过为基线；plan-9 起为 61 项）
3. 调试循环：`status`/`list_entities` 观测 → 改 `assets/scenes/*.json` → 0.5s 后 `status.reloads` 自增即为生效 → `screenshot` 拿画面 → `set_entity`/`spawn` 做运行时实验
4. 收尾：`{"cmd":"quit"}` 让引擎干净退出
5. 日志在 stdout（TraceLog 格式），解析失败原因可在其中检索 `[scene]`
6. 美术资产生成走 PixelLab MCP（见「PixelLab 资产管线」）；转换产物用 `python3 pixellab/pxlab.py verify` 校验

> **截图视觉验收（2026-09-10 修订，取代 2026-09-09 的分工版）**：当前对话模型**已支持图片输入**——read 工具可直接读图（PNG/JPEG/WebP 等），视觉验收应由 **Agent 自己读截图并下结论**，不再默认推给用户目测。约定：
> ① read 工具**不接受项目外路径**（如 `/tmp`）与 `build/` 等目录——截图写在这些位置时，先用 terminal 拷进项目内可读路径（用完即删，避免污染仓库）再 read；
> ② 仍建议辅以**数值自证**（Python 像素级比对、IPC 实体快照/transform 视图）——亚像素残差/截断类问题肉眼易漏，机器判定优先（见「数值精度纪律」）；
> ③ 若换回不支持图片输入的模型/工具，回退旧流程：把截图路径与具体核对要点交给用户目测，**不得假装已看图**，也不得就此放弃视觉验收。

## 编码规范

- **C++20**；库命名空间统一 `tg`（`namespace tg { ... }`）；公共符号用 `tg::` 前缀命名空间，文件内私有符号用 `namespace detail` 或匿名命名空间。
- **公共 API 风格总纲（2026-09-07 拍板）**：引擎公共 API **不用 OOP 层级**——无继承、无虚函数、无抽象接口类；**自由函数优先**（查询/渲染/推进/采样等操作走 `tg::` 自由函数，如 `is_solid_at`/`render_scene`），RAII 类只持有资源与生命周期、方法为资源操作薄封装，不承载玩法逻辑；值类型 = 纯数据（public 字段、无 getter/setter 泛滥）；引擎内部直接调 raylib C API、不引入 raylib-cpp（见「引擎实现语言决策」「依赖与环境」）。game 层不受此约束（使用者自选 OOP/ECS）。
- **纯值类型 + 自由函数 + RAII 资源类**：优先值语义（可复制的快照 struct），资源（asset/贴图/ipc/watcher/播放器句柄）用 RAII 类管理，析构自动释放；禁止手动 `new/delete` 泄漏面。
- **异常策略**：公共 API 边界用 `tl::expected`/错误码/`std::optional` 显式表达可预期失败；内部允许 `try/catch` 兜底但不跨 API 抛裸异常；避免异常作为主控制流。
- **协程**：自研最小协程原语 `trogue/coro.hpp` 提供 `tg::task`/`tg::generator`/事件 awaiter（header-only，约 300–400 行，零第三方依赖；选型记录见「引擎实现语言决策」），用于演出脚本与顺序逻辑；数据驱动播放器不依赖协程。封装与语义以计划书（`docs/plan-5.1.md` §5.2）与实现为准。
- 日志统一 `[模块] 消息` 前缀、TraceLog（raylib）或等价输出；用户可见消息中文，注释中文解释"为什么"。
- 内存所有权：RAII 天然表达「asset 析构释放其资源；game 持有对象与播放器实例」；asset 值生命周期结束时所有派生引用/快照须先失效（值语义 + 文档约束）。
- 参教命名清晰避免歧义（宽/高用 `ew/eh/rw/rh` 等）。
- **注释自足纪律（2026-09-11 拍板，反例教训）**：源码与资产/工具代码的注释（含 docstring）**不得引用本仓库内部文档或规划产物**作为依据——禁止出现 `plan-N`/`docs/plan-*.md`/`§x.y` 节号、`里程碑 N`/`门禁 MX`/`M4`-`M9` 阶段名、`AGENTS.md` 指针、`reference/` 查证副本路径、`third_party/` 路径等。原因：这些文件不随代码分发（引擎/工具会被单独取走、vendored 进模板），这类指针对读者是悬空噪音；注释必须自足——直接陈述契约、语义与「为什么」，而不是指向一份外人看不到的文档。设计沿革/阶段记录归文档与 CHANGELOG，不归代码注释。合法的外部溯源（如对齐原版 `trogue-orign/*.lua`、Godot 上游符号名）可以保留。属修正历史遗留问题（旧代码大量携带此类指针），新增与改动代码一律遵守。

> 历史 C11 编码规范（`static`/`tg_` 前缀/纯 struct）只适用于历史实现，见「历史实现阶段记录」。

## 开发流程（沿用 trogue-orign 流程，用户 2025-09-01 拍板）

**里程碑开工门禁（用户 2026-09-07 拍板）**：每个里程碑正式开工前必须走完闭环——

1. ① 写计划书 `docs/plan-<M>.md`（含目的/范围/步骤/验证/遗留；须说明本里程碑**补齐/验证引擎哪项通用能力**，仅让某款游戏更好玩的内容不进主线）
2. ② 交 subagent 审查
3. ③ 停下等待审查结果（不得并行开工）
4. ④ PASS 才开工；不 PASS 则按审查意见修改后重新送审，循环至 PASS

> 门禁与下方步骤 1 的关系：门禁看「计划书经 subagent 审查通过」，步骤 1 的「给出计划等待批准」指用户对计划书的拍板；两者都通过才进入实现（步骤 2）。

1. 给出计划等待批准（项目未正式发布，可大胆提议架构级改动）
2. 实现计划
3. **subagent 检查**未提交代码是否合理、优雅、风格统一、无逻辑问题（禁止自检，自检无效）
4. 检查之后或用户要求时更新 CHANGELOG.md（检查之前禁止修改 CHANGELOG.md）
5. 检查是否需要更新本文件
6. 询问用户是否写 commit message；如需则给出**英文** commit message 预览等待用户确认，**禁止直接提交**
7. 确认后提交**所有**变更（包括非本次变更），然后推送（`git push`，本地 main 追踪远端）

> **远端拓扑（2026-09-10 拍板）**：唯一远端 `origin = https://github.com/Cryptocho/trogue`（原版 Lua 项目的仓库）；本地 main → 远端分支 `trogue-raylib`，两分支**零共同历史、永不 merge**（无意义且需要 `--allow-unrelated-histories`）；远端 main（Lua 原版）永不触碰；本地仓库不含 `trogue-orign/`（gitignore），两分支内容零重叠。remote URL 内嵌 `$GIT_PAT` 凭证（仅存本地 .git/config，勿打印/勿外传）。

- 计划必须含具体步骤（含上述 3~7 步）；重大设计先写入本文件对应章节再实现（文档先行）
- CHANGELOG 与 commit message 不包含阶段编号、AGENTS.md/TODO 等内部文档信息
- 回复用户始终使用中文；禁止 mermaid

> **子代理等待纪律（2026-09-07 拍板，反例教训）**：启动 subagent 时若**下一步动作依赖其结果**，一律阻塞等待，拿到结果再继续；不要开后台 subagent 后反复轮询（`list_agents`/`job_output` 空转、连续重复相同调用）空耗 token。若确实需要后台并行推进独立工作，启动后**继续做有用的独立准备**（只读核对、起草文档等），只在其真正完成的通知到达后收集结果；等待期间禁止重复无意义轮询。

> **不重复造轮子（2026-09-09 拍板，反例教训）**：引擎已提供的通用能力（如 `tg::TweenManager` 的数值/位置/颜色补间、`tg::AnimationPlayer` 动画）**必须直接复用，不得在 game 层手写等价物**。本项目两次踩坑：① `MoveAnim`（手写 0.12s outQuad 移动插值）——后改为引擎 `add_vec2 + Easing::quad_out`（公式 `1-(1-t)²` 与原版 `-t*(t-2)` 完全一致，1:1 对齐手感）；② manual lerp（指数趋近，永不收敛 → 残差）。凡通用表现、算法、数据结构，先检索引擎 `trogue/*.hpp` 与既有代码有无现成实现；有则直接调用。game 层只写「引擎原语之上的决策/组合/编排」。

> **数值精度纪律（2026-09-09 拍板，反例教训）**：涉及像素对齐/网格对齐的插值不得用指数趋近 lerp（float 永不收敛，残差 ~1e-3px 被 `DrawRectangle(int)` 截断 → 恒定错位 + 相机同源放大成帧间抖动）。正确做法：固定时长 tween（引擎 `TweenManager`），播完**精确 snap 到目标整数像素**；绘制用浮点原语（raylib `DrawRectanglePro`），勿经 `DrawRectangle(int)` 截断。本次 bug 教训：纯逻辑单测只覆盖逻辑坐标，发现不了渲染层截断/残差类问题（逻辑坐标全对、测试 8/8 全绿，人眼才发现错位抖动）——表现层可观测性由 IPC 实体快照的 transform 视图承担（见 IPC 命令表标注），Agent 凭"视觉位置 == 逻辑坐标"数值断言即可发现。

## CHANGELOG 格式规范

在 `## [Unreleased]` 下按功能模块组织变更，每个模块使用 `### 功能描述` 标题。

必填字段：`- 影响的文件:` 列出所有变更文件路径（用反引号包裹）。新的修改写在最前面。

常用子标题：`#### Added` / `#### Refactored` / `#### Bug Fixes` / `#### Architecture` / `#### Breaking Changes`

## 移植路线（trogue-origin → trogue）

> **本表是验证映射参考**：把原版概念对应到本引擎，用于压测「引擎能否复现原版工作量」。它**不代表移植是交付物**——映射到的玩法系统属于 `game/`，不进引擎。

| 原项目 (Lua) | 本引擎 (C++) | 备注 |
|--------------|-----------|------|
| TILE_SIZE=16 / SCALE=2 | tile_width/height=16 + 相机 zoom 2 | 对齐 |
| 1-based tile 坐标 | 0-based 像素 | 换算：`px = (tx-1)*16, py = (ty-1)*16` |
| Position/Stats/Actor 组件 | `game/` OOP/ECS；`tg::SceneEntity` 只作为只读 spawn descriptor 快照 | 组件数据仍为纯 struct，由 game 自己导入 |
| Solid 组件 | game 的动态碰撞 + engine 的静态 solid 查询 | `tro-scene.entities[].solid` 是通用初始/静态属性，不等价于 ECS 组件 |
| autotile 4-bit bitmask | tro-tileset `terrain_sets`/`peering_bits` + engine `pick_tile` | 对接点 |
| custom_data `Ground` (bool) | 透传至 tileset `custom_data`（引擎忽略） | solid 语义改由场景分层表达 |
| RuleEngine 事件管线 | game 层最小子集：EventBus + punch→damage→death 管线（冷却/延迟销毁） | 属验证线，见 Roadmap |

## 历史实现（非当前 API）：阶段 2 设计：Godot → tro-scene 资产管线（定稿，原文归档 docs/history.md）

> **注**：本章 v1/v1.1 格式已被 tro-scene/tro-tileset **v2 取代**（权威定义见「资产规范」）；**完整原文归档于 `docs/history.md`**。沿革要点：`editor/`（Godot 4.7 项目）+ scene_exporter 插件（编辑器菜单 + headless runner 双通道，产物直写 `../assets/`）；tro-tileset v1（`tiles[]` 数组顺序即 tile id、terrain_set/custom_data 透传、SceneCollection 场景 tile 跳过）；tro-scene v1.1（可选单 `tilemap.tileset` 引用——有则 tiles=id、无则 palette 索引；实体 `props` 透传；层 `origin`）；Godot 映射 = Node2D metadata 标注（`type` 必填、`w/h/color/solid` 可选，坐标系与 tro-scene 天然一致零换算）；引擎侧新增 tileset 模块（图集渲染 + 最近邻采样），solid 语义修正为「层矩形外 = 无数据 = 不阻挡」。

## 历史实现（非当前 API）：阶段 3 设计：tro-scene/tro-tileset v2 与素材重整（定稿，2025-09-05，原文归档 docs/history.md）

> **注**：v2 权威字段定义已并入「资产规范：tro-scene v2 / tro-tileset v2」，**完整原文归档于 `docs/history.md`**。沿革要点：v2 破坏性升级——多 tileset（`tilesets[]` + 层引用，palette 与图集互斥）、实体 `sprite`（图集/独立贴图双形态）+ `z` + `solid`、tileset v2 增 `terrain_sets`/`peering_bits`；导出插件 v3——多 TileSet 分组导出（一层一贴图约束）、场景 tile（tree.tscn 类）实例化转实体（solid 缺省 true、足印取 RectangleShape2D、贴图 offset 换算）、实体 Sprite2D → sprite（region 命中 tile 矩形 → 图集形态）、peering_bits 按 mode 导出、headless 多场景逗号分隔；引擎 v2——(y, z) 稳定排序复刻 LÖVE 层级、独立贴图路径缓存、solid 实体参与碰撞（里程碑 5 起 C++ 边界已重定）。已知 v2 限制：一层一贴图、tileset/纹理变更不热重载、场景 tile z_index 忽略（metadata `z` 微调）。v2 范围外：迷雾/移动 tween（M6 起游戏层自管）、运行时 autotile、props/custom_data 消费。

## Roadmap

> **两条线（2026-09-11 对齐）**：**引擎能力线**是交付物；**引擎能力验证线**用 `game/` 作探针，其玩法代码非交付物。新里程碑一律先问「它补齐/验证引擎哪项通用能力」——若只能回答「让某款游戏更好玩」，则不进主线。

### 引擎能力线（交付物：`engine/` + 资产管线）

- [x] MVP：world/scene/render + tro-scene v1 + 热重载 + tro-ipc + 冒烟测试
- [x] Godot 导出插件 v2：TileMapLayer → tro-scene 场景导出（含 headless runner）
- [x] 纹理/图集支持：tro-tileset v1 + 图集渲染（palette 双轨兼容）
- [x] tro-scene/tro-tileset v2：多 tileset + 实体 sprite/z/solid + 场景 tile → 实体（导出插件 v3）
- [x] 动画资产与插件统一：tro-scene v2.1（实体 animations + bare 三态）+ tro-animations v1 + 插件 v4（AnimatedSprite2D 导出/纯实体场景/独立动画导出）+ 删 tileset_exporter
- [x] **C++ 引擎里程碑（原 M5A 扩展，2026-09-07 完成）**：C++20/纯 C++ API/RAII + nlohmann+json 替换 + 无 `TgWorld`/实体池边界重构 + 引擎内置帧动画播放器（消费 tro-animations）+ Tween 补间原语 + C++ game demo（计划 `docs/plan-5.md` 已通过审查并落地）
- [x] **IPC 事件通道（2026-09-09 完成）**：engine `tg::Ipc` 新增 subscribe/unsubscribe/connections 传输层保留命令 + publish/disconnect/connections() API + subscribe 可选 filter 顶层等值匹配（单实体观测）；断开即订阅清零、事件超限无兜底直接断开（判别式保护）；game `events` 目录命令（注册表当前为空，不实现任何具体 game 事件）；计划 `docs/plan-7.md` 三轮审查通过并落地
- [x] **autotile 机制与内存加载（2026-09-11 完成）**：engine `TerrainTable`/`pick_tile`（peering_bits 解析校验 + 确定性评分选择器，AnimationPlayer 边界模式：采样归 engine、指派/生成归 game）+ `SceneAsset::load_json` 内存加载 + demo IPC `genmap`（game 噪声指派 → 选择器 → 内存加载 → 渲染；同 seed 像素级一致）（计划 `docs/plan-12.md` 两轮审查通过并落地）
- [x] **帧动画消费（2026-09-10 完成）**：`AnimationSet::name()` 返回所属 entity id + game `Actor::anim_set` 导入绑定 + 绘制循环采样 `current_frame()` 组合 offset + IPC 快照 `anim:{clip,frame}`；E2E 帧序列/像素比对验证（计划 `docs/plan-10.md` 已通过审查并落地）
- [x] **项目模板（template/，2026-09-11 完成）**：最小自包含骨架——vendored 快照（engine/pixellab/editor/tools/scene_gen）+ 起步 game 骨架（内置内存场景）+ 模板自有 `AGENTS.md`（不含本仓库测试套件/fixture）；单份 `sync_from_source.sh` 兼顾安装（空目录运行→铺模板）/更新（项目根运行→临时克隆上游、只刷新 vendored 快照、保留 game/）/维护者模式（template 内运行→刷新快照）；独立副本构建零告警 + 起服/冒烟/截图验证（计划 `docs/plan-14.md`）
- [ ] 二进制资产格式（可选，JSON 为准）

### 引擎能力验证线（探针：`game/`；非交付物）

> 以下里程碑的价值是「用一款真实游戏压测引擎能否复现 LÖVE2D 级别的开发与运行工作流」，其玩法系统本身可被替换或丢弃。

- [x] **移植 trogue-orign 最小闭环（2026-09-09 完成）**：回合制（玩家回合 → 敌方回合 → 回合+1）+ 单格 8 向移动/碰撞（tile solid + 实体互斥 + 斜切切角）+ 敌方静止策略 + 手写 forest 关卡 + IPC `turn`/`move`/`wait` 回合命令 + 无窗口单测（计划 `docs/plan-6.md` 已通过审查并落地）
- [x] **敌人 AI + RuleEngine 最小子集 + 首批事件（2026-09-09 完成）**：game 层 EventBus（tg::Json 载荷，桥接 IPC）+ 三态状态机/视野（chebyshev≤5+Bresenham LOS）/A* 寻路（ALERT_DELAY=1、70% 游走、固定种子）+ punch→damage 管线（冷却/死亡延迟销毁/GameOver 相位）+ 首批 6 对外事件 + hp/ai 快照注入（计划 `docs/plan-9.md` 两轮审查通过并落地）
- [x] **动画查看器（2026-09-10 完成）**：game 层触发/切换首个消费者——独立可执行 `anim_viewer`（任意键暂停/恢复、左键轮转 clip、相机 zoom 3x 观察、自带 IPC 端点 48765）+ 引擎 `AnimationPlayer::paused()` 查询；E2E 轮转/冻结/像素比对全过（计划 `docs/plan-11.md` 已通过审查并落地）

## 历史实现阶段记录（非当前 API）

> **2025-08-31 MVP ~ 2026-09-09 里程碑 7 的完整实现记录归档于 `docs/history.md`**（其描述的 C 时代实现已在里程碑 5 整体删除）；以下为摘要。

- **2025-08-31 MVP**：world/scene/render + tro-scene v1 + 热重载 + tro-ipc 落地；冒烟 15/15、watcher 实测 4 项；修掉 spawn 改名竞态、`TakeScreenshot` 破坏绝对路径（改 LoadImageFromScreen+ExportImage）、热重载位置保留收窄为 `player`。
- **2025-09-01 阶段 2（Godot 资产管线）**：editor/ + scene_exporter v2（菜单 + headless 双通道）；tro-tileset v1 / tro-scene v1.1；引擎 tileset 模块（图集渲染）；端到端 headless 导出 → 引擎加载截图确认；评审修复 world_swap 泄漏、防抖尾沿补触发、spawn 复用空槽等。
- **2025-09-05 阶段 3（v2 素材重整）**：schema v2 破坏性升级（多 tileset / 实体 sprite/z/solid）+ 导出插件 v3（场景 tile 转实体、solid 缺省 true）+ 引擎 v2（(y,z) 稳定排序、独立贴图缓存、solid 实体碰撞）；56 树实体化端到端、valgrind 零泄漏；评审修复 set_error 自重叠 UB 等。
- **2026-09-07 里程碑 4（动画资产与插件统一）**：schema v2.1（实体 animations + bare 三态）+ tro-animations v1 + 插件 v4（AnimatedSprite2D 导出/纯实体场景/独立动画导出）；引擎透传不消费；soldier 7 动画 43 帧逐项一致；冒烟 23/23。

- **2026-09-07 里程碑 5：C++ 引擎迁移（已完整验证；完整记录归档 `docs/history.md`）**：C++20/纯 C++ API/RAII + nlohmann/tl::expected 替换 + 删历史 C API；SceneAsset/tile 查询/渲染/Animation/Tween/Ipc/Watcher 模块落地；game demo + ctest/seam 测试体系；评审修复两轮（含 Tween 回调重入 UB）。

- **2026-09-09 里程碑 6：移植 trogue-orign 最小闭环（已完整验证；完整记录归档 `docs/history.md`）**：game_core 回合状态机 + 单格 8 向移动（地形 solid/实体互斥/斜切切角，对齐原版 canDiagonalMove）+ 静止敌方回合（同步结算）+ forest.json + IPC `turn`/`move`/`wait` 回合命令（dx/dy 严格整数）+ game_core_test 8 用例；评审修复 asset 悬垂/切角误判/dx 非整数截断。

- **2026-09-09 里程碑 7：IPC 事件通道（已完整验证；完整记录归档 `docs/history.md`）**：engine `tg::Ipc` 事件通道（subscribe/unsubscribe/connections + publish/disconnect + 可选 filter 顶层等值匹配；断开即订阅清零、事件超限无兜底直接断开；三轮审查 PASS，范围裁定 engine-only 不实现具体 game 事件）+ game `events` 空注册表；`tools/tests/ipc_test.cpp` 双分支单测 + smoke +12 断言（44/44）。
