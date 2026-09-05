# Changelog

## [Unreleased]

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
