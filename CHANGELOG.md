# Changelog

## [Unreleased]

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
