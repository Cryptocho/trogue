# Changelog

## [Unreleased]

### IPC 移动视觉修复与审查修正（AI 调试会话）

- 影响的文件: `game/src/main.cpp`、`game/src/rules.hpp`、`game/src/nav.hpp`、`game/src/nav.cpp`、`AGENTS.md`、`docs/plan-9.md`、`assets/textures/goblin.png`（新增）、`assets/scenes/goblin_test.json`（新增）

#### Bug Fixes
- IPC `move` 从直接调 `game::player_move` 改为复用 `handle_move`（改返回 `ActionResult`）——修复 IPC 移动不驱动视觉 tween：逻辑格已动、视觉停在旧格一整格（transform 视图暴露：logical y=64、visual y=48）；键盘/IPC 收敛为同一条「移动→tween→精确落格」管线
- `despawn` 清理对应 `enemy_view` 条目并取消 tween（此前只 erase actor，同 id 重生会渲染在旧位置）；敌人移动 tween 的 update/complete 回调改 `find` 判空，条目被删时不再静默回插僵尸条目
- 首帧 `init_player_view_if_needed` 提前到 IPC poll/按键处理之前（任何来源的首帧行动不会从 (0,0) 起 tween）

#### Added
- PixelLab 外部工具连通性验证素材：`assets/textures/goblin.png`（16×16 哥布林精灵）+ `assets/scenes/goblin_test.json`（tro-scene v2.1 bare 模式示例；加载/快照/截图/像素比对端到端验证）

#### Documentation
- AGENTS.md：截图视觉验收改为 Agent 读图自证（模型已支持图片输入，取代 2026-09-09 分工版）；IPC 命令表 `list_entities`/`solid_at` 与实体快照字段说明对齐现实现（移除不再输出的 `solid?`/`v?`，补 `transform` 视图）
- plan-9：§2.4/§7 A* 切角措辞修正（地形 + 战斗实体、惰性实体不参与；比 `try_move` 窄、比原版 A* 宽，nav.hpp/cpp 注释同步）；§6 GameOver wire 口径补 2026-09-10 实测记录（status/turn 均为 `game_over`；`turn.player` 按 §2.5 不 despawn 语义留场，快照 hp:[0,100]）

### 敌人 AI + RuleEngine 最小子集 + 首批游戏事件（EventBus）

- 影响的文件: `game/src/event_bus.hpp`（新增）、`game/src/nav.hpp`/`nav.cpp`（新增）、`game/src/rules.hpp`/`rules.cpp`（新增）、`game/src/ai.hpp`/`ai.cpp`（新增）、`game/src/game_core.hpp`、`game/src/game_core.cpp`、`game/src/main.cpp`、`game/CMakeLists.txt`、`tools/CMakeLists.txt`、`tools/tests/game_core_test.cpp`、`tools/ipc_smoke.py`、`docs/plan-9.md`（新增）、`docs/history.md`、`AGENTS.md`

#### Added
- game 层 EventBus（`event_bus.hpp`，header-only、纯逻辑零 IPC 依赖）：on/off/emit + priority 越小越先（同优先级按注册序）、dirty 延迟重建（对齐原版 events.lua）、emit 先快照后调用（handler 内 on/off/嵌套 emit 重入安全）；载荷 `tg::Json` 与 IPC wire 同构、顶层 `entity`/`source`/`target` 字符串 id 与 M7 filter 口径直接兼容
- 导航原语（`nav`）：chebyshev、Bresenham 视线（两端点不判定、遮挡回调注入）、A* 单步（8 向/chebyshev 启发/对角 1.414/切角约束复用/迭代上限 1000/实体阻挡注入——敌人互挡、玩家格不挡，对齐原版 player 无 Actor 组件语义）
- RuleEngine 最小子集（`rules`）：punch（冷却 0/射程 1）+ damage_physical（固定 5）；管线严格按原版顺序——校验（失败仅 AbilityUseFailed）→ 设冷却（仅 >0 才登记）→ DamageRequest → DamageDealt → EntityDied → 最后 AbilityUsed（仅成功），wire 上 DamageDealt 先于 AbilityUsed；TurnEnded 全体冷却 -1 下限 0；射程校验为加固（原版无）；死亡走延迟销毁（enemy → pending_despawn 收尾统一清除，player → GameOver 相位）
- 敌人 AI（`ai`）：三态状态机（idle/alerted/chasing，先迁移后行动）+ 视野（chebyshev ≤5 + Bresenham LOS，solid 层遮挡）+ 行为表（idle 70% 4 向游走 / alerted 停 1 回合（ALERT_DELAY=1）/ chasing 贴脸攻击否则 A* 一步，丢视线且抵达记忆位回 idle）；std::mt19937 固定种子（20260909）可复现
- 首批对外游戏事件 6 个（StateChanged/MoveSucceeded/AbilityUsed/DamageDealt/EntityDied/TurnEnded）经 main 层桥接 `tg::Ipc::publish`；IPC `events` 注册表从空表变实表（含可 filter 字段说明）；实体快照注入 `hp:[cur,max]` 与战斗原型 `ai:{state,target}`（coin 等惰性实体两者皆无）
- GameOver 相位：玩家 hp≤0 → 收尾不 +1 回合、不发 TurnEnded、`turn`/`status` 的 phase 三值化（player/enemy/game_over）、move/wait 拒绝、HUD 提示、reload 复位
- 敌人视觉移动复用引擎 `tg::TweenManager`（0.12s quad_out，与玩家同参数，精确落格）；`MoveSucceeded`/`EntityDied` 驱动视觉条目生命周期，热重载随 `cancel_all()` 清空
- 单测 `game_core_test` 扩至 21 用例（EventBus priority/off/快照重入、LOS/A*、状态机迁移、攻击 wire 序、死亡与 GameOver、冷却、固定种子游走复现、惰性门控）；冒烟扩至 61 项（events 注册表实表、hp/ai 快照、filter 单实体观测实战——双 filter 并存 + 缺键不匹配反向断言 + wire 序）

#### Refactored
- 移动裁决泛化为 `try_move`（任意 actor，成功 emit MoveSucceeded）；`player_move`/`player_wait` 增加 GameSystems 可空重载——完整敌方阶段（AI）与 M6 静止语义单一代码路径，既有测试零改动；新增 `tile_solid_terrain`（界外=不阻挡，供视野）与 `tile_is_solid`（界外=阻挡，M6 移动语义）区分

### 渲染：独立贴图缓存 RAII

- 影响的文件: `engine/src/render.cpp`

#### Bug Fixes
- 修复独立贴图 sprite 全部隐形（`render_sprite` 返回 Drawn、无任何日志，但画面无像素）：`SharedTexture` 未禁拷贝，`make_shared<const SharedTexture>(SharedTexture{tex})` 的临时副本析构时把刚加载的 GPU 纹理 `UnloadTexture`，缓存留下悬空 `tex.id` 的僵尸条目，此后每帧采样已删除纹理得全透明；补上 plan-5.3 §2 设计要求的非拷贝约束（`=delete` 拷贝/移动）、`tex{}` 全成员零初始化（`id==0 ⇔ 无纹理` 判据可靠）、调用点改为以 `Texture2D` 直接构造堆对象（不经临时副本）；像素级截图比对验证（soldier 预期区域 652 像素精确命中源贴图 region 特征色）+ IPC 冒烟 44/44 回归

### tro-tileset 多格 tile 与纹理原点（size_in_atlas / texture_origin / y_sort_origin）

- 影响的文件: `editor/addons/scene_exporter/tro_schema.gd`、`engine/include/trogue/config.hpp`、`engine/src/scene_impl.hpp`、`engine/src/scene_asset.cpp`、`engine/src/render.cpp`、`tools/tests/scene_schema_test.cpp`、`game/src/main.cpp`、`assets/scenes/test.json`、`assets/tilesets/test.json`（新增）、`assets/tilesets/test_1.json`（新增）、`assets/textures/Soldier.png`（新增）、`editor/assets/test.tscn`（新增）、`editor/assets/Decorations.png`（新增）、`editor/assets/Tile Set.png`（新增）、`editor/assets/Soldier with shadows/soldier.tres`（新增）、`docs/plan-8.md`（新增）、`AGENTS.md`

#### Added
- tro-tileset v2 只增可选字段（`version` 仍为 2，旧资产零迁移）：`tiles[]` 新增 `size_in_atlas`（`[w,h]`，tile 覆盖的图集格子数，region = `(col*tw, row*th, sw*tw, sh*th)`）、`texture_origin`（Godot 纹理原点，可负）、`y_sort_origin`（Godot y-sort 排序键偏移；引擎解析存储、暂不消费——无逐 tile y-sort）；校验类型/长度/值域（size 各 ∈ [1,4096]，origin/sort 绝对值 ≤65536；region 越界与 col/row 同不在 load 期校验）
- 导出插件导出三字段（headless 与菜单共用）：非缺省才写、单格 tile 导出零 diff；margins/separation 非 0 图集 warning（明确损失：引擎 col/row→像素映射不含该偏移）
- 引擎 tile 绘制对齐 Godot 4.7.2 语义：dest 左上 = cell 中心 − region.size/2 − texture_origin（`TilesetMeta::tile_rects` 升级为 `tile_visuals`/`TileVisual{region, texture_origin, y_sort_origin}`）；1×1 且原点 0 时与旧公式逐位一致，既有资产零回归；`render_sprite` 图集形态 region 自动含多格尺寸、`pos+offset` 锚点语义不变
- schema 单测 15 用例（旧格式全缺省/部分带/三字段全带/零原点正例 + 类型/长度/值域/防御上限反例逐项拒绝）接入 CTest

#### Refactored
- 导出插件解除 v2「一层一贴图」限制：TileMapLayer 混用多个贴图组时自动拆分为多个输出层（首组沿用层名、其余 `_组序号` 后缀，按组索引稳定排序），此前直接报错拒绝

#### Bug Fixes
- 修复多格 tile 显示错误：3×5 树（`size_in_atlas=[3,5]` + `texture_origin=[-2,30]`）此前被压成 16×16 单格且画在格子左上角；像素级截图比对验证（树位 1523/1523 不透明像素命中源贴图 region、旧公式位 0/1276 无命中、1×1 tile 256/256 回归命中）
- 截图前强制 flush 渲染批（`rlDrawRenderBatchActive`）：不 flush 时 `LoadImageFromScreen` 经 glReadPixels 拍到尚未提交 GL 的残缺帧（曾致截图全黑/丢元素误诊）

### IPC 事件通道（tg::Ipc subscribe/publish/disconnect，inspector 式可观测）

- 影响的文件: `engine/include/trogue/ipc.hpp`、`engine/src/ipc.cpp`、`tools/tests/ipc_test.cpp`（新增）、`tools/CMakeLists.txt`、`tools/ipc_smoke.py`、`game/src/main.cpp`、`docs/plan-7.md`（新增）、`docs/history.md`（新增）、`AGENTS.md`

#### Added
- `tg::Ipc` 事件通道（tro-ipc v1.2，只增不改，协议版本仍 1）：传输层保留命令 `subscribe`/`unsubscribe`/`connections`（ping 档，先于 game handler，无需 game handler 即可用；订阅表为传输层自有状态）；连接 id 自 accept 单调递增、断开不复用；`publish(event, data)` 非阻塞直写广播 `{"ok":true,"event":E,"data":D}` 事件行（判别式：响应永不含顶层 event 键）；`disconnect(conn_id)` game 层主动断开（自动清订阅）+ `connections()` 连接快照
- **断开即订阅清零**：对端关闭/写失败/主动断开三条路径统一 `close_slot` 单点收口；慢消费者（停止读取）写遇 EAGAIN 即被断开（自愈，无出站队列，永不阻塞主循环）；**超限语义分叉**——响应行超限换兜底错误行、连接保持（现状），事件行超限无兜底行、直接断开该事件全部 filter 匹配订阅者（防判别式漏洞）
- **subscribe 可选 filter（单实体观测）**：对事件 data 顶层字段做 JSON 等值匹配（多键 AND、缺键不匹配、数字按数值相等、空 object 恒真）；engine 不识任何键语义，可过滤字段由 game 在 `events` 注册表文档化
- game `events` 目录命令（注册表当前为空，条目形态预留 `{name, when, data}`）；`help` 登记 subscribe/unsubscribe/connections/events
- `tools/tests/ipc_test.cpp` 双分支单测（Debug 真实 loopback：订阅/退订全量集、参数校验、publish 分流、filter 匹配、handler 内 publish 先于响应、事件超限断开、慢消费者有界收敛、disconnect/conn_id 单调、无 handler 保留命令；Release 桩行为）接入 CTest（两配置各 9/9 对称）；`tools/ipc_smoke.py` 新增 12 项通道协议断言（44/44）

#### Bug Fixes
- 修正 `ipc.cpp` 两处过时注释（响应超限实为「换兜底错误行、连接保持」而非「关连接」）；`handle_line` fd 快照处钉死重入因果链（handler 内 publish 断开本连接 → 响应静默丢失、对端见事件行/残行+EOF，既有守卫已闭环）

### 回合制最小闭环（移植 trogue-origin：移动 + 碰撞 + 回合制 + IPC 回合命令）

- 影响的文件: `game/src/game_core.hpp`（新增）、`game/src/game_core.cpp`（新增）、`game/src/main.cpp`、`game/CMakeLists.txt`、`tools/CMakeLists.txt`、`tools/tests/game_core_test.cpp`（新增）、`tools/ipc_smoke.py`、`assets/scenes/forest.json`（新增）、`docs/plan-6.md`（新增）、`AGENTS.md`

#### Added
- `game_core`（game 层纯逻辑，无 UI/无渲染依赖）：回合状态机（玩家回合 → 敌方回合 → 回合计数 +1）、单格 8 向移动裁决（tile solid + 实体互斥）、敌方回合（本里程碑为「静止」策略）、`GameState` 作为 actor 表 + 回合状态唯一所有权（`main.cpp` 只读快照渲染/IPC）
- 移动碰撞对齐原版 roguelike：斜切仅当两个相邻正交格都被阻挡时禁止（贴墙/贴树可切角；目标格本身仍须可通行）；地形 solid 与越界视为阻挡；`(0,0)`/值域外/无玩家时拒绝且不消耗回合
- 手写森林关卡 `assets/scenes/forest.json`（palette 模式：非 solid 地面 + solid 墙层外框/内部障碍，玩家 + 3 敌人）
- IPC 回合命令（tro-ipc v1.1 只增不改，全部在 game handler）：`turn`（phase/turn_count/player/enemies）、`move`（`{dx,dy}` 整数各 -1..1，非整数直接报错不静默截断；`invalid`/`blocked` 返回成功包络 `data.result`，仅非玩家回合用 `ok:false+error`——当前同步结算不可达，为未来异步分支预留）、`wait`（玩家跳过行动直接结算敌方回合）；`help` 同步登记新命令
- 键盘回合制输入：WASD/方向键 4 向 + Q/E/Z/C 斜向 + 空格等待；玩家移动带帧间平滑插值；相机跟随
- **实体快照 inspector 式 transform 视图**（用户 2026-09-09 拍板）：`list_entities`/`get_entity`/`query_entities`/`turn` 的实体快照新增 `transform:{visual:[vx,vy], moving}`——`visual` 为当前绘制位置（玩家移动中为引擎 tween 插值浮点值，静止时精确等于逻辑格像素 x/y）、`moving` 是否在移动动画中。**game 层可扩展**：`Demo::extra_entity_fields`（`std::function`）可向快照追加任意字段（未来 ECS 组件观察走同一注入点，不改引擎与 wire 包络）。Agent 凭"静止时 visual==x/y"数值断言即可自动发现错位/抖动/逻辑-视觉失步
- 无窗口单测 `game_core_test`（8 用例：导入/移动消耗回合/地形与实体阻挡不消耗/单侧堵可切角双侧堵禁止/等待推进/无玩家拒绝）+ 输入缓冲 5 用例接入 CTest（Debug/Release 各 8/8）；`tools/ipc_smoke.py` 新增回合断言 + transform 视图断言（32/32）

#### Bug Fixes
- 玩家视觉移动由**引擎 `tg::TweenManager::add_vec2` + `Easing::quad_out`（0.12s）**驱动（对齐原版 `tween_system.lua` 的 `easeOutQuad(t)=-t*(t-2)=2t-t²`），**不再自造 MoveAnim/manual lerp**（避免重复造轮子；引擎 quad_out 与原版公式一致，播完精确落格=目标整数像素）
- 修复玩家与 tile 网格错位/像素抖动：原指数趋近 lerp 永不收敛（残差 ~1e-3px）再经 `DrawRectangle(int)` 截断 → 恒定错位 + 相机同源放大的帧间抖动。现静止时 view 恒等于逻辑格、无 tween 无残差；移动中实体用浮点 `DrawRectanglePro`（绕开引擎 `draw_rect` 的 int 截断），与 tile 层同相机变换严格对齐
- 键盘移动与 `trogue-orign/src/systems/input.lua` 手感一致：4 向键入缓冲（0.18s 窗口 `InputBuffer`），窗口内第二正交键立即合成对角（各自 clamp [-1,1]，反向抵消回落第一键），斜向键（Q/E/Z/C）清缓冲立即走格，空格等待
- `set_entity` 瞬移玩家后同步 snap 视觉位置（此前逻辑坐标已变、transform 视图暴露"视觉==欲望不一致"——正是 transform 视图设计要抓的问题，冒烟据此补断言）

#### Refactored
- `game/src/main.cpp` 从自由移动改为回合制：实体坐标统一 tile 网格（像素 = grid×16，spawn/set_entity 吸附网格）；渲染按 (y, z) 稳定排序显式绘制；热重载保留玩家位置与回合计数（失败安全保留旧资产旧状态）

### C++ 引擎里程碑（C++20 引擎 + 模型无关边界 + 通用表现原语）

- 影响的文件: `engine/include/trogue/*.hpp`（config/types/scene/render/animation/tween/hotreload/ipc/coro/trogue 伞，新增；删除旧 `.h`）、`engine/src/**`（`*.cpp` 替换 `*.c`）、`game/src/main.cpp`（替换 `main.c`）、`tools/CMakeLists.txt`、`tools/tests/*.cpp`（5 个单测 + 2 个 consumer smoke）、`tools/ipc_smoke.py`、顶层/`engine`/`game` 的 `CMakeLists.txt`、`AGENTS.md`、`docs/plan-5*.md`（`docs/plan-5.old-c11.md` 归档）、`.gitignore`、`CHANGELOG.md`

#### Added
- 引擎整体迁到 C++20，公共 API 纯 C++：`namespace tg`、自由函数优先、值类型（public 字段纯数据）+ RAII 资源类、无继承/虚函数；对外只暴露 `trogue/*.hpp`，不泄漏 raylib/nlohmann 类型
- `tg::SceneAsset` 只读 RAII 场景资产（`load()` 返回 `tg::expected`，失败不产生半成品）：tile 层、tileset 图集、palette、场景 descriptor 快照（`SceneEntity`/`LayerInfo`）、内嵌动画帧表
- tile-only 查询自由函数：`is_solid_at`/`rect_hits_solid`/`tile_at`（只查 solid 层；层矩形外 = 无数据 = 不阻挡；descriptor `solid` 永不参与）
- 显式渲染原语：`render_scene`/`render_sprite`/`draw_rect`/`shutdown_render`（不隐式遍历 game 对象；无窗口调用安全返回 `WindowUnavailable`）
- 通用表现原语（engine 内置，game 决定触发）：`AnimationSet`（只读集视图）+ `AnimationPlayer`（play/stop/seek/速度/loop、帧事件/完成回调、`co_await` 完成）；`TweenManager`（float/Vec2/Color 补间、缓动/延迟/循环、`wait()` 协程等待）
- 自研最小协程原语 `trogue/coro.hpp`（`tg::task`/`tg::generator`/`single_consumer_event`，header-only，零第三方协程依赖）
- `tg::Ipc`（JSON-lines 传输 + game handler 回调分发；engine 不拥有命令语义，唯一例外传输层内置 ping）与 `tg::Watcher`（150ms 防抖尾沿补触发，只报告合法 `.json` basename）；`TROGUE_DEBUG=OFF` 编译为 API 形状不变的桩
- C++ game demo（`game/src/main.cpp`）：窗口/相机/渲染、WASD 移动 + 静态碰撞、动画/Tween 示范（idle/walk 切换 + 位移补间）、热重载（Watcher + F5 + IPC reload，candidate load → 帧外 swap，失败保留旧资产）、全部 IPC 命令由 game handler 实现——证明命令是 app policy 而非 engine contract
- 测试体系：无窗口单测 5 个（schema 拒绝全集/查询边界/render 三段计数/动画 Tween 虚拟时钟/Watcher 分类 + Ipc 集成）、consumer smoke 2 个（OOP 风格与极简 ECS 各自消费同一套公共 API，证引擎与使用者对象模型无关）、`tools/ipc_smoke.py` 23 项断言全通过；测试库 seam 白名单（`render_test_*`×2 + `asset_test_*`×3）与生产库严格隔离，符号差集审计干净
- CMake：依赖改为系统包（raylib 6.0 / nlohmann_json / tl::expected，不再 vendored 第三方库）；`TROGUE_DEBUG`/`TROGUE_BUILD_CONSUMER_SMOKES` option；`-Wall -Wextra -Wpedantic` 零告警基线

#### Refactored
- 删除全部历史 C11 API：`TgWorld`/`TgEntity`/`tg_world_*`/`tg_scene_*`/`tg_tileset_*`/`tg_ipc_*`/`tg_watcher_*` 与 `world.h` 等旧公共头；资产格式（`tro-*`）不变，旧场景 JSON 零迁移
- 渲染/碰撞语义重构到新边界：不再有 engine 运行时实体池、引擎 y-sort、`type=="player"` reload 分支；热重载由 game candidate load/swap，`type`/`solid` 只作 descriptor 导入提示

#### Bug Fixes
- `AnimationPlayer` 空帧 clip 播放永不结束、`done()` 协程永久挂起（空 `frames` 的 clip 视为即时完成，`advance()` 首拍置停并触发 on_finish/done）
- `TweenManager::tick` 回调内再入 `add_*`/`cancel_all` 令正在迭代的 map 迭代器失效（UB）——tick 改两阶段：先推进/擦除、后统一触发回调
- `TweenManager` update 采样回调随首 tick 被 `std::move` 出槽导致后续 tick 不再采样——改拷贝进 deferred，每 tick 可用
- tile 查询对「极大但有限」坐标的 float→int 转换 UB（`rect_hits_solid` 先剔除完全层外 + clamp 到层尺寸后再转换；补回归测试）
- 图集 tile 矩形映射与 tro-tileset 契约不符：曾按 `id % columns` 顺序推断，而 `tiles[].col/row` 决定实际矩形（真实资产 id=0 为 col=1,row=3 会被错位）——load 期解析 col/row 建 id→Rect 表，渲染查表
- Ipc handler 抛异常/读坏类型请求会杀进程（nlohmann assert/异常穿透主循环）——engine 侧 try/catch 兜底回 `internal error`，demo handler 改类型安全读取（缺失键/类型错返回错误而非 panic）
- `draw_rect` 忽略 `color` 参数恒画白（改用传入色填充）
- `LayerInfo.nonempty` 解析时计数但从未写入快照，`layers` 命令的非空 tile 数恒为 0
- `AnimationPlayer` 非 loop clip 播完后 `frame_index()` 回卷首帧——改为停在末帧（视觉语义正确）
- nlohmann_json 曾 PRIVATE 链接但公共伞头直接 include——改 PUBLIC（consumer 编译不再依赖系统默认 include 路径）

#### Breaking Changes
- 引擎公共 API 从 C11 整体替换为 C++20（`tg::` 命名空间）；原 C API 全部删除，无兼容层

### 动画资产与插件统一（tro-scene v2.1 / tro-animations v1 / 插件 v4）

- 影响的文件: `AGENTS.md`, `docs/plan-4.md`, `engine/src/scene.c`, `editor/addons/scene_exporter/tro_schema.gd`, `editor/addons/scene_exporter/headless_export.gd`, `editor/addons/scene_exporter/scene_exporter.gd`, `editor/project.godot`, `editor/README.md`, `editor/assets/soldier_animated_sprite_2d.tscn`, `editor/assets/Soldier with shadows/*.png`, `assets/animations/soldier_animated_sprite_2d.json`, `assets/textures/Soldier_*.png`（删除 `editor/addons/tileset_exporter/` 与旧素材）

#### Added
- 实体 `animations` 字段（v2.1）：完整动画帧表 `{textures 索引表, animations:[{name, fps, loop, frames:[{texture, region?, offset?}]}]}`，贴图路径去重入索引；引擎暂不消费（透传保留），静态画面靠默认动画首帧 `sprite` 渲染
- 独立 tro-animations v1 资产（`assets/animations/*.json`）：由 AnimatedSprite2D 导出（菜单「Export tro-animations...」与 headless `animations=` 双通道），fps/loop 透传、AtlasTexture 取 atlas+region；无 AnimatedSprite2D 时 flat Sprite2D 兜底为单帧动画
- bare 纯实体场景：tro-scene v2 三态模式（图集 / palette / bare），无 tilesets/palette 且层空或缺失才合法；引擎与导出器均支持（单节点动画素材场景可直接导出为 bare）
- 导出器 v4：sprite 查找扩展到 AnimatedSprite2D（实体导出首帧 + animations 全帧表）；纯实体场景不再拒绝；root 节点（单节点素材场景）可作为实体导出；SpriteFrames 无可导出帧时降级为 warning 而非失败
- 插件统一：删除遗留 `tileset_exporter`，`project.godot` 仅启用 `scene_exporter`

#### Refactored
- 「Export tro-scene...」菜单项移除（headless `scene=` 保留，自动化通道不变；场景导出 UI 另行规划）
- 三插件文件头注释与产物清单统一为 v4（补 `assets/animations/`）

#### Bug Fixes
- headless `animations=` 参数支持 `animations=a,animations=b` 连写（与 `scene=` 对称）

### tro-ipc v1.1 观测命令（面向非视觉 Agent）

- 影响的文件: `engine/include/trogue/world.h`, `engine/src/world.c`, `engine/src/ipc.c`, `tools/ipc_smoke.py`

#### Added
- 新命令 `query_entities`（半径模式 x/y/radius 按实体中心距、矩形模式 rect:[x,y,w,h] 按 AABB 相交，同时提供时半径优先；可选 `type` 过滤）、`layers`（层信息 + tileset 名反查 + 非空 tile 数，palette 模式 tileset=null）、`solid_at`（像素坐标 solid 判定，solid 层与 solid 实体一并）、`get_tile`（像素坐标各层非空格值 + solid）
- 实体快照条件输出新字段：`z`（≠0）、`solid`（true）、`sprite`（图集形态转 tileset 名 / 独立贴图含 region/offset）、`v`（速度数组，非零时）；既有字段与响应包络不变，向后兼容
- `tg_world_tile_at()`：像素→tile 换算的公共查询 API（world.c 碰撞与 IPC get_tile 共用同一实现）
- 冒烟测试新增 8 项断言（query/layers/solid_at/get_tile），基线 15 → 23

#### Bug Fixes
- 冒烟测试玩家中心计算改为从实体快照取 w/h，消除 16×16 硬编码假设

### tro-scene / tro-tileset v2 资产格式与导出器升级

- 影响的文件: `engine/include/trogue/config.h`, `engine/include/trogue/world.h`, `engine/include/trogue/tileset.h`, `engine/include/trogue/render.h`, `engine/src/world.c`, `engine/src/scene.c`, `engine/src/render.c`, `engine/src/tileset.c`, `game/src/main.c`, `editor/addons/scene_exporter/tro_schema.gd`, `editor/addons/scene_exporter/headless_export.gd`, `assets/scenes/demo.json`, `assets/scenes/test.json`, `assets/scenes/tile_map_layer.json`, `assets/tilesets/tile_set.json`, `assets/textures/Decorations.png`

#### Breaking Changes
- tro-scene 升级 v2：`tilemap.tilesets` 数组（多 tileset，name 引用）+ 层级 `tileset` 引用；palette 模式保留且与图集模式互斥；`version` 必须 = 2，不读 v1
- tro-tileset 升级 v2：每贴图一个 JSON，新增 `terrain_sets` 与 per-tile `peering_bits` 透传（引擎暂不消费，autotile 阶段接入）

#### Added
- 实体 `sprite` 字段：图集形态（`{tileset, tile}`）与独立贴图形态（`{texture, region?, offset?}`）互斥，绘制锚点 = 实体 x/y + offset，color 作染色 tint
- 实体 `z`（渲染排序键）与 `solid`（实体 AABB 参与 solid 碰撞，点查询与 AABB 查询语义一致）
- 渲染：各层按自己的 tileset 走图集或 palette 色块；实体贴图渲染（tint = color）；实体按 (y, z) 稳定排序（场景 tile 实体排前，复刻 LÖVE「同行树先画、人后画」）；独立贴图路径缓存懒加载，新增 `tg_render_shutdown()`
- 导出器 v3：多 TileSet 按贴图分组导出（一层一贴图约束）；场景 tile（TileSetScenesCollectionSource）自动转实体（足印取 RectangleShape2D，solid 缺省 true）；实体 Sprite2D 自动导出 sprite 字段；peering_bits 按 terrain mode 导出；headless 支持逗号分隔多场景
- 限额 `TROGUE_MAX_SPRITE_TEXTURES` 移入 config.h

#### Bug Fixes
- `set_error` 将 `tg_scene_last_error()`（同一静态缓冲）作为 `%s` 输入造成自重叠 UB，错误信息损坏
- 导出器：实体图集 sprite 引用的贴图组若未被任何层使用，未登记进 `tilemap.tilesets`，导致场景被引擎整单拒绝
- `tg_parse_hex_color` 失败路径提前清零输出，导致无 color 实体为黑色而非默认白色
- 贴图加载失败或独立贴图缓存已满时每帧重复告警，改为失败占位缓存、仅告警一次

### 工程结构重构

- 影响的文件: `CMakeLists.txt`（重写为顶层聚合）, `engine/CMakeLists.txt`（新建）, `game/CMakeLists.txt`（新建）, `include/trogue/*.h`（移动至 `engine/include/trogue/`）, `src/*.c`（引擎模块移动至 `engine/src/`）, `src/main.c`（移动至 `game/src/main.c`）, `AGENTS.md`

#### Refactored
- 引擎库与游戏层物理分离：`engine/`（自包含的 trogue_engine，可整体取走复用）与 `game/`（引擎消费方），消除演示应用混入引擎源码目录的边界模糊；`TROGUE_DEBUG` option 移入 engine；可执行文件统一输出 `build/bin/`。无功能变更。

### 引擎库 trogue_engine 与演示应用

- 影响的文件: `CMakeLists.txt`, `include/trogue/`（`config.h` `world.h` `tileset.h` `scene.h` `render.h` `hotreload.h` `ipc.h` `trogue.h`）, `src/`（`world.c` `tileset.c` `scene.c` `render.c` `hotreload.c` `ipc.c` `main.c`）, `assets/scenes/demo.json`, `tools/ipc_smoke.py`, `.gitignore`

#### Added
- 静态库 `trogue_engine` 五模块：world（实体池/tile 层/碰撞查询）、scene（tro-scene JSON 解析与热重载）、render（palette 色块/图集渲染）、hotreload（inotify 资产监听）、ipc（tro-ipc v1 TCP 调试服务）
- tro-scene v1.1 资产格式：像素坐标系（原点 tilemap 左上、y 向下）、行主序 tiles（-1=空）、solid 层碰撞（层矩形外=无数据=不阻挡）、层 `origin` 偏移、可选 `tilemap.tileset` 引用（图集/色块双轨）、实体 `props` 透传
- tro-tileset v1 资产格式：`tiles[]` 数组顺序即 id，图集区域渲染，`bitmask`/`peering_bits`/`custom_data` 透传（引擎暂不消费）
- 热重载语义：监听 `assets/scenes/*.json`（150ms 防抖）；仅 `player` 按 id 保留运行时位置，其余实体完全以资产为准；解析失败保留旧场景
- tro-ipc v1：TCP 127.0.0.1（默认 48764），JSON-lines 协议，命令 `ping`/`help`/`status`/`list_entities`/`get_entity`/`set_entity`/`spawn`/`despawn`/`reload`/`screenshot`/`log`/`quit`；Release 构建编译为 no-op 桩
- 演示应用：WASD 移动（分轴滑墙碰撞）、相机跟随、F5 手动重载、F12/IPC 截图（`LoadImageFromScreen`+`ExportImage`）
- IPC 冒烟测试 `tools/ipc_smoke.py`（15 项断言）

### Godot 导出管线与编辑器项目

- 影响的文件: `editor/`（`project.godot`、`.editorconfig`、`addons/scene_exporter/`（`plugin.cfg` `scene_exporter.gd` `tro_schema.gd` `headless_export.gd`）、`assets/` 源资源副本）, `assets/tilesets/tile_set.json`, `assets/scenes/test.json`, `assets/scenes/tile_map_layer.json`, `assets/textures/Tile Set.png`

#### Added
- `editor/` Godot 4.7 编辑器项目（自 trogue-orign/tools 复制，uid/.import 完整保留）
- `scene_exporter` 插件：tro-tileset / tro-scene 导出，编辑器菜单与 `godot --headless --script` 无头通道双入口，贴图自动拷贝至 `assets/textures/`
- 场景导出映射：TileMapLayer → tile 层（`solid` 取节点 metadata，负坐标 cell 经 `origin` 表达）；带 metadata `type` 的 Node2D 派生节点 → 实体（id=节点名，坐标零换算）；其余 metadata 收入 `props`
