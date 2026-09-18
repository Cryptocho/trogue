# trogue 项目概览

## 项目定位与目标

> **交付物是 trogue 引擎本身，不是任何一款游戏。** 成功标准 = 使用者能像用 LÖVE2D 一样，用 trogue 从零写出一款 2D 游戏，而无需自行重造渲染、资产、动画、补间、传输等通用机制——而不是某一款游戏被做得多完整。

**trogue** 是一个基于 raylib 的轻量、通用游戏引擎库（C++20），服务三个支撑目标：

1. **schema-first**：资产是固定格式的 JSON（`tro-*`）。Godot 只是可替换的视觉数据生产前端，Agent 也可以直接生成运行时资产；游戏运行时不依赖 Godot。
2. **Agent-first**：Agent 负责把游戏实现、构建、生成场景、导出资产、运行验证和迭代调试串成闭环；人主要负责讨论游戏设计，以及在需要视觉判断时使用 Godot 做标注。
3. **轻量与可扩展**：引擎公共 API 为**纯 C++**（namespace + 不透明类型 + RAII）；运行时只依赖 raylib + nlohmann/json；渲染层薄，未来接入 Live2D/Rive2D 等外部 API 时不与引擎核心耦合。

### 引擎是交付物，game 是验证台

- **`engine/` 是唯一交付物**：主线 = 按「功能准入判据」逐项补齐引擎的通用能力，而不是把某款游戏做完整。
- **`game/` 是引擎能力的验证台（reference consumer），不是交付物**：它为每项引擎能力提供一个真实、可运行、可 E2E 验证的消费方。其中已长出的玩法系统（回合、AI、RuleEngine、战斗）是**验证副产品**，验证目的达成后可被替换或丢弃。「把某款游戏做完整」不是目标；「引擎被验证为像 LÖVE2D 一样可用」才是。
- **判据一致性（路线图自检）**：Roadmap 每一项都应能回答「它补齐/验证了引擎哪项通用能力」。若某项只能回答「它让某款游戏更好玩」，它就只是 `game/` 的可选内容，不进引擎、不进主线。

### 关键设计决策（结论）

- **引擎 C++20，公共 API 纯 C++**：`namespace` 组织、不透明资源类、值类型快照、RAII 管理生命周期；不做其它语言绑定，无需 C ABI。
- **JSON 用 nlohmann/json，错误用 `tl::expected`**；schema 校验语义（格式/限额/深度/键白名单/路径 grammar）以头文件与实现为准。
- **通用表现原语由引擎内置**：无论使用者的对象模型是 OOP、ECS 还是两者并存，「帧动画播放」「补间 Tween」都是通用原语。engine 提供**播放执行原语**（帧采样、fps/loop、补间、缓动、完成/帧事件回调、协程等待），game 决定**何时触发哪条动画/哪个补间、如何做状态切换**。
- **自研最小协程**（`trogue/coro.hpp`，header-only，零第三方依赖）：`tg::task`/`tg::generator`/事件 awaiter，供演出脚本 `co_await` 补间/动画完成；数据驱动播放器不依赖协程。
- **引擎内部直接调 raylib C API，不引入 raylib-cpp**：raylib 本就是 C 库；公共 API 不暴露 raylib 类型，RAII 由 `tg::` 资源类自管。

## 架构分层与边界

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
│  render      tile 层/显式 sprite 绘制原语 + 离屏 │
│  collision   tile 层查询 + 静态几何 + 运动学步进 │
│  animation   帧动画播放器（消费 tro-animations）│
│  tween       数值/位置/颜色补间执行原语          │
│  terrain     autotile 匹配表 TerrainTable/pick_tile│
│  random      确定性随机（坐标哈希 + 流式 PRNG）  │
│  time        固定步时钟 StepClock（双池/护栏）   │
│  input       虚拟注入通道（步边界消费/防撕裂）   │
│  hotreload   文件变化通知（不自行替换游戏状态）   │
│  ipc         JSON-lines 传输/事件推送/callback 分发│
├──────────────────────────────────────────────┤
│      raylib 6.0（窗口/GLFW/OpenGL）+ nlohmann/json│
└──────────────────────────────────────────────┘
```

### 设计约定（权威）

- **使用者拥有运行时模型**：引擎公共 API 不定义 world、entity、ECS registry、component、system 或对象生命周期；game 可选 OOP、ECS 或两者并存。
- **通用表现原语归 engine、触发决策归 game**：engine 的 `animation`/`tween` 只负责按数据推进与采样（fps/loop/补间/缓动/回调）；「何时播哪条、何时切换、怎么组合」由 game 决定。这既保证任何对象模型都开箱即用，又不侵犯玩法控制权。
- **功能准入判据**：引擎只收**机制性、确定性、可无头测试**的执行原语（帧采样、补间、bits→tile id 选择、JSON 校验、watcher、IPC、静态几何查询、确定性随机、固定步时钟、虚拟输入）。音频总线/混音、shader 管理、粒子等**美学/玩法决策载体**由 game 直调 raylib 实现。公共 API 薄到能完整装进 Agent 上下文——能用 raylib 直达的不进引擎。
- **场景资产不是游戏世界**：引擎加载的是不可变/只读的 `tg::SceneAsset` 与 `tg::SceneEntity` 快照，只描述 tilemap、资源和通用 spawn descriptor；运行时对象由 game 自己定义和维护。
- **绘制采用显式输入**：引擎绘制 tile 层和调用方传入的 sprite/变换，不隐式遍历或修改 game 对象；descriptor 的 `type`、`solid` 不触发引擎玩法分支。
- **碰撞是低层查询，不是规则系统**：引擎提供 tile 层查询与静态地形几何原语；是否把 descriptor 或 OOP/ECS 对象纳入碰撞、如何处理动态碰撞，由 game 决定。
- **单线程**：ipc/watcher/tween/animation 的推进在主循环每帧调用，无锁，状态确定性好（AI 调试可预期）。
- **DEBUG no-op**：`TROGUE_DEBUG=OFF` 时 ipc/hotreload 编译为桩，API 形状不变，release 零开销。平台维度同理：无 inotify 的平台（如 macOS）下 `tg::Watcher` 也是同一套桩语义，`tg::Ipc` 不受影响。

### 引擎公共 API 边界

符号名以 `engine/include/trogue/*.hpp` 为准。

- **资产（`scene.hpp`）**：`tg::SceneAsset` 是**不可变/只读资产对象**（RAII），拥有 tile 层、tileset、背景、descriptor 与（若资产内嵌）动画帧表；公共头不暴露 tiles 缓冲区、GPU 对象或可写指针。层与 tileset 不作为公共类型暴露；实体与层信息通过**值类型快照**（`tg::SceneEntity`、`tg::LayerInfo`）返回，内部字符串引用只在 asset 存活期间有效。`SceneAsset::load_json(text, name)` 与 `load(path)` 同一解析/校验路径（程序生成场景的一等公民入口）。**程序生成场景的类型化写路径**：`tg::SceneSpec`/`tg::SceneLayerSpec`/`TilesetRef`（值类型，schema 构造子集镜像）+ `SceneAsset::create(spec)`（序列化后仍走 `load_json`，校验单一来源）+ `scene_spec_to_json(spec)` 供需要落盘的消费方。实体在 `SceneSpec` 中**原样携带 JSON**而非镜像我读路径快照——`SceneEntity` 不含 `animations`，镜像会静默丢字段。缺省字段按 schema 缺省省略（bare 不写 tile 尺寸，故 bare 资产 `tile_width()==0` 不变）。`tg::Json` 别名亦在本头（`ipc.hpp` 各自声明同名同型别名，避免包含耦合）。受限写入：`update_layer_tiles`（整层）、`set_tile_at`（单格，层局部 tile 坐标）。**solid 层索引查询**：`solid_layer_indices()` 返回值快照，取代「层 1 是 solid」这类散落在 game/tool 里的约定。
- **descriptor**：`tg::SceneEntity` 是 schema 的通用值快照，不是运行时实体；引擎不提供按 id 改位置、spawn、despawn 或按 `type` 分支的运行时 API。`props`（实体）与 `meta_props()`（场景级）为自由透传，引擎只校验形状、不解释任何键。
- **tile 查询**：`is_solid_at`/`rect_hits_solid`/`tile_at` 只查显式标记 solid 的 tile 层，返回可区分的错误/清除/实体；descriptor 的 `solid` 只作 game 导入提示，不自动加入引擎碰撞集合。层矩形之外 = 无数据 = 不阻挡。批量查询：`tile_grid`（某层一块 tile 值，tile 坐标）/ `solid_mask`（全部 solid 层可走性合成掩码）。
- **渲染（`render.hpp`）**：`render_scene` 只绘制 tile 层；sprite/色块由 game 显式调用绘制原语（`render_sprite` 支持 scale/旋转/flip/tint，scale 须为有限正数，翻转只走显式 flip 字段）。`render_scene(asset)` 绘制资产全部 tile；新 overload `render_scene(asset, std::optional<Rect> viewport)` 接受可选世界坐标矩形裁剪每层 tile 范围（按各层 origin/tile_w/tile_h 换算后 floor/ceil + clamp，半开区间），`std::nullopt` = 不裁剪（与无 viewport overload 等价）。**视口裁剪无相机变换**：viewport 是世界坐标，调用方从自己的 Camera2D 自己换算后再传入；engine 不调用 BeginMode2D/EndMode2D、不接收 camera，全部变换由 game 在 `BeginMode2D()...EndMode2D` 区间内设置。`render_scene_to_png` 把 tile 层渲染到离屏 FBO 并导出 PNG（不含实体/HUD；走无 viewport overload）。`RenderStats`/`render_stats()`/`render_reset_stats()` 提供渲染可观测计数（`culled_tiles` 字段仅由视口路径在段② 之前累加，CPU-only，无窗口单测可稳定断言）。`reload_texture(path)` 使进程级独立贴图缓存的失效、下次绘制重读盘（图集贴图随 asset RAII，不在此列）。对象排序、相机与 UI 属 game。
- **碰撞（`collision.hpp`）**：`aabb_overlap`（纯谓词）、`segment_hits_solid`（线段 vs solid 层）、`sweep_move`（轴分离 swept 滑移）、`kinematic_step`（sweep + 探地/探墙/landed 事件派生）、`resolve_overlap`（最小轴脱出）、`DynBox` + `sweep_move_mixed`（静态层 ∪ 动态盒）、`SolidGrid`（渲染色 tile 与碰撞掩码的原子同步写；另有 `create(谓词建格)` 支非资产来源掩码——该形态 `layer_id == -1`，`set_tile` 必然 fail-loud，`refresh` 会整体替换掩码故不得用于它）、单向平台（矩形数组重载，机制性穿越规则）。**探针查询**：`probe_grounded`（视图/one_way/asset 三重载，返回三态 `solid|clear|error`）与 `kinematic_step` 的探地**共用同一实现**——`KinematicEvents::grounded` 只在跑过一步后才有意义，spawn/reset 后要立即判定贴地请用 `probe_grounded`（`is_solid_at` 是点查询，不能替代底边探地矩形）。**三态纪律**：几何查询的 `error`（参数非法）**不等于可通行**，只判 `== solid` 会漏掉它。**速度积分、土狼/缓冲/可变跳高、动态-动态互推解算等手感与规则归 game。**
- **动画（`animation.hpp`）**：`tg::AnimationSet`（只读动画集视图）+ `tg::AnimationPlayer` 消费实体 `animations`/tro-animations 帧表，提供 play/stop/seek/速度/loop、暂停/恢复、帧事件与完成回调、`co_await` 完成；它**输出当前帧的视觉描述（贴图/region/offset/tint），不自动 draw、不绑定实体生命周期**。`tg::AnimationAsset::load/load_json` 消费独立 `tro-animations` v1。
- **Tween（`tween.hpp`）**：`tg::TweenManager` 提供 float/`Vec2`/`Color` 补间执行原语（`TweenSpec` 时长/缓动/延迟/循环、on_update/on_complete、`wait()` 协程等待）；game 决定补间对象、目标值与触发。engine 不把 Tween 与任何实体或系统耦合。**`repeats` 契约（头文件与测试双钉）**：`>0` = 首段之后再重播 N 次（共 1+N 段），段末各发一次 `t=1.0` 采样，`on_complete` 只在最后一段后触发一次；`<0` = 无限，`on_complete` 永不触发（须显式 `cancel`）；`delay` 只在首段前等待一次（重播段从 delay 位置继续），单次 `tick` 至多完成一段。**timer idiom**：值恒定的补间即定时器（`add_float(0,0,spec,忽略采样,on_complete)`），串行演出为「先 `add` 拿 id → `co_await wait(id)`」配 `tg::TaskRunner`；`wait` 等最终完成，对不存在/已取消/已完成的 id 立即完成（顺序反了会静默穿过）。
- **Autotile（`terrain.hpp`）**：`tg::TerrainTable`（tro-tileset terrain 数据的只读匹配表）+ `tg::pick_tile`（无状态纯函数：8 方向 pattern → tile id；确定性评分降级 + 同分取最小 id）。它输出 tile id 供 game 拼装场景；**地形指派、程序生成、动态改图的触发归 game**，engine 不保存地形状态、不做扩散式重排。
- **随机（`random.hpp`）**：`tg::hash_u64`/`hash_combine`（坐标哈希）+ `tg::Random`（xoshiro256** 流式 PRNG）。`Random::draws()` 暴露**原始 `next_u64` 抽取计数**（只读；含拒绝采样/短路的内部消耗），`(seed, draws)` 唯一确定流位置——重放即恢复（不做状态序列化/O(1) 恢复）。算法与常量钉死为可复现契约（同 seed 同调用序列逐位一致，替换算法属破坏性变更）。生成策略归 game。
- **时间/输入（`time.hpp`/`input.hpp`）**：`tg::StepClock` 固定步时钟（授步池/时间池双池、授步永不丢、alpha 余量报告）；`tg::VirtualInput` 确定性按键注入（稳定序、步边界一次性消费、可选防撕裂）。手感参数与玩法策略归 game。
- **协程推进**：`tg::TaskRunner` 启动/回收 `tg::task<>`；不每帧重 resume（等待由事件同步驱动），析构不隐式 cancel。
- **IPC（`ipc.hpp`）**：`tg::Ipc` 不持有 scene/world 指针；只负责 JSON-lines 分帧、响应顺序、包络、**事件通道**与 game callback。命令语义由 game 注册和实现；事件是纯传输——engine 不识事件名与 filter 键的任何语义。
- **热重载（`hotreload.hpp`）**：`tg::Watcher` 只报告监听目录内安全的 `.json` basename 变化；game 决定何时加载新资产、是否 reconcile、如何保留或删除运行时状态。

### 明确不进引擎

动态实体碰撞**规则**、刚体物理/solver、斜坡、圆/胶囊/旋转形状、动态-动态互推的传递解算、寻路（图搜索）、相机与绘制排序、音频总线/混音、shader 管理、粒子、定时器/调度器 API（tween 即 timer）、圆形碰撞谓词（raylib `CheckCollisionCircles` 可无头直用）、全局随机源与概率分布对象、背景层/视差/渐变、地形扩散式重排、旋转碰撞。这些全部留在 `game/`，或由 game 直调 raylib。

## Agent-first 开发模型与 Godot 职责边界

> **Godot 是可选的视觉资产标注/预览工具，不是游戏运行时、玩法编辑器或开发流程的必经步骤。** 人与 Agent 的职能分离优先于对 Godot 功能的完整覆盖。

| 角色 | 职责边界 |
|------|----------|
| 人 | 讨论游戏目标、规则、体验和美术方向；在机器难以替代视觉判断时，用 Godot 做必要的视觉标注、编排和确认 |
| Agent | 理解设计、决定实现路径、编写引擎与游戏代码、生成/修改场景和测试资产、调用导出器、运行验证、观察 IPC/截图并迭代 |
| Godot | 提供可视化资源整理、TileSet/Terrain 标注、动画帧编排、场景预览和少量 metadata 标注；不承载游戏玩法 |
| scene_exporter | 将 Godot 中实际需要的视觉数据编译为稳定的 `tro-*` 运行时资产；负责校验、资源复制、确定性输出和机器可读诊断 |
| engine / game | engine 提供低层数据、渲染、查询与通用表现原语；game 负责 OOP/ECS、输入、状态、AI、战斗、玩法，以及「何时播放哪条动画/哪个补间、如何切换」 |

**权威性与依赖方向**：

- **游戏设计意图**来自人与 Agent 的讨论；可执行规则的唯一实现位置是 `game/` 与必要的 `engine/` 代码，不从 Godot 节点树或 Godot 脚本推导玩法。
- **运行时资产契约**是 `tro-*` schema 及其 JSON 产物。`assets/` 中的 JSON 是引擎消费的直接输入，也是 Agent 自动化验证的对象。
- **Godot 源文件**（`.tscn`、`.tres`、导入资源）只是可选的上游创作输入，不能成为运行时依赖，也不自动等价于 ECS 实体、组件、系统或状态机。
- **`tro-scene.entities` 是通用 spawn descriptor**：描述场景中放置对象的初始数据、空间属性和视觉资源，不是 ECS 专属定义，也不是 engine 的运行时实体池。engine 只把它作为只读描述暴露；game 可导入 OOP 对象、ECS 组件，也可完全忽略。无论最终由哪种模型接管，都用同一种 `entities[]` 描述格式。
- **`solid` 是通用导入提示，不是 engine 运行时策略**：它不等价于添加碰撞组件，也不自动进入 engine 的实体碰撞集合；game 完全决定是否导入、如何导入。
- **engine 不规定游戏架构，也不保存第二份运行时实体状态**：同一 descriptor 可由 game 映射到 OOP 或 ECS，但只能由 game 的一个明确所有者维护可变位置与生命周期。

**明确不属于 Godot/scene_exporter**：输入、移动、碰撞规则、AI、战斗、回合、ECS 组件与系统、OOP 游戏对象、动画状态切换、相机行为、UI 业务逻辑、音频播放逻辑、存档、网络同步和运行时事件。Godot/插件可提供这些系统所需的图片、帧表、布局提示和标注数据，但不实现或决定它们。Tween 的**执行原语**由 engine 提供，玩法补间（谁在何时对什么对象起哪些补间、如何衔接）归 game。

**Agent-first 开发闭环**：

1. 人描述游戏目标、机制或视觉需求，Agent 澄清约束和验收标准。
2. Agent 判断需求应由代码、直接 JSON、Godot headless 导出，还是一次必要的人工视觉标注完成；**不得默认把人或 Godot 放进链路**。
3. Agent 实现引擎/游戏代码，或生成最小可运行的场景与测试资产。
4. 需要 Godot 数据时，Agent 调用 `scene_exporter` 导出；不需要时直接使用 `tro-*` JSON。
5. Agent 启动游戏，通过 IPC、日志、结构化查询和截图观察结果，必要时修改代码或资产并热重载。
6. Agent 完成回归验证；只有遇到未决的设计选择或视觉判断时才请求人确认，然后继续完成剩余工作。

**插件扩展原则**：需求驱动（只有出现视觉数据阻塞才扩展）；运行时优先（先定义 game/engine 真正消费的最小数据与稳定 schema）；最小映射（只导出视觉资产、布局与显式标注）；Agent 可调用（新增能力优先 headless、确定性、可校验、机器可读错误）；双路径（能直接生成的内容不强制经过 Godot）；明确损失（不支持或有损转换必须 warning/error，不得静默伪造兼容）。`tro-archetype`、全量 manifest、完整 UI 导出等不因「可能有用」而预先纳入范围。

> **「不打开 Godot 也能继续开发、生成场景、运行和测试」是 Agent-first 能力的验收标准之一。**

## 与 trogue-orign 的关系

- `trogue-orign/` 是**只读参考 + 验证载体**：原 LÖVE2D 回合制 Roguelike（ECS + RuleEngine）。它的用途是**压测引擎**——用一款真实游戏验证 trogue 能否复现 LÖVE2D 级别的开发与运行工作流；**移植它本身不是项目目标**，其玩法系统不进入引擎交付范围。禁止修改该目录。
- 原版概念到本引擎的对应关系（tile 尺寸/坐标系、组件、Solid、autotile bitmask、RuleEngine 事件管线等）在 `game/` 的实现中体现，属验证线。

## 目录结构

```
trogue/
├── AGENTS.md              # 本文件（唯一权威文档）
├── CHANGELOG.md           # 变更记录（格式见下）
├── CMakeLists.txt         # 顶层聚合（add_subdirectory engine + game，C++20）
├── engine/                # trogue_engine 库（自包含，可整体取走复用/拆库）
│   ├── CMakeLists.txt     # 依赖查找 + TROGUE_DEBUG option + 库定义
│   ├── include/trogue/    # 公共头：trogue.hpp(伞) config.hpp scene.hpp render.hpp
│   │                      #   collision.hpp animation.hpp tween.hpp terrain.hpp
│   │                      #   random.hpp time.hpp input.hpp hotreload.hpp ipc.hpp coro.hpp
│   └── src/               # 私有实现（*.cpp + 私有资源模块）
├── game/                  # 游戏层（引擎消费方；游戏概念禁止流入 engine/）
│   ├── CMakeLists.txt     # 可执行 trogue + anim_viewer（输出到 build/bin/）
│   └── src/               # main.cpp + anim_viewer.cpp + 游戏自有模块（引擎能力验证台，非交付物）
├── assets/                # 游戏资产（引擎按 CWD assets/ 约定读取）
│   ├── scenes/            # 手写示例 + 占位生成场景（tools/scene_gen 产物）
│   ├── animations/        # tro-animations v1 独立动画资产（导出产物）
│   ├── tilesets/          # tro-tileset 产物（PixelLab / Godot 导出 / 占位生成）
│   └── textures/          # 贴图（导出时自动拷贝 / 占位生成）
├── editor/                # Godot 4.7 可选视觉标注/预览项目（可由人或 Agent headless 使用）
│   └── addons/scene_exporter/  # 导出插件 v4（菜单 + headless）
├── docs/                  # 文档与历史归档（history.md 等）；计划书为临时产物，实现后即删
├── pixellab/              # PixelLab MCP → tro-* 转换层（上游资产管线，与 editor/ 平级）
├── template/              # 新游戏项目模板（最小自包含骨架）
│   ├── scripts/          # sync_from_source.sh（安装/更新：临时克隆上游 → 铺到目标项目）
│   ├── engine/ pixellab/ editor/ tools/   # 快照（权威源=本仓库，勿在模板内手改）
│   ├── game/             # 起步游戏骨架（内置内存场景；模板自有）
│   ├── game/examples/    # 两个范式范例（swarm=ECS 风格 / platformer=OOP 风格；可删）
│   └── AGENTS.md README.md CMakeLists.txt # 模板自有
├── tools/
│   ├── ipc_smoke.py       # IPC 冒烟测试
│   ├── placeholder_tileset.py  # 占位瓦片集生成器（已标注 tro-tileset + 贴图）
│   ├── scene_gen.cpp      # 普通 cell-terrain 离线地图生成 CLI（网格 → pick_tile → tro-scene）
│   ├── dual_grid_scene_gen.cpp # PixelLab vertex_grid → dual-grid visual layer
│   └── tests/             # 无窗口单测 + OOP/ECS consumer smoke（CTest）
├── build/  build-release/ # 构建产物（gitignore）
├── reference/             # 引擎源码参考副本（gitignore；Godot 4.7.2 + raylib 6.0，查证行为用）
└── trogue-orign/          # 只读参考（gitignore）
```

## 开发命令

```bash
# Debug 构建（TROGUE_DEBUG 默认 ON，含 IPC + 热重载）
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build

# 运行（必须从项目根目录，资产路径相对 CWD）
./build/bin/trogue [--scene assets/scenes/demo.json] [--port 48764]

# 动画查看器（帧动画触发/切换验证台；独立 IPC 端点 48765）
./build/bin/anim_viewer [--scene assets/scenes/soldier_animated_sprite_2d.json] [--port 48765] [--zoom 3]

# Release 构建（IPC/热重载为 no-op 桩；无 inotify 平台下 hotreload 同样为桩）
cmake -B build-release -S . -DCMAKE_BUILD_TYPE=Release -DTROGUE_DEBUG=OFF

# IPC 冒烟测试（先起服再跑）
python3 tools/ipc_smoke.py
```

### 依赖与环境

> **依赖管理**：第三方库**不 vendored 进仓库**（不使用 `third_party/` 子模块）。依赖来源二选一：① 系统包管理器（优先，本环境为 Gentoo emerge）；② CMake FetchContent（仅当系统包缺失且无法安装时），并在本文件登记。
>
> **缺依赖处理流程**：Agent **不得自行安装系统依赖**（emerge/apt 需要 sudo 且属环境维护，不是 Agent 职责）。构建/配置时发现缺依赖（`find_package` 失败、链接缺符号、版本不符）→ **立即通知用户**并给出具体缺失项与建议命令；用户安装完成后继续。不要临时安装、不要改系统、不要绕过。

| 依赖 | 版本 | 说明 |
|------|------|------|
| C++ 编译器 | C++20 | 需支持 coroutine（GCC 12+/Clang 15+，或同能力编译器） |
| raylib | 6.0 | 渲染/窗口。**坑**：`+system-glfw` 的 raylib 要求系统 glfw 同时含 X11+Wayland 后端，glfw 的 `X` USE flag 必须开启，否则链接报 `undefined reference to glfwGetX11Window` |
| nlohmann/json | 3.11+ | JSON 解析（资产 + IPC） |
| tl::expected | 1.x | 错误载体（`tl/expected.hpp` + CMake 包 `tl-expected` → target `tl::expected`）；`tg::expected`/`ErrorOr` 别名依赖它，随 `trogue_engine` PUBLIC 传播 |
| 协程原语 | 自研 `trogue/coro.hpp` | header-only，零第三方协程依赖 |
| inotify | Linux 内核 | 热重载文件监听，无额外依赖（无 inotify 的平台降级为 no-op 桩） |

> **Godot 行为查证约定**：需要确认 Godot 引擎行为语义时**以本地源码为准**：Godot 4.7.2-stable 完整源码在 `reference/godot-4.7.2-stable/`（gitignore，不入仓库），直接 grep/阅读实现；官方文档用 browser-mcp 查看 `https://docs.godotengine.org/en/stable/`。不得凭记忆或旧版本资料推断。
>
> **raylib 行为查证约定**：raylib 6.0 完整源码在 `reference/raylib/`（gitignore）；确认 API 行为/渲染语义时直接 grep/阅读本地源码，不得凭记忆推断。
>
> **PixelLab MCP 使用约定**：调用任何 PixelLab MCP 工具（素材生成/动画/像素转换等）前，**必须先查看其文档** `https://api.pixellab.ai/mcp/docs`，以文档为准确认参数与行为，不得凭记忆猜测。

## 资产规范：tro-scene / tro-tileset / tro-animations

### tro-scene（`assets/scenes/*.json`）

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
        "origin": [0, 0], "tileset": "floor", "tiles": [0, 0, -1] }
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

**字段与语义规则（权威）**

| 规则 | 说明 |
|------|------|
| 版本 | format 必须为 `"tro-scene"` 且 version 必须为 2；其他值拒绝载入（v2 为破坏性升级，不读 v1） |
| 坐标系 | 像素，原点 = tilemap 左上角，y 向下；实体 x/y 为**左上角** |
| meta | `name`（可选 string）、`background`（可选 `#rrggbb`）、`props`（可选 object，场景级自由透传，经 `SceneAsset::meta_props()` 暴露，引用随 asset 存活）；其余未知键宽容忽略 |
| 三态模式 | ① `tilesets` 存在 → 图集模式；② 有 `palette` 无 `tilesets` → palette 模式（tiles = 调色板索引）；③ 无地形数据 → **bare 纯实体场景**。图集与 palette **互斥**；无地形数据却声明非空层 → 拒绝载入 |
| tilesets | 1..8 项 `{name, path}`，path 相对 assets/；name 场景内唯一；每个 tileset 的 tile 尺寸必须与场景 tile_width/height 一致 |
| 层 tileset | tilesets 非空时每层必填 `tileset`（引用 name）；palette 模式下不得携带。Godot 一层混用多个贴图组时导出插件自动拆层（首组沿用层名，其余加 `_组序号`） |
| tiles | 行主序一维数组，长度必须 = width×height；`-1`=空；图集模式值域 `[0, 所引 tileset.count)`，palette 模式 `[0, palette_count)` |
| solid 层 | 参与 engine 的 tile-only 查询；**层矩形之外 = 该层无数据 = 不阻挡**；地图边界由关卡自身绘制的边墙表达 |
| origin | 可选 `[ox, oy]` 像素（可负）：层左上角的世界偏移 |
| 实体 id | 必填、场景内唯一；loader 遇重复 id 直接拒绝；运行时冲突处理由 game 定义，不由 engine 自动追加 `_N` |
| 实体 type | 默认 `"unknown"`；schema 只把它作为不透明的 archetype/spawn 标识，不定义玩法。engine 不读取或分支处理 `type` |
| 实体 z | 可选 number（缺省 0，浮点取整）：通用视觉层级提示；engine 不自动排序或解释 |
| 实体 rotation | 可选 number（缺省 0，度，须有限）：spawn 朝向提示，纯数据透传；不改变 AABB 语义，不参与引擎碰撞/查询 |
| 实体 solid | 可选 bool（缺省 false）：通用的初始空间/导入提示；engine tile-only 查询不读取它，也不自动加入碰撞。仅 `true` 字面量生效 |
| 实体 sprite | 可选对象，两形态互斥：图集形态 `{"tileset": name, "tile": id, "offset"?: [ox,oy], "flip_x"?: bool, "flip_y"?: bool}`（name 须在场景 tilesets 中；**region 不接受**）或独立贴图形态 `{"texture": "textures/x.png", "region"?: [x,y,w,h], "offset"?: [ox,oy], "flip_x"?: bool, "flip_y"?: bool}`。`offset` 缺省 `[0,0]`，绘制锚点 = 实体 x/y + offset；`flip_x/flip_y` 缺省 false。贴图按原始像素尺寸绘制 |
| 实体 animations | 可选对象：动画帧表 `{"textures": [...], "animations": [{"name":..., "fps": N, "loop": bool, "frames": [{"texture": 索引, "region"?: [...], "offset"?: [...]}]}]}`；`textures` 相对 assets/ 且去重，帧经索引引用。结构同 tro-animations v1 |
| 实体 props | 可选 object（非 object 拒绝载入）：实体 metadata 自由透传，经 `SceneEntity::props` 值拷贝暴露；引擎只校验、不解释任何键 |
| color | `#rrggbb` 或 `#rrggbbaa`，缺省白色；有 sprite 时作染色 tint，无 sprite 时为色块颜色 |
| 渲染顺序 | engine 只保证 tile 层按资产数组序绘制；entity descriptor 不由 engine 自动绘制或排序。调用顺序、y-sort、z-sort 归 game |
| 校验 | format/version 不符、tiles 长度或值域不对、tileset 引用不存在、尺寸不一致、三态组合不合法 → 拒绝载入并保留旧场景 |
| 限额 | layers ≤4，tilesets ≤8，palette ≤32；实体名 63 字节 |

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

- **`tiles[]` 数组顺序即 tile id**（0..N-1），id 是 tro-scene tiles 引用的稳定键。TileSet 里增删 tile 会导致 id 漂移——重导出场景即可；约定单次编辑会话内 tileset + scene 成对重导。
- **多格 tile 与原点（只增可选字段，`version` 仍为 2）**：`tiles[]` 条目可选 `size_in_atlas: [w,h]`（各 ∈ [1,4096]，缺省 `[1,1]`）、`texture_origin: [x,y]`（int 可负，缺省 `[0,0]`）、`y_sort_origin: y`（int，缺省 `0`）。校验：数组长度 2 且元素 int，origin/sort 绝对值 ≤65536。region 越界不在 load 期校验（load 不读纹理文件）。margins/separation 非 0 图集不支持，插件 warning。
- **tile 绘制语义（对齐 Godot 4.7.2）**：dest 左上 = cell 中心 − region.size/2 − texture_origin；1×1 且 origin=0 时退化为「格子左上角」。多格 tile 逻辑上仍只占一个 cell（solid 查询/tile_at 语义不变）；同层多格 tile 重叠次序 = 行主序扫描序。`y_sort_origin` 解析存储、暂不消费。
- texture 单贴图；**一个场景的多张贴图由 tro-scene v2 的 `tilesets` 数组表达**（每贴图一个 tro-tileset JSON）。
- `terrain_sets`：普通 cell-terrain/Godot terrain set 透传（`mode`: sides / corners / corners_and_sides）；`peering_bits` 仅导出该 mode 用到的邻位、值 = terrain 序号。引擎解析 + 校验并消费：`tg::load_terrain_table`/`tg::pick_tile` 按 peering_bits 把地形 pattern 确定性映射为 tile id。
- `dual_grid`：PixelLab `tileset15` 的可选四角查表元数据：`{"mode":"corners","terrains":["lower","upper"],"tile_count":16}`；对应 tile 条目使用 `dual_grid_corners: {"NW":0,"NE":1,"SW":0,"SE":1}`。它描述视觉 tile 的四个象限，不是普通 cell terrain pool；由 `tg::load_dual_grid_table`/`tg::pick_dual_grid_tile` 精确选择，缺失组合返回 `kNotFound`，不做降级。
- `custom_data` 透传，引擎忽略。

### tro-animations v1

独立动画资产（`assets/animations/*.json`），结构 = 实体 `animations` 字段，供动画素材脱离场景独立复用：

```json
{
  "format": "tro-animations", "version": 1,
  "textures": ["textures/Soldier_Attack01.png"],
  "animations": [
    { "name": "attack01", "fps": 6, "loop": false,
      "frames": [ { "texture": 0, "region": [0, 0, 100, 100] } ] }
  ]
}
```

- 由 scene_exporter v4 从 AnimatedSprite2D 导出（菜单「Export tro-animations...」/ headless `animations=`）；flat 单帧贴图也可导出为 1 动画 1 帧。
- 由引擎内置 `tg::AnimationSet`/`tg::AnimationPlayer` 消费（见「引擎公共 API 边界」）。

### v1 → v2 迁移（破坏性）

- 手写场景：`version` 改 2；用了 v1.1 tileset 单字段的改写为 `tilesets` 数组 + 层引用；palette 场景仅改 version。
- Godot 导出场景：全部由 scene_exporter v3+ 重导出，无需手改。
- **v2 → v2.1/v2.2 零迁移**：只增可选字段与 bare 形态，`version` 仍为 2，旧资产原样可读。

## PixelLab 资产管线（pixellab/）

> **定位**：PixelLab MCP（外部像素美术生成服务）→ tro-* 运行时资产的**上游转换层**，与 `editor/`（Godot 导出管线）平级——都是「上游创作输入 → assets/ 中的 tro-*」。引擎与 game 运行时零 PixelLab 概念；本层是纯离线工具（Python，依赖仅 Pillow + stdlib）。

- **MCP 调用纪律**：调用任何 PixelLab 工具前先查官方文档 `https://api.pixellab.ai/mcp/docs`；批量生成前 `get_balance`；pro 模式必须走 confirm_cost 报价流程（先报价 → 用户确认 → 再调）。全局 skill `pixellab-mcp`（`~/.agents/skills/`）承载操作指南。
- **角色/动画数据流**（产物落 `assets/`）：`import-character` / `import-character-sheet` → `assets/textures/pixellab/<n>.png` + `assets/animations/<n>.json`，fps/loop 为显式参数。
- **tileset15 数据流**：标准 PixelLab top-down 16-tile `tileset15` 可由 `import-tileset --meta <json> --image <png> --name <n>` 转为 `tro-tileset v2` 的 `dual_grid` + `dual_grid_corners`。它保留四角视觉语义，不复制成 cell terrain pool；引擎通过 `tg::load_dual_grid_table` / `tg::pick_dual_grid_tile` 精确查找。PixelLab 的 25-tile transition 形态当前拒绝导入，避免静默丢失 pattern。
- **确定性**：同输入重跑产物 byte-identical；`assets/pixellab_manifest.json` 按 (源类型, 源 id) upsert 记录来源 URL + 产物 sha256（已入库，含仍存活角色和 tileset 条目）。下载 URL 可能过期——**产物 + sha256 为权威**。
- **两种地形采样必须区分**：`tools/scene_gen` 的 `.`/`#` 是普通 cell-terrain + 多数投票占位路径；PixelLab dual-grid 使用 `(w+1)×(h+1)` `vertex_grid`，视觉 cell 的 NW/NE/SW/SE 直接读取四个顶点，由 `tools/dual_grid_scene_gen` 生成单一视觉层。
- **明确损失**：spritesheet 边 > 4096px、图像尺寸 <8px → 拒绝导入并报错，不静默伪造兼容。
- **导入验收**：`python3 pixellab/pxlab.py check-grid --image <png>`（只在检测到整数倍块放大时降采样）、`python3 pixellab/pxlab.py verify`（校验 manifest 中每个产物的 sha256）。
- **独立 tro-animations 有运行时加载器**：`tg::AnimationAsset::load/load_json` 消费 `tro-animations` v1；实体内嵌 `animations` 仍由 `SceneAsset::animation_set` 提供。
- **双路径不变**：规则明确的资产仍直接手写 tro-*；PixelLab 路径只在需要美术生成力时使用。

## 占位资产工具（tools/）

> **定位**：正式美术/瓦片集尚未就位时，让 Agent 立刻拿到**能跑、能 autotile、零外部依赖**的资产。两个工具都是确定性、无头、可校验的离线 CLI，产物落 `assets/`；生成完即可直接起游戏看画面。

- `python3 tools/placeholder_tileset.py [--name placeholder] [--tile-size 16] [--lower-name ground] [--upper-name wall] [--lower-color C] [--upper-color C]` → `assets/tilesets/<n>.json` + `assets/textures/<n>.png`。产出 **32 tile** 的占位 Wang 集（2 地形 × 16 角组合，corners mode）：`peering_bits` 与 `terrain` **由构造保证自洽**，每个地形池都覆盖全部 4 角组合，故 `pick_tile` 恒精确命中（零降级）；贴图是纯色块（底色 = 本地形色，异地形角画 1/4 边长的角块），视觉与标注同源。
- `./build/tools/trogue_scene_gen <spec.json> <out_scene.json> [--name <场景名>]` → 普通 cell-terrain tro-scene v2；spec = `{"tileset": <assets 相对路径>, "grid": ["...#", ...]}`，流程和多数投票语义保持不变。
- `./build/tools/trogue_dual_grid_scene_gen <spec.json> <out_scene.json>` → PixelLab dual-grid tro-scene v2；spec = `{"tileset": <assets 相对路径>, "vertex_grid": [[0,1,...], ...]}`，输入尺寸为视觉 cell 尺寸 + 1，输出单一 `visual` layer，缺失组合 fail-loud。
- **产物是占位**：`placeholder_tileset.py` 的色块视觉 + 临时碰撞仍可用于普通路径；PixelLab dual-grid 资产是外部视觉输入，tile 选择不引入游戏玩法概念。

## 项目模板（template/）

> **目的**：让「用 trogue 从零自主开发一个游戏」可复制——`template/` 是一个**最小**自包含项目骨架，复制它即得到能构建、能运行、能被 Agent 迭代的新游戏起点。

- **内容（最小起点，不含框架自用测试）**：`engine/`、`tools/placeholder_tileset.py` 与 `tools/scene_gen.cpp` 的 **vendored 快照** + **可选 vendored 快照** `pixellab/`（仅转换脚本）/`editor/`（安装与更新时用 `--with-pixellab`/`--with-editor` 显式选择，缺省不装——派生游戏不预设美术生成管线与 Godot 工具链）+ **起步游戏** `game/`（窗口/场景渲染/WASD 移动/热重载/IPC 基础命令）+ **两个范式范例** `game/examples/`（`swarm/` 类幸存者→ECS 风格、`platformer/` 平台跳跃→OOP 风格；同一份公共 API 的两种消费方式，可整目录删除）+ 模板自有 `CMakeLists.txt`/`.gitignore`/`README.md`/`AGENTS.md`/`scripts/`/`tools/CMakeLists.txt`/`tools/gen_font.py`/`tools/ipc_smoke.py`（即 `tools/` 下只有那两个工具文件是快照，其余模板自有）。
- **不含本仓库的测试套件与 fixture**：`tools/tests/`（引擎单测）与 `pixellab/tests/`、`pixellab/fixtures/` 是本仓库验证 trogue 引擎自用，**不进模板**。
- **权威源**：vendored 文件的权威源是**本仓库**；模板内 vendored 文件禁止手改，上游更新后在仓库内重跑 `template/scripts/sync_from_source.sh` 刷新（维护者模式：engine/pixellab/editor 整目录替换 + tools 逐文件；永不触碰模板自有文件）。
- **派生与更新（一份脚本）**：在**空目录**运行 = 新建项目（铺入模板并剥离 `README.md`/引导脚本）；在**已有项目根**运行 = 更新引擎（从上游临时克隆取快照，只刷新 vendored 集合，保留 `game/`/`assets/`/项目自有文件）。可选快照用 `--with-pixellab`/`--with-editor` 显式安装或刷新，缺省跳过（已装的项目更新时不加标志也不删除）。上游 URL/分支可用 `--url`/`--ref` 覆盖（日志内凭证自动遮盖）；`--source <dir>` 用本地源仓库代替克隆。
- **模板自有 `AGENTS.md`** 面向派生项目，保持精简：不写 API 摘要（Agent 直读 `engine/include/trogue/` 头文件），不写安装器用法，不预设题材与工具链。
- **非目标**：不改 engine 公共 API / tro-* schema；不把本仓库的 roguelike 玩法移植与其框架测试带入模板（`game/examples/` 是**消费者可选**的范式演示，不是玩法移植，也不是模板推荐的架构）；不做参数化脚手架；不自动建 git。

## 热重载规范

- **监听**：`assets/scenes/*.json` 的 CLOSE_WRITE/MOVED_TO/CREATE/MODIFY（inotify），150ms 防抖抑制编辑器原子保存连发。**有 inotify 的平台**（Linux）在 Debug 构建下生效；无 inotify 的平台或 `TROGUE_DEBUG=OFF` 时 `tg::Watcher` 恒 invalid（`poll()` 恒空，安全 no-op），热重载改由 F5 / IPC `reload` 承担。
- **语义**：watcher 只报告文件名，engine 只提供一次性资产加载（`tg::SceneAsset::load`）；game 负责 candidate load/import/swap、按自己的 OOP/ECS policy 保留或删除对象状态。engine 不按 `type` 判断 player，也不决定 ECS 状态。
- **投影约束**：若 game 把对象的 sprite/动画帧显式绘制到 engine，game 对象仍是唯一可变权威；engine asset 只提供 tile 层、视觉资源与表现原语采样。
- **失败安全**：资产加载失败不修改旧 asset（返回 `expected`/空 optional + 日志）；game candidate import/swap 失败也保留旧 asset 与旧 game state。
- 手动触发：F5、watcher、IPC `reload` 的合并、节流和 candidate coordinator 属于 game。

## IPC 协议：tro-ipc v1（仅 DEBUG 构建）

> **版本号说明**：wire 协议版本号**恒为 1**——`hello` 的 `data.version` 与 `ping` 响应的 `version` 均为 `1`，此值为能力探测契约、**只增不改**。客户端按 `version:1` 判兼容即可。
>
> **当前边界**：engine 只提供无 world 的 JSON-lines transport/callback；命令语义、实体快照、截图和退出状态全部由 `game/src/main.cpp` 的 game handler 实现，engine 不拥有或理解实体命令。

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
| `subscribe` | `events`（非空字符串数组）；可选 `filter`（object） | `{events:[{event, filter?}]}`（订阅后该连接的**全量订阅集**）。保留名 `hello` 整条拒绝 |
| `unsubscribe` | `events`（空数组 = no-op 回显；按名整体移除含全部 filter 变体） | `{events:[{event, filter?}]}`（回显当前订阅集） |
| `connections` | — | `{connections:[{conn, events:[...]}], count}` |
| `help` | — | `{commands:[...]}` |
| `events` | — | `{events:[{name, when, data, filter}]}`（game 事件注册表） |
| `status` | — | `{scene, reloads, entities, phase, fps, uptime_s, port}` |
| `list_entities` | — | `{entities:[{id,type,x,y,w,h,color, z?, rotation?, props?, transform:{visual,moving}, sprite?, hp?:[cur,max], ai?:{state,target}, anim?:{clip,frame}}], count}` |
| `get_entity` | `id` | `{entity:{...}}`（字段同 list_entities） |
| `query_entities` | 半径模式 `x` `y` `radius`；或矩形模式 `rect:[x,y,w,h]`（半径模式优先）；可选 `type` 过滤 | `{entities:[...], count}` |
| `set_entity` | `id`，`x?` `y?` `color?` | `{entity:{...}}`（改后快照） |
| `spawn` | `x` `y` 必填；`id?` `type?` `w?` `h?` `color?` | `{entity:{...}}` |
| `despawn` | `id` | `{despawned:true}` |
| `layers` | — | `{layers:[{name,width,height,solid,origin,tileset,tiles}], count}`（tileset=null 表 palette 模式；tiles=非空 tile 数） |
| `solid_at` | `x` `y`（像素） | `{solid}`（仅判定 solid tile 层；实体不参与） |
| `get_tile` | `x` `y`（像素） | `{tiles:[{layer,value}]（仅非空格）, solid}` |
| `set_tile` | `layer` `tx` `ty` `value`（层局部 tile 坐标，`tx = floor((px − 层origin_x)/tile_w)`；value=-1 表空） | `{set:true, layer, tx, ty, value}`（写后 `solid_at`/`get_tile` 立即可见；内存修改不落盘，watcher/F5/reload 会以磁盘内容覆盖——预期行为） |
| `probe_collide` | `a`/`b`（各 `[x,y]`，给则出 segment）；`rect` + `delta`（给则出 sweep）；至少给一组 | `{segment?:{result,layer,tx,ty,t,point}, sweep?:{box,blocked_x,blocked_y,result}}` |
| `probe_kinematic` | `op`（`kinematic`｜`resolve`｜`mixed`）；`kinematic`：`rect`+`delta`，可选 `one_way`；`resolve`：`rect`；`mixed`：`rect`+`delta`+可选 `others` | `kinematic`：`{box, blocked_x, blocked_y, result, grounded, landed, hit_ceiling, hit_wall, wall_dir}`；`resolve`：`{box, resolved}`；`mixed`：`{box, blocked_x, blocked_y, result, hit_dyn_x, hit_dyn_y}` |
| `reload` | — | `{reloaded:true, reloads:N}` |
| `reload_texture` | `path`（assets 相对路径） | `{reloaded:true, path}`（独立贴图缓存失效，下次绘制重读盘；路径不安全 → 错误包络） |
| `genmap` | `seed` 必填 int；`w`/`h` 可选（缺省 40，∈[1,64]） | `{generated:true, seed, w, h, nonempty, reloads}`（程序生成 palette 双层色块地图：`ground` 非 solid + `walls` solid，不依赖瓦片集/贴图；**`nonempty` = `walls`（solid）层非空 tile 数**，`ground` 非空数 = `w*h − nonempty`；同 seed 同尺寸逐位一致；尺寸极小时噪声可能退化为全墙或全空，均合法。生成后 watcher/F5/reload 会以 scene_path 覆盖之——预期行为） |
| `screenshot` | `path?`（缺省 `screenshot_<时间戳>.png`） | `{path, ok, w, h, bytes}`；**同步**——响应返回时文件已落盘（game 层离屏 FBO 渲染完整帧，不依赖屏幕缓冲；`--headless` 下同样可用） |
| `log` | `msg` | `{logged:true}` |
| `quit` | — | `{bye:true}` |

**实体快照字段说明**：`z`/`sprite`/`anim` 仅在有意义时出现；`sprite` 图集形态为 `{tileset,tile}`，独立贴图形态为 `{texture, region?, offset?}`；`transform` 为 inspector 视图（见下）；`anim` 为帧动画状态（clip 名 + 当前帧索引）。game 层可经 `Demo::extra_entity_fields`（`std::function`）在快照上追加任意字段——未来 ECS 组件观察走同一注入点，不修改引擎与 wire 包络。

> **transform 视图（inspector 式可观测性）**：实体快照含 `transform: {visual:[vx,vy], moving}`——`visual` 为当前绘制位置（移动中为插值浮点值，静止时**精确等于**逻辑格像素 x/y），`moving` 为是否在移动动画中。设计目的：非视觉 Agent 凭「静止时 visual == x/y」即可**数值发现**插值残差/截断/逻辑-视觉失步类渲染问题。

> **anim_viewer 独立端点**：`game/src/anim_viewer.cpp` 是与 demo 平级的 engine 消费者，自带 `tg::Ipc` 实例监听 **48765**（`--port` 可改），命令语义与 demo 命令表无关：`status`、`anim`（`op`: `toggle_pause`｜`next_clip`｜`play`+`clip`）、`screenshot`、`quit`、`help`。IPC op 与键鼠共用同一组动作函数——E2E 走 IPC 即覆盖触发逻辑本体。

### 事件通道语义

- **事件行**：`{"ok":true,"event":"<名>","data":{...}}`——与 hello 问候同构。**判别式（按顶层键）**：响应永远不含顶层 `event` 键；长连接读行时「有顶层 event 键 = 事件，否则 = 响应」。
- **订阅模型**：订阅记录 = (事件名, filter) 二元组，按连接存储于 engine 传输层（保留命令，不经 game handler）；同名不同 filter 并存，同对去重幂等。filter 为纯 JSON **顶层字段等值匹配**（多键 AND、`data` 缺键不匹配、数字按数值相等、空 object 恒真）——engine 不理解任何键的语义，可过滤字段由 game 在 `events` 注册表文档化。
- **publish / 断开**：game 在主线程任意点调 `tg::Ipc::publish(event, data)`（非阻塞直写）。**断开即订阅清零**：对端关闭、写失败、game 主动 `disconnect(conn)` 三条路径统一收口。慢消费者（停止读取）写遇 EAGAIN 即被断开（自愈，无出站队列）。
- **超限语义分叉**：响应行超限 = 换兜底错误行、连接保持；**事件行超限 = 无兜底行、直接断开该事件的全部 filter 匹配订阅者**（发无 event 键的兜底行会破坏判别式）。
- **时序**：publish 在调用瞬间写出；game handler 执行期内发布的事件行**先于**该连接的响应行到达。
- **脚本约定**：短连接 RPC → 零订阅零干扰；长连接订阅 → 专脚本持续读行（顶层有 `event` 键 = 事件）；**单连接端到端模式**（推荐）：同一连接 `subscribe → 发命令 → 循环读行`；监听脚本必须带 `--duration`/`--count` 出口（防残留连接占满 8 槽）。

### 已知坑

1. raylib 的 `TakeScreenshot()` 会给路径强拼 CWD 前缀，绝对路径会被破坏；本项目用 `LoadImageFromScreen()` + `ExportImage()` 替代。
2. 测试脚本必须按行解析 TCP 流（hello 与后续响应可能连包）。
3. 遗留引擎进程会占用 IPC 端口导致新实例 bind 失败（日志有提示）；清理用 `pkill -x trogue`（`-f` 模式会误杀自身 shell）。
4. 事件通道断开前，对端最后可能是**残缺行（无 `\n`）+ EOF**；读端把残行丢弃、视作断开即可。
5. 订阅连接与 RPC 连接共享 8 槽上限，残留监听器会挤占槽位；game 层可用 `connections()`/`disconnect(conn)` 定点清场。

## AI Agent 调试工作流

0. **文件工具纪律**：修改任何文件前必须先用 **read 工具**读它——禁止用 bash（`cat`/`head`/`sed -n` 等）代替读取。edit/write 工具以 read 工具的读取记录为准：未经 read 读过或读后文件已变更，edit/write 一律拒绝（「edit requires reading first」「file changed since it was read」）。此为硬性前置步骤。
1. 起服：`cmake --build build && (./build/bin/trogue > /tmp/trogue_run.log 2>&1 &)`
2. 冒烟：`python3 tools/ipc_smoke.py`（全过为基线）
3. 调试循环：`status`/`list_entities` 观测 → 改 `assets/scenes/*.json` → 0.5s 后 `status.reloads` 自增即为生效 → `screenshot` 拿画面 → `set_entity`/`spawn` 做实体运行时实验、`set_tile` 做地形运行时实验（挖墙/填墙 → `solid_at` 立即断言）
4. 收尾：`{"cmd":"quit"}` 让引擎干净退出
5. 日志在 stdout（TraceLog 格式），解析失败原因可在其中检索 `[scene]`
6. 美术资产生成走 PixelLab MCP（见「PixelLab 资产管线」）；转换产物用 `python3 pixellab/pxlab.py verify` 校验

> **截图视觉验收**：当前对话模型已支持图片输入——**Agent 自己 read 截图并下结论**，不默认推给用户目测。
> ① read 工具**不接受项目外路径**（如 `/tmp`）与 `build/` 等目录——截图写在这些位置时，先用 terminal 拷进项目内可读路径（用完即删）再 read；
> ② **禁止用代码对图片做逐像素处理/比对**（Python/PIL 等一律不用）——视觉验收以 Agent 直接 read 图片下结论为准；数值自证只允许走 IPC 实体快照/transform 视图等**结构化观测**；
> ③ 若换回不支持图片输入的模型/工具，回退旧流程：把截图路径与具体核对要点交给用户目测，**不得假装已看图**，也不得就此放弃视觉验收。

## 编码规范

- **C++20**；库命名空间统一 `tg`（`namespace tg { ... }`）；公共符号用 `tg::` 前缀命名空间，文件内私有符号用 `namespace detail` 或匿名命名空间。
- **公共 API 风格总纲**：引擎公共 API **不用 OOP 层级**——无继承、无虚函数、无抽象接口类；**自由函数优先**（查询/渲染/推进/采样等操作走 `tg::` 自由函数，如 `is_solid_at`/`render_scene`），RAII 类只持有资源与生命周期、方法为资源操作薄封装，不承载玩法逻辑；值类型 = 纯数据（public 字段、无 getter/setter 泛滥）；引擎内部直接调 raylib C API、不引入 raylib-cpp。game 层不受此约束（使用者自选 OOP/ECS）。
- **纯值类型 + 自由函数 + RAII 资源类**：优先值语义（可复制的快照 struct），资源（asset/贴图/ipc/watcher/播放器句柄）用 RAII 类管理，析构自动释放；禁止手动 `new/delete` 泄漏面。
- **异常策略**：公共 API 边界用 `tl::expected`/错误码/`std::optional` 显式表达可预期失败；内部允许 `try/catch` 兜底但不跨 API 抛裸异常；避免异常作为主控制流。
- **协程**：自研最小协程原语 `trogue/coro.hpp` 提供 `tg::task`/`tg::generator`/事件 awaiter（header-only，零第三方依赖），用于演出脚本与顺序逻辑；数据驱动播放器不依赖协程。
- 日志统一 `[模块] 消息` 前缀、TraceLog（raylib）或等价输出；用户可见消息中文，注释中文解释"为什么"。
- 内存所有权：RAII 天然表达「asset 析构释放其资源；game 持有对象与播放器实例」；asset 值生命周期结束时所有派生引用/快照须先失效（值语义 + 文档约束）。
- 参数命名清晰避免歧义（宽/高用 `ew/eh/rw/rh` 等）。
- **注释自足纪律**：源码与资产/工具代码的注释（含 docstring）**不得引用本仓库内部文档或规划产物**作为依据——禁止出现计划书文件名/节号、里程碑或门禁编号、`AGENTS.md` 指针、`reference/` 查证副本路径、`third_party/` 路径等。原因：这些文件不随代码分发（引擎/工具会被单独取走、vendored 进模板），这类指针对读者是悬空噪音；注释必须自足——直接陈述契约、语义与「为什么」。设计沿革/阶段记录归文档与 CHANGELOG，不归代码注释。合法的外部溯源（如对齐原版 `trogue-orign/*.lua`、Godot 上游符号名）可以保留。

## 开发流程

**里程碑开工门禁**：每个里程碑正式开工前必须走完闭环——

1. ① 写计划书（临时产物，见下方纪律），须说明本里程碑**补齐/验证引擎哪项通用能力**；仅让某款游戏更好玩的内容不进主线
2. ② 交 subagent 审查
3. ③ 停下等待审查结果（不得并行开工）
4. ④ PASS 才开工；不 PASS 则按审查意见修改后重新送审，循环至 PASS

> 门禁看「计划书经 subagent 审查通过」，「给出计划等待批准」指用户对计划书的拍板；两者都通过才进入实现。
> **顺序**：先送 subagent 审查并循环至 PASS，**然后**才把定稿计划交用户拍板——不经审查的草案不提前递用户（审查若推翻事实，同一份计划的返工处理会变成对用户的两次打扰）。

1. 给出**已过审**的计划等待批准（项目未正式发布，可大胆提议架构级改动）
2. 实现计划
3. **subagent 检查**未提交代码是否合理、优雅、风格统一、无逻辑问题（禁止自检，自检无效）
4. 检查之后或用户要求时更新 CHANGELOG.md（检查之前禁止修改 CHANGELOG.md）
5. 检查是否需要更新本文件
6. 询问用户是否写 commit message；如需则给出**英文** commit message 预览等待用户确认，**禁止直接提交**
7. 确认后提交**所有**变更（包括非本次变更），然后推送（`git push`，本地 main 追踪远端）

> **计划书纪律**：里程碑计划书（`docs/plan-*.md`，含分卷）是**临时产物**。门禁审查与实现期可存在；**里程碑一旦落地即删除**，不得留在仓库里充当历史或二手文档。设计结论沉淀到本文件；`docs/history.md` 只收**被取代/删除**的设计原文，逐项实现记录沉淀到 CHANGELOG。本文件与代码注释**不得引用计划书路径**（见「注释自足纪律」）。

> **远端拓扑**：唯一远端 `origin = https://github.com/Cryptocho/trogue`（原版 Lua 项目的仓库）；本地 main → 远端分支 `trogue-raylib`，两分支**零共同历史、永不 merge**；远端 main（Lua 原版）永不触碰；本地仓库不含 `trogue-orign/`（gitignore），两分支内容零重叠。remote URL 内嵌 `$GIT_PAT` 凭证（仅存本地 .git/config，勿打印/勿外传）。

- 计划必须含具体步骤（含上述 3~7 步）；重大设计先写入本文件对应章节再实现（文档先行）
- CHANGELOG 与 commit message 不包含阶段编号、AGENTS.md/TODO 等内部文档信息
- 回复用户始终使用中文；禁止 mermaid

> **子代理等待纪律**：启动 subagent 时若**下一步动作依赖其结果**，一律阻塞等待，拿到结果再继续；不要开后台 subagent 后反复轮询空耗 token。若确实需要后台并行推进独立工作，启动后**继续做有用的独立准备**（只读核对、起草文档等），只在其真正完成的通知到达后收集结果。

> **不重复造轮子**：引擎已提供的通用能力（如 `tg::TweenManager` 的数值/位置/颜色补间、`tg::AnimationPlayer` 动画）**必须直接复用，不得在 game 层手写等价物**。凡通用表现、算法、数据结构，先检索引擎 `trogue/*.hpp` 与既有代码有无现成实现；有则直接调用。game 层只写「引擎原语之上的决策/组合/编排」。

> **数值精度纪律**：涉及像素对齐/网格对齐的插值不得用指数趋近 lerp（float 永不收敛，残差被 `DrawRectangle(int)` 截断 → 恒定错位 + 相机同源放大成帧间抖动）。正确做法：固定时长 tween（引擎 `TweenManager`），播完**精确 snap 到目标整数像素**；绘制用浮点原语（raylib `DrawRectanglePro`），勿经 `DrawRectangle(int)` 截断。表现层可观测性由 IPC 实体快照的 transform 视图承担，Agent 凭「视觉位置 == 逻辑坐标」数值断言即可发现。

> **不擅自回退未提交改动**：Agent **不得**用 `git checkout -- <path>`、`git restore <path>`、`git reset --hard`、`git stash`、手动重写工作区内容等手段**动到用户未提交的工作**（包括 subagent 审查标记为 nit/无关脏改的未提交修改）。哪怕理由是「与本里程碑无关」「清理评审焦点」「属于 nit」。**先停下来问用户**——给出选项（保留、回退、单独处理），等明确指示再动。原则：未提交改动 = 用户主权；Agent 只读、提问、改自己刚写的代码。这一条也覆盖「Agent 自检发现『误改』要自己撤销」的情形——撤销自己的手笔同样要先问。

## CHANGELOG 格式规范

在 `## [Unreleased]` 下按功能模块组织变更，每个模块使用 `### 功能描述` 标题。

必填字段：`- 影响的文件:` 列出所有变更文件路径（用反引号包裹）。新的修改写在最前面。

常用子标题：`#### Added` / `#### Refactored` / `#### Bug Fixes` / `#### Architecture` / `#### Breaking Changes`

## Roadmap

> **两条线**：**引擎能力线**是交付物；**引擎能力验证线**用 `game/` 作探针，其玩法代码非交付物。新里程碑一律先问「它补齐/验证引擎哪项通用能力」——若只能回答「让某款游戏更好玩」，则不进主线。已交付项见 `CHANGELOG.md`，此处只列主线框架与未完项。

> 当前无未完成的主线项（新项先入 `docs/BACKLOG.md`，立里程碑时按门禁升入此处）。

**引擎能力线（已交付概览）**：资产解析与校验（tro-scene/tro-tileset/tro-animations）、tile 层查询与受限写入（含 solid 层索引查询）、显式渲染与离屏导出、渲染可观测计数、帧动画播放器、Tween 补间与协程等待（含 `repeats` 契约与 timer/串行演出 idiom 的文档与测试）、autotile 选择器、碰撞几何与运动学步进（单向平台/最小轴脱出/混合扫掠/SolidGrid）、碰撞视图的谓词构造与 `probe_grounded` 探针查询、确定性随机、固定步时钟与虚拟输入、IPC 传输与事件通道、热重载通知、Godot 导出插件。程序生成场景的类型化写路径（`tg::SceneSpec` + `SceneAsset::create` + `scene_spec_to_json`）与案例工具（`tools/scene_gen`）同批交付。逐项变更记录见 `CHANGELOG.md`。

**引擎能力验证线（探针：`game/`）**：回合制最小闭环、敌人 AI 与 RuleEngine 最小子集、动画查看器；模板侧两个范式范例（`template/game/examples/` 的 ECS 风格 `swarm` 与 OOP 风格 `platformer`）作为「引擎不规定对象模型」的可见、可运行证据。这些玩法系统是验证副产品，可被替换或丢弃。
