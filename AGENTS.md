# trogue 项目概览

> **本文件是项目的唯一权威文档。** 先写文档理清设计，再动代码；每完成一个阶段立即更新本文件对应章节。

## 项目目标

**trogue** 是一个基于 raylib 的轻量 2D 游戏引擎库，服务三个目标：

1. **schema-first**：资产是固定格式的 JSON（`tro-*`）。Godot 只是可替换的视觉数据生产前端，Agent 也可以直接生成运行时资产；游戏运行时不依赖 Godot。
2. **Agent-first**：Agent 负责把游戏实现、构建、生成场景、导出资产、运行验证和迭代调试串成闭环；人主要负责讨论游戏设计，以及在需要视觉判断时使用 Godot 做标注。
3. **轻量与可扩展**：引擎实现语言为 **C++20**，公共 API 为**纯 C++**（namespace + 不透明类型 + RAII）；运行时只依赖 raylib + nlohmann/json；渲染层薄，未来接入 Live2D/Rive2D 等外部 API 时不与引擎核心耦合。

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
| Agent | 理解设计、决定实现路径、编写引擎与游戏代码、生成/修改场景和测试资产、调用导出器、运行验证、观察 IPC/截图并迭代，直到游戏功能完成 |
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

- `tro-scene` 可以由 Agent 直接生成；场景搭建不要求人打开 Godot。
- **通用表现原语由 engine 提供**（语言决策已拍板）：帧动画播放器消费 `tro-animations`/实体 `animations` 帧表，Tween 提供数值/位置/颜色的补间；engine 负责**推进与采样**，game 决定**触发、切换、组合**。M4 的「导出但暂不播放」是历史事实，C++ 里程碑起由引擎内置播放器消费（见 Roadmap）。
- C++/RAII 里程碑的目标运行时是 `tg::SceneAsset` 资产对象 + game-owned OOP/ECS；engine 不根据 `type` 判断 player，不保留运行时实体位置，不把 descriptor `solid` 自动加入 engine 碰撞，也不提供实体池。
- scene reload 是 game 的 candidate load/import/swap 流程；engine 只提供一次性 asset load、tile-only query、显式绘制、watcher 通知和通用表现原语。
- `scene_exporter` 按真实开发阻塞逐步扩展；`tro-archetype`、全量 manifest、完整 UI 导出等不因“可能有用”而预先纳入当前范围。
- “不打开 Godot 也能继续开发、生成场景、运行和测试”是 Agent-first 能力的验收标准之一。

## 与 trogue-orign 的关系

- `trogue-orign/` 是**只读参考**：原 LÖVE2D 回合制 Roguelike（ECS + RuleEngine），后续将其玩法移植到本引擎。禁止修改该目录。
- 其 Godot 导出插件（`tools/addons/tileset_exporter/tileset_exporter.gd`）产出的 tileset JSON（bitmask、custom_data）是资产管线的上游，对接方式见 [移植路线](#移植路线trogue-origin--trogue)。

## 架构分层

```
┌──────────────────────────────────────────────┐
│      Game / App Layer (game/src/ 使用者代码)    │
│  OOP 对象或 ECS、输入、规则、碰撞策略、相机、HUD │
│  场景 descriptor 导入、运行时状态、IPC 命令语义  │
│  动画/Tween 的触发与状态切换决策               │
├──────────────────────────────────────────────┤
│         trogue_engine (engine/ 可复用静态库)    │
│  scene_asset  tro-* 资产解析与只读 descriptor    │
│  tileset     图集资源与 tile 区域                │
│  render      tile 层/显式 sprite 绘制原语        │
│  collision   tile 层查询（调用方选择如何使用）   │
│  animation   帧动画播放器（消费 tro-animations）│
│  tween       数值/位置/颜色补间执行原语          │
│  hotreload   文件变化通知（不自行替换游戏状态）   │
│  ipc         JSON-lines 传输与 callback 分发      │
├──────────────────────────────────────────────┤
│      raylib 6.0（窗口/GLFW/OpenGL）+ nlohmann/json│
└──────────────────────────────────────────────┘
```

设计约定：
- **使用者拥有运行时模型**：引擎公共 API 不定义 `TgWorld`、`TgEntity`、ECS registry、component、system 或对象生命周期；game 可以选择 OOP、ECS，或两者并存。
- **通用表现原语归 engine、触发决策归 game**：engine 的 `animation`/`tween` 模块只负责按数据推进与采样（fps/loop/补间/缓动/回调）；「何时播哪条、何时切换、怎么组合」由 game 决定。这既保证任何对象模型都开箱即用，又不侵犯玩法控制权。
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
- **动画**：`tg::AnimationSet`（只读动画集视图）+ `tg::AnimationPlayer`（播放器）消费实体 `animations`/tro-animations 帧表，提供 play/stop/seek/速度/loop、帧事件与完成回调、`co_await` 完成；**它输出当前帧的视觉描述（贴图/region/offset/tint），不自动 draw、不绑定实体生命周期**。实体 descriptor 的 `animations` 由 asset 解析为可查询的动画集；game 把播放器绑定到自己的对象并决定触发/切换。
- **Tween**：`tg::TweenManager` 提供 float/`Vec2`/`Color` 补间执行原语（`TweenSpec` 时长/缓动/延迟/循环、on_update/on_complete、`wait()` 协程等待）；game 决定补间对象、目标值与触发。engine 不把 Tween 与任何实体或系统耦合。
- `tg::Ipc` 不持有 scene/world 指针；只负责 JSON-lines 分帧、响应顺序、包络与 game callback。命令语义由 game 注册和实现。
- `tg::Watcher` 只报告监听目录内安全的 `.json` basename 变化；game 决定何时加载新资产、是否 reconcile、如何保留或删除运行时状态。

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
│   │                      #   animation.hpp tween.hpp hotreload.hpp ipc.hpp（.hpp，C++）
│   └── src/               # 目标：scene_asset.cpp render.cpp animation.cpp tween.cpp
│                          #   hotreload.cpp ipc.cpp + 私有资源模块
├── game/                  # 游戏层（引擎消费方；游戏概念禁止流入 engine/）
│   ├── CMakeLists.txt     # 可执行 trogue（输出到 build/bin/）
│   └── src/               # main.cpp + 游戏自有模块（将成长为 roguelike 本体）
├── assets/                # 游戏资产（引擎按 CWD assets/ 约定读取）
│   ├── scenes/            # demo.json（手写示例）+ test.json/tile_map_layer.json（Godot 导出）
│   ├── animations/        # tro-animations v1 独立动画资产（导出产物）
│   ├── tilesets/          # tro-tileset 导出产物
│   └── textures/          # 导出时自动拷贝的贴图
├── editor/                # Godot 4.7 可选视觉标注/预览项目（可由人或 Agent headless 使用）
│   └── addons/scene_exporter/  # 导出插件 v4（菜单 + headless，v2.1 schema + 动画）
├── docs/                  # 里程碑计划书（plan-<M>.md，开工前闭环送审，见开发流程）
├── tools/
│   ├── ipc_smoke.py       # IPC 冒烟测试（23 项断言）
│   └── tests/             # 无窗口单测 + OOP/ECS consumer smoke（CTest）
├── build/  build-release/ # 构建产物（gitignore）
└── trogue-orign/          # 只读参考（gitignore）
```

> 目录结构已按 **C++ 里程碑（2026-09-07 完成）** 落地：公共头为 `engine/include/trogue/*.hpp`（含 `coro.hpp`）、私有实现 `engine/src/*.cpp`、测试与 consumer smoke 收敛于 `tools/`。历史 C 目录/文件见「历史实现阶段记录」。

## 开发命令

```bash
# Debug 构建（TROGUE_DEBUG 默认 ON，含 IPC + 热重载）
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build

# 运行（必须从项目根目录，资产路径相对 CWD）
./build/bin/trogue [--scene assets/scenes/demo.json] [--port 48764]

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
| 层 tileset | tilesets 非空时每层必填 `tileset`（引用 name）；palette 模式下层不得携带该字段 |
| tiles | 行主序一维数组，长度必须 = width×height；`-1`=空；图集模式值域 `[0, 所引 tileset.count)`，palette 模式 `[0, palette_count)` |
| solid 层 | 参与 engine 的 tile-only 查询（C++ API：`tg::is_solid_at` / `tg::rect_hits_solid`，见「引擎公共 API 边界」）；**层矩形之外 = 该层无数据 = 不阻挡**；地图边界由关卡自身绘制的边墙表达 |
| origin | 可选 `[ox, oy]` 像素（可负）：层左上角的世界偏移，Godot 负坐标 cell 由它表达 |
| 实体 id | 必填、场景内唯一；asset loader 遇重复 id 直接拒绝；runtime object 的冲突处理由 game 自己定义，不由 engine 自动追加 `_N` |
| 实体 type | 默认 `"unknown"`；schema 只把它作为不透明的 archetype/spawn 标识，不定义玩法。engine 不读取或分支处理 `type`；演示应用如需 `player` 规则，只能由 `game/` 自己实现 |
| 实体 z | 可选 number（缺省 0，浮点取整）：通用视觉层级提示，作为 descriptor 提供给 game；engine 不自动排序或解释它 |
| 实体 solid | 可选 bool（缺省 false）：通用的初始空间/导入提示；engine tile-only 查询不读取它，也不自动把实体 AABB 加入碰撞。game 接管对象时可自行决定是否导入 OOP/ECS 碰撞组件；仅 `true` 字面量生效，其余值按缺省 false 处理 |
| 实体 sprite | 可选对象，两种形态互斥：图集形态 `{"tileset": name, "tile": id}`（name 必须在场景 tilesets 中；不接受 region/offset，写了被忽略）或独立贴图形态 `{"texture": "textures/x.png", "region": [x,y,w,h]?, "offset": [ox,oy]?}`；region 缺省整图，offset 缺省 `[0,0]`；绘制锚点 = 实体 x/y + offset，贴图按原始像素尺寸绘制（不缩放） |
| 实体 animations | 可选对象（v2.1）：动画帧表 `{"textures": [贴图路径索引表], "animations": [{"name": ..., "fps": N, "loop": bool, "frames": [{"texture": 索引, "region": [x,y,w,h]?, "offset": [ox,oy]?}]}]}`；`textures` 路径相对 assets/ 且去重，帧经索引引用；region 缺省整图，offset 缺省 `[0,0]`；结构同「tro-animations v1」。引擎已消费（C++ 里程碑起由 `tg::AnimationSet`/`tg::AnimationPlayer` 播放，见「引擎公共 API 边界」）；历史 C 阶段曾透传不消费 |
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
- texture 单贴图；**一个场景的多张贴图由 tro-scene v2 的 `tilesets` 数组表达**（每贴图一个 tro-tileset JSON）。
- `terrain_sets`：Godot terrain set 透传（`mode`: sides / corners / corners_and_sides）；`peering_bits` 仅导出该 mode 用到的邻位、值 = terrain 序号（未连接的邻位省略）。引擎 v2 忽略，autotile 阶段消费。
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

## 热重载规范

- **监听**：`assets/scenes/*.json` 的 CLOSE_WRITE/MOVED_TO/CREATE/MODIFY（inotify），150ms 防抖抑制编辑器原子保存连发。非 Linux 为 no-op。
- **当前语义**：watcher 只报告文件名，engine 只提供一次性资产加载（`tg::SceneAsset::load`，历史 C 为 `tg_scene_asset_load`，已删）；game 负责 candidate load/import/swap、按自己的 OOP/ECS policy 保留或删除对象状态。engine 不按 `type` 判断 player，也不决定 ECS 状态。
- **历史过渡语义**：M4 及更早的演示应用曾按 `type=="player"` + id 保留位置；这是已降级的历史实现，不是当前 schema/API 契约。
- **投影约束**：若 game 把对象的 sprite/动画帧显式绘制到 engine，game 对象仍是唯一可变权威；engine asset 只提供 tile 层、视觉资源与表现原语采样，不保存对象投影状态。
- **失败安全**：资产加载失败不修改旧 asset（C++ 用返回 `expected`/空 optional + 日志，历史 C 返回 NULL）；game candidate import/swap 失败也保留旧 asset 与旧 game state。
- 手动触发：F5、watcher、IPC `reload` 的合并、节流和 candidate coordinator 属于 game。

## IPC 协议：tro-ipc v1.1（仅 DEBUG 构建）

> **当前边界（里程碑 5 落地）**：本节命令表最初是历史 demo wire protocol，保留作兼容迁移参考；当前 engine 只提供无 world 的 JSON-lines transport/callback，命令语义、实体快照、截图和退出状态全部由 `game/src/main.cpp` 的 game handler 实现，engine 不再拥有或理解实体命令。

> v1.1 在 v1 基础上**只增不改**：新增观测命令（query_entities/layers/solid_at/get_tile）与实体快照字段（z/solid/sprite/v）；包络、传输、既有命令语义不变，协议版本号仍为 1（老客户端不受影响）。设计目标：非视觉 Agent 不看截图也能摸清实体与地形。

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
| `help` | — | `{commands:[...]}` |
| `status` | — | `{scene, reloads, entities, fps, uptime_s, port}` |
| `list_entities` | — | `{entities:[{id,type,x,y,w,h,color, z?, solid?, sprite?, v?:[vx,vy]}], count}` |
| `get_entity` | `id` | `{entity:{...}}`（字段同 list_entities） |
| `query_entities` | 半径模式 `x` `y` `radius` 必填；或矩形模式 `rect:[x,y,w,h]`（同时提供时**半径模式优先**）；可选 `type` 过滤 | `{entities:[...], count}`；radius 按实体中心距查询点距离 ≤ radius，rect 按 AABB 相交 |
| `set_entity` | `id`，`x?` `y?` `color?` | `{entity:{...}}`（改后快照） |
| `spawn` | `x` `y` 必填；`id?` `type?` `w?` `h?` `color?` | `{entity:{...}}` |
| `despawn` | `id` | `{despawned:true}` |
| `layers` | — | `{layers:[{name,width,height,solid,origin,tileset,tiles}], count}`（tileset=null 表 palette 模式；tiles=非空 tile 数） |
| `solid_at` | `x` `y`（像素） | `{solid}`（solid 层与 solid 实体一并判定） |
| `get_tile` | `x` `y`（像素） | `{tiles:[{layer,value}]（仅非空格）, solid}` |
| `reload` | — | `{reloaded:true, reloads:N}` |
| `screenshot` | `path?`（缺省 `screenshot_<时间戳>.png`） | `{path}`；文件在下一帧绘制后写出 |
| `log` | `msg` | `{logged:true}`（打印进引擎日志） |
| `quit` | — | `{bye:true}`（引擎退出主循环） |

实体快照字段说明：`z`/`solid`/`sprite`/`v` 仅在有意义时出现（z≠0、solid=true、有贴图、速度非零）；`sprite` 图集形态为 `{tileset:<名字>,tile:<id>}`，独立贴图形态为 `{texture, region?, offset?}`；`v` 为速度数组 `[vx,vy]`。

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

## AI Agent 调试工作流

1. 起服：`cmake --build build && (./build/bin/trogue > /tmp/trogue_run.log 2>&1 &)`
2. 冒烟：`python3 tools/ipc_smoke.py`（23 项断言全过为基线）
3. 调试循环：`status`/`list_entities` 观测 → 改 `assets/scenes/*.json` → 0.5s 后 `status.reloads` 自增即为生效 → `screenshot` 拿画面 → `set_entity`/`spawn` 做运行时实验
4. 收尾：`{"cmd":"quit"}` 让引擎干净退出
5. 日志在 stdout（TraceLog 格式），解析失败原因可在其中检索 `[scene]`

## 编码规范

- **C++20**；库命名空间统一 `tg`（`namespace tg { ... }`）；公共符号用 `tg::` 前缀命名空间，文件内私有符号用 `namespace detail` 或匿名命名空间。
- **公共 API 风格总纲（2026-09-07 拍板）**：引擎公共 API **不用 OOP 层级**——无继承、无虚函数、无抽象接口类；**自由函数优先**（查询/渲染/推进/采样等操作走 `tg::` 自由函数，如 `is_solid_at`/`render_scene`），RAII 类只持有资源与生命周期、方法为资源操作薄封装，不承载玩法逻辑；值类型 = 纯数据（public 字段、无 getter/setter 泛滥）；引擎内部直接调 raylib C API、不引入 raylib-cpp（见「引擎实现语言决策」「依赖与环境」）。game 层不受此约束（使用者自选 OOP/ECS）。
- **纯值类型 + 自由函数 + RAII 资源类**：优先值语义（可复制的快照 struct），资源（asset/贴图/ipc/watcher/播放器句柄）用 RAII 类管理，析构自动释放；禁止手动 `new/delete` 泄漏面。
- **异常策略**：公共 API 边界用 `tl::expected`/错误码/`std::optional` 显式表达可预期失败；内部允许 `try/catch` 兜底但不跨 API 抛裸异常；避免异常作为主控制流。
- **协程**：自研最小协程原语 `trogue/coro.hpp` 提供 `tg::task`/`tg::generator`/事件 awaiter（header-only，约 300–400 行，零第三方依赖；选型记录见「引擎实现语言决策」），用于演出脚本与顺序逻辑；数据驱动播放器不依赖协程。封装与语义以计划书（`docs/plan-5.1.md` §5.2）与实现为准。
- 日志统一 `[模块] 消息` 前缀、TraceLog（raylib）或等价输出；用户可见消息中文，注释中文解释"为什么"。
- 内存所有权：RAII 天然表达「asset 析构释放其资源；game 持有对象与播放器实例」；asset 值生命周期结束时所有派生引用/快照须先失效（值语义 + 文档约束）。
- 参数命名清晰避免歧义（宽/高用 `ew/eh/rw/rh` 等）。

> 历史 C11 编码规范（`static`/`tg_` 前缀/纯 struct）只适用于历史实现，见「历史实现阶段记录」。

## 开发流程（沿用 trogue-orign 流程，用户 2025-09-01 拍板）

**里程碑开工门禁（用户 2026-09-07 拍板）**：每个里程碑正式开工前必须走完闭环——

1. ① 写计划书 `docs/plan-<M>.md`（含目的/范围/步骤/验证/遗留）
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
7. 确认后提交**所有**变更（包括非本次变更）；当前无 remote，配置远端后提交并推送

- 计划必须含具体步骤（含上述 3~7 步）；重大设计先写入本文件对应章节再实现（文档先行）
- CHANGELOG 与 commit message 不包含阶段编号、AGENTS.md/TODO 等内部文档信息
- 回复用户始终使用中文；禁止 mermaid

> **子代理等待纪律（2026-09-07 拍板，反例教训）**：启动 subagent 时若**下一步动作依赖其结果**，一律用 `run_in_background: false` 阻塞等待，拿到结果再继续；不要开后台 subagent 后反复轮询（`list_agents`/`job_output` 空转、连续重复相同调用）空耗 token。若确实需要后台并行推进独立工作，启动后**继续做有用的独立准备**（只读核对、起草文档等），只在其真正完成的通知到达后收集结果；等待期间禁止重复无意义轮询。

## CHANGELOG 格式规范

在 `## [Unreleased]` 下按功能模块组织变更，每个模块使用 `### 功能描述` 标题。

必填字段：`- 影响的文件:` 列出所有变更文件路径（用反引号包裹）。新的修改写在最前面。

常用子标题：`#### Added` / `#### Refactored` / `#### Bug Fixes` / `#### Architecture` / `#### Breaking Changes`

## 移植路线（trogue-origin → trogue）

| 原项目 (Lua) | 本引擎 (C) | 备注 |
|--------------|-----------|------|
| TILE_SIZE=16 / SCALE=2 | tile_width/height=16 + 相机 zoom 2 | 对齐 |
| 1-based tile 坐标 | 0-based 像素 | 换算：`px = (tx-1)*16, py = (ty-1)*16` |
| Position/Stats/Actor 组件 | `game/` ECS；`TgEntity` 只作为场景描述/渲染投影 | 通过统一 spawn descriptor 与 SceneImporter/RenderBinding 对接，组件数据仍为纯 struct |
| Solid 组件 | game ECS 的动态碰撞 + engine 的静态 solid 查询 | `tro-scene.entities[].solid` 是通用初始/静态属性，不等价于 ECS 组件 |
| autotile 4-bit bitmask | tro-tileset 透传 `terrain_set`/`terrain`（最终格式待 autotile 阶段定义） | 对接点 |
| custom_data `Ground` (bool) | 透传至 tileset `custom_data`（v1 引擎忽略） | solid 语义改由场景分层表达 |
| RuleEngine 事件管线 | 待定（先移植移动/回合最小闭环） | 见 Roadmap |

## 历史实现（非当前 API）：阶段 2 设计：Godot → tro-scene 资产管线（定稿）

> **注（2025-09-05）**：本章 v1/v1.1 格式已被 tro-scene/tro-tileset **v2 取代**（单 tileset 限制、scene tiles 跳过等，见下方「阶段 3 设计」章节）；权威字段定义以「资产规范：tro-scene v2」为准。本章仅作设计沿革保留。

### 目录与数据流

```
editor/  (Godot 4.7 项目, 用户画关卡)
   │  scene_exporter 插件: 编辑器菜单 或 headless CLI
   ▼
assets/scenes/*.json     (tro-scene v1.1, 引擎热重载监听)
assets/tilesets/*.json   (tro-tileset v1)
assets/textures/*.png    (导出时自动从 editor/assets 拷贝)
```

- 导出器以 `ProjectSettings.globalize_path("res://")` 定位并写入 `../assets/`。
- `editor/` 由 `trogue-orign/tools/` 复制而来（保留原 .import/uid 引用），项目名 trogue-editor；原 `tileset_exporter` 插件保留可用。

### tro-tileset v1（新资产类型）

```json
{
  "format": "tro-tileset", "version": 1,
  "texture": "textures/Tile Set.png",
  "tile_width": 16, "tile_height": 16,
  "columns": 5, "rows": 7,
  "tiles": [
    { "id": 0, "col": 1, "row": 3,
      "terrain_set": 0, "terrain": 0, "custom_data": {"Ground": true} }
  ]
}
```

- **`tiles[]` 数组顺序即 tile id**（0..N-1），id 是 tro-scene tiles 引用的稳定键（TileSet 里增删 tile 会导致 id 漂移——重导出场景即可，约定单次编辑会话内 tileset+scene 成对重导）。
- `terrain_set`/`terrain`/`custom_data` 为透传字段，v1 引擎忽略；autotile 所需的 bitmask/peering_bits 最终格式在 autotile 阶段定义。
- `TileSetScenesCollectionSource`（如 tree.tscn）：v1 跳过 + warning，v1.1 映射为实体模板。

### tro-scene v1.1（向后兼容变更）

- `tilemap` 新增可选字段 `"tileset": "tilesets/xxx.json"`（相对 assets/）。
  **有 tileset → tiles 值 = tile id；无 tileset → v1 的 palette 索引语义**（demo.json 零迁移）。渲染器按场景级 tileset 是否存在走图集或色块；v1 限定整个场景共享一个 tileset（多个 TileSet 资源后置）。
- 实体新增可选 `"props": {...}` 自由对象：导出器把实体节点上除 type/w/h/color 外的全部 metadata 收进来；引擎暂不读取（schema 保留），为玩法移植预留。
- 其余字段与语义规则不变。

### Godot → tro-scene 映射规则（权威）

| Godot | tro-scene |
|---|---|
| TileMapLayer 节点 | `tilemap.layers[]` 一项：`name`=节点名；`solid`=节点 metadata `solid`（bool，缺省 false）；tiles = 每个 used cell 的 atlas coords 查 tileset 得 id（未收录 tile → warning + 跳过） |
| 实体节点 = 带 metadata `type` 的 Node2D 派生节点 | `entities[]` 一项：`id`=节点名（须唯一）；`x/y`=global_position（**Godot 与 tro-scene 坐标系天然一致：原点左上、y 向下、cell(0,0)=像素(0,0)，零换算**）；`w/h`=metadata（缺省 tile 尺寸）；`color`=metadata（缺省白）；其余 metadata → `props` |
| TileSetAtlasSource + 贴图 | tro-tileset；贴图文件自动拷到 `../assets/textures/` |
| TileSet custom_data | tileset `custom_data` 透传 |

实体标注规范（给用户的操作指引）：给节点加 metadata（检查器 → Node → Metadata）：`type`（必填，如 "player"/"goblin"）、可选 `w`/`h`/`color`。

### 导出插件 v2（scene_exporter，实现中）

- 编辑器菜单：`Project > Tools > Export tro-tileset...` / `Export tro-scene...`
- headless runner（Agent 自动化通道）：`godot --headless --path editor --script res://addons/scene_exporter/headless_export.gd -- scene=res://assets/test.tscn`
- 产物直接写 `../assets/`，引擎侧即可热重载。

### 引擎侧变更（B1，图集渲染）

- 新模块 `tileset`：`tg_tileset_load(path)`（texture 路径相对 `assets/` 解析，`SetTextureFilter(TEXTURE_FILTER_POINT)` 保持像素风）。
- `TgWorld` 增加场景级 tileset 引用；layer 渲染按 tileset 有无走 `DrawTextureRec` 图集区域或 palette 色块。
- 热重载监听仍仅 `assets/scenes/`（tileset/纹理变更需重启或 IPC reload，已知限制）。

## 历史实现（非当前 API）：阶段 3 设计：tro-scene/tro-tileset v2 与素材重整（定稿，2025-09-05）

背景：用户大批更换素材并重新标注，借此窗口对资产格式做**破坏性升级**（v2 不兼容 v1，v1 资产全部重导出）。目标：消除「实体是色块」「整场景单 tileset」「树被跳过」三个复刻 LÖVE 版画面的最大障碍。

### schema v2 要点（权威定义见「资产规范」章节）

- tro-scene v2：`tilemap.tilesets[]`（多 tileset，名字引用）+ 层级 `tileset` 引用；palette 模式保留且与图集模式互斥；实体新增 `sprite`（图集形态 / 独立贴图形态）与 `z`（渲染排序键）；version 必须 = 2。
- tro-tileset v2：新增 `terrain_sets`（mode + terrains 元数据）与 per-tile `peering_bits`（仅该 mode 用到的邻位）；其余同 v1（tiles[] 顺序即 id、texture 单贴图）。

### 导出器 v3（scene_exporter）

- **多 TileSet**：场景内每个 TileMapLayer 可引用不同 TileSet；每个含图集 tile 的 TileSet 导出为独立 tro-tileset（name = .tres 文件名，重名加 `_N` 后缀），层写 tileset 引用；所有 TileSet 的 tile 尺寸必须一致（v2 约束）。
- **场景 tile → 实体**（TileSetScenesCollectionSource，如 tree.tscn）：实例化模板场景，读第一个 Sprite2D——贴图（AtlasTexture 取其 atlas + region）、`offset`、`centered`；足印 w/h 取模板根 metadata `w`/`h`，否则第一个 RectangleShape2D 的 size，否则 tile 尺寸；`type` 取根 metadata `type`，否则场景文件名。模板根无 `solid` metadata 时**缺省导出为 solid:true**（树类覆盖物需要阻挡；普通实体仍缺省 false），可显式加 metadata `solid: false` 关闭。层内每个场景 tile cell 生成实体 `{id: <type>_N, x/y = cell 左上角, sprite = 独立贴图形态}`；贴图偏移统一换算为「sprite 左上角世界坐标 = 贴图节点全局位置 + Sprite2D.offset − (centered ? 贴图尺寸/2 : 0)」，相对 cell/实体左上角即得 `sprite.offset`（如 tree.tscn：cell 左上 + (tw/2 − rw/2 + 0, th/2 − rh/2 − 38)）。场景 tile 的 Godot `z_index` 忽略——层级由引擎 y-sort 表达，需要微调时给实体节点加 metadata `z`。
- **实体 Sprite2D**：实体节点自身或其子树的第一个 Sprite2D → `sprite` 字段；region 恰好等于某 tileset 的 tile 矩形 → 图集形态，否则独立贴图形态；centered 居中换算同上（相对实体节点位置）；flip 忽略并警告。
- **peering_bits 导出**：按 terrain set 的 mode 决定导出哪些邻位（sides → 4 边；corners → 4 角；corners_and_sides → 8 邻位），值 = terrain 序号，未连接的邻位省略。
- 保留行为：metadata `type` 必填、`w`/`h`/`color`/`solid`/`background` 为保留名、其余 metadata → `props`、空层跳过、alternative tile 忽略 + warning。

### 引擎侧 v2

- `TgWorld` 持有 tilesets 数组（≤8，含 name）；`TgTileLayer` 增加本层 tileset 指针；`TgEntity` 增加 `sprite`（TgSprite：tileset 索引 + tile id 或独立贴图路径 + region + offset）与 `z`。
- 渲染：层按各自 tileset 走图集或 palette 色块；实体有 sprite → 画贴图（tint = color），无 sprite → 色块 + 描边；实体绘制前按 (y, z) 稳定排序；独立贴图经 render 模块的路径缓存懒加载（`tg_render_shutdown()` 统一释放）。
- 热重载监听仍仅 `assets/scenes/`（tileset/纹理变更需重启或 IPC reload，已知限制）。

### v2 范围外（后续阶段）

- 迷雾/视野、移动 tween 动画：游戏层数据 + 表现层，待玩法移植时做。
- 运行时 autotile：静态关卡由 Godot 地形画笔烘好变体，引擎消费 peering_bits 仅在动态改图时需要。
- peering_bits/props/custom_data 引擎消费。

## Roadmap

- [x] MVP：world/scene/render + tro-scene v1 + 热重载 + tro-ipc + 冒烟测试
- [x] Godot 导出插件 v2：TileMapLayer → tro-scene 场景导出（含 headless runner）
- [x] 纹理/图集支持：tro-tileset v1 + 图集渲染（palette 双轨兼容）
- [x] tro-scene/tro-tileset v2：多 tileset + 实体 sprite/z/solid + 场景 tile → 实体（导出插件 v3）
- [x] 动画资产与插件统一：tro-scene v2.1（实体 animations + bare 三态）+ tro-animations v1 + 插件 v4（AnimatedSprite2D 导出/纯实体场景/独立动画导出）+ 删 tileset_exporter
- [x] **C++ 引擎里程碑（原 M5A 扩展，2026-09-07 完成）**：C++20/纯 C++ API/RAII + nlohmann+json 替换 + 无 `TgWorld`/实体池边界重构 + 引擎内置帧动画播放器（消费 tro-animations）+ Tween 补间原语 + C++ game demo（计划 `docs/plan-5.md` 已通过审查并落地）
- [ ] autotile/bitmask 渲染（tileset v2 的 peering_bits 已透传）
- [ ] 移植 trogue-origin：移动+碰撞+回合制最小闭环（InputSystem/MovementSystem/TurnSystem 对应物）+ IPC 回合命令（依赖 C++ 里程碑）
- [ ] RuleEngine 事件管线 C++ 化
- [ ] 二进制资产格式（可选，JSON 为准）

## 历史实现阶段记录（非当前 API）

- **2025-08-31 MVP（已完整验证）**：
  - Debug + Release 双配置构建通过。
  - `tools/ipc_smoke.py` 15/15 通过（hello/ping/status/list_entities/spawn/get_entity/set_entity 移动+颜色/despawn/截图落盘/reload/player 位置保留/quit）。
  - watcher 实测 4 项：① 改坐标+新增实体 → reloads 自增且资产生效；② player 运行时位置保留；③ 还原资产 → goblin 回原位、probe 消失；④ 坏 JSON → 解析失败不计数、不破坏旧场景、引擎继续响应。
  - 开发中修掉的 bug：spawn 唯一性检查早于 active 置位导致全体实体被错误改名 `_2`；raylib `TakeScreenshot` 破坏绝对路径（改用 LoadImageFromScreen+ExportImage）；热重载位置保留语义收窄为仅 `player`（原实现会让编辑器对 NPC 的改动永远不可见）。

- **2025-09-01 阶段 2：Godot → tro-scene 资产管线（已端到端验证）**：
  - `editor/`（Godot 4.7 项目）+ `scene_exporter` 插件：编辑器菜单（Export tro-tileset/tro-scene）与 headless runner 双通道；texture 自动拷贝到 assets/textures/。
  - tro-tileset v1 落地（tiles[] 顺序即 id，bitmask/peering/custom_data 透传）；tro-scene v1.1（可选 `tilemap.tileset` 引用 + 实体 `props` + 层 `origin`），palette 模式零迁移。
  - 引擎新增 tileset 模块（`tg_tileset_load/destroy`、`DrawTextureRec` 图集渲染、最近邻采样）；solid 语义修正为"层外=无数据=不阻挡"。
  - 端到端：`godot --headless` 导出 `test.tscn` → 引擎加载 → 截图确认贴图渲染位置正确；`tile_map_layer.tscn` 同样通过；demo.json 回归 15/15。
  - 已知 v1 限制：scene tiles（tree.tscn）跳过（导出 warning + 引擎留空）；多 TileSet 资源不支持；tileset/纹理变更不触发热重载。
  - 提交前 subagent 评审修复：world_swap 泄漏旧 tileset 及其 GPU 纹理（每次重载累积）；编辑器菜单导出少传 tree_root 参数（headless 测试掩盖）；热重载防抖改为**尾沿补触发**（窗口内连发不丢事件，实测验证）；spawn 复用 despawn 空槽；导出器支持层节点 transform 计入 origin、跳过空层、Color 类型 metadata 规范化；tileset/实体解析严格化；端口参数校验。

- **2025-09-05 阶段 3：tro-scene/tro-tileset v2 与素材重整配套（已端到端验证）**：
  - schema v2 破坏性升级（详见「资产规范」与「阶段 3 设计」）：多 tileset（`tilemap.tilesets` + 层引用，palette 与图集互斥）、实体 `sprite`（图集/独立贴图双形态）、`z`、`solid`；tro-tileset v2 透传 terrain_sets/peering_bits。
  - 导出插件 v3：多 TileSet 分组导出（一层一贴图约束）、场景 tile（tree.tscn 等）自动转实体（solid 缺省 true、足印取 RectangleShape2D）、实体 Sprite2D → sprite、peering_bits、headless 多场景逗号分隔；独立贴图自动拷贝。
  - 引擎 v2：world 持 tilesets 数组 + 层级 tileset 指针；实体贴图渲染（tint=color）；(y, z) 稳定排序复刻 LÖVE「同行树先画」；solid 实体参与碰撞（is_solid_at 与 rect_hits_solid 对称）；独立贴图缓存（失败哨兵防每帧刷日志）+ `tg_render_shutdown`。
  - 端到端：headless 导出 test.tscn → 56 棵树实体化 → 引擎加载 + 截图；连续 6 次 reload 实体稳定；demo.json 回归 15/15；Debug/Release 双构建零警告；评审 subagent 另行 valgrind 验证 3 次热重载零泄漏。
  - 评审修复：set_error 自重叠 UB；实体图集 sprite 引用的未注册 tileset 组未写入 tilemap.tilesets；tg_parse_hex_color 提前清零默认色；z 放宽为 number、solid 仅 true 字面量（语义已入文档）。
  - 已知 v2 限制：一层一贴图（混用需拆 TileMapLayer）；tileset/纹理变更不热重载（重启或重导出）；场景 tile z_index 忽略（用 metadata z 微调）；独立贴图重名会覆盖。

- **2026-09-07 里程碑 4：动画资产与插件统一（已端到端验证）**：
  - 计划书 `docs/plan-4.md` 走完开工门禁（subagent 审查 PASS + 用户批准）后开工。
  - schema v2.1（权威定义见「资产规范」）：实体 `animations` 完整帧表（textures 索引 + name/fps/loop/frames），引擎透传不消费、静态画面靠默认动画首帧 `sprite`；tro-scene 三态模式（图集/palette/bare 纯实体场景）；独立 tro-animations v1 资产。
  - 导出插件 v4：AnimatedSprite2D → sprite 首帧 + animations 全帧表（AtlasTexture 取 atlas+region，fps/loop 透传）；纯实体场景导出（bare：无 tilesets/palette/层）；headless `animations=` 通道 + 菜单「Export tro-animations...」（撤「Export tro-scene...」菜单，headless scene= 保留）；root 单节点素材可直接作实体导出。
  - 引擎：scene.c 三态判定（bare 合法需无 tilesets/palette 且层空/缺失），demo/test 场景零回归。
  - 插件统一：删 `tileset_exporter`，仅启用 `scene_exporter`。
  - 端到端：soldier 导出 tro-animations 7 动画 43 帧逐项一致（attack01/02/03/death/hurt/idle/walk，fps/帧数/region 100×100）；bare 场景引擎加载 + IPC 可见 + 贴图加载成功；冒烟 23/23；Release 构建通过。
  - 评审修复：AGENTS.md 示例路径与 `_texture_rel` 平铺输出一致；SpriteFrames 空帧降级 warning 不炸整个场景导出；headless `animations=` 前缀连写兼容；错误提示覆盖 bare。

- **2026-09-07 里程碑 5：C++ 引擎迁移（已完整验证）**：
  - 计划书 `docs/plan-5.md`（综述）与 `docs/plan-5.1.md`~`docs/plan-5.6.md`（分卷）走完开工门禁（subagent 审查 PASS + 用户批准）后开工；`docs/plan-5.old-c11.md`（C11 版历史计划）作废归档。
  - 引擎整体 C++20 化：公共 API 纯 C++（`namespace tg`、自由函数优先、值类型 + RAII 资源类、无继承/虚函数）；删除全部历史 C11 API（`TgWorld`/`TgEntity`/`tg_world_*` 等与旧 `.c/.h`）；资产格式（`tro-*`）不变，旧场景 JSON 零迁移。
  - 模块落地：`SceneAsset`（只读 RAII 资产 + `tg::expected` 错误）、tile-only 查询（is_solid_at/rect_hits_solid/tile_at）、显式渲染原语（render_scene/render_sprite/draw_rect/shutdown_render）、`AnimationSet`/`AnimationPlayer`（消费实体 animations 帧表）、`TweenManager`（float/Vec2/Color 补间 + wait 协程）、`Ipc`（无 world JSON-lines 传输 + game handler）、`Watcher`（150ms 防抖尾沿）；`TROGUE_DEBUG=OFF` 编译为 API 形状不变的桩。
  - 依赖替换：nlohmann/json（替换 jansson）、tl-expected 系统包（替换 submodule vendored）；第三方库全部走系统包/CMake 探测，删除 `third_party/` 与 `.gitmodules`（用户拍板不 vendored）。
  - C++ game demo（`game/src/main.cpp`）：窗口/相机/渲染、WASD 移动 + 静态碰撞、动画/Tween 示范、热重载（Watcher+F5+IPC reload，candidate load → 帧外 swap）、全部 IPC 命令由 game handler 实现；`tools/ipc_smoke.py` 23 项断言全过。
  - 测试体系：无窗口单测 5 个 + OOP/ECS consumer smoke 2 个（Debug/Release/ASan+UBSan 三配置 ctest 各 7/7）；测试库 seam 白名单精确 5 符号（`render_test_*`×2 + `asset_test_*`×3），生产库零 seam；Debug/Release 双构建零告警（`-Wall -Wextra -Wpedantic`）。
  - 门禁评审修复（两轮）：空帧 clip 播放永不结束/done 挂死、Tween tick 回调再入迭代器失效 UB、tile 查询「极大但有限」坐标 int 转换 UB、draw_rect 忽略 color 恒画白、LayerInfo.nonempty 计数未写入快照。
