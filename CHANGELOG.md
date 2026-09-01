# Changelog

## [Unreleased]

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
