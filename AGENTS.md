# trogue 项目概览

> **本文件是项目的唯一权威文档。** 先写文档理清设计，再动代码；每完成一个阶段立即更新本文件对应章节。

## 项目目标

**trogue** 是一个基于 raylib 的轻量 2D 游戏引擎库，服务三个目标：

1. **schema-first**：资产是固定格式的 JSON（`tro-scene`）。编辑器只是"生产 schema 的前端"，可以替换——当前用 Godot 编辑器 + 导出插件（`trogue-orign/tools/addons/tileset_exporter/` 已验证），自研编辑器是 schema 稳定后的可选演进项。
2. **AI Agent 可参与**：DEBUG 构建内置 tro-ipc（TCP 调试通道）+ 资产热重载。Agent 可以在游戏运行中读写状态、改资产立即看效果、截图观察画面。
3. **轻量与可扩展**：运行时只依赖 raylib + jansson；渲染层薄，未来接入 Live2D/Rive2D 等外部 API 时不与引擎核心耦合。

## 与 trogue-orign 的关系

- `trogue-orign/` 是**只读参考**：原 LÖVE2D 回合制 Roguelike（ECS + RuleEngine），后续将其玩法移植到本引擎。禁止修改该目录。
- 其 Godot 导出插件（`tools/addons/tileset_exporter/tileset_exporter.gd`）产出的 tileset JSON（bitmask、custom_data）是资产管线的上游，对接方式见 [移植路线](#移植路线trogue-origin--trogue)。

## 架构分层

```
┌──────────────────────────────────────────────┐
│      Game Layer (game/src/ 游戏层)             │
│  输入/移动逻辑、相机、HUD —— 引擎的消费方       │
├──────────────────────────────────────────────┤
│         trogue_engine (engine/ 静态库)         │
│  world     实体池 + tile 层 + 碰撞查询          │
│  tileset   tro-tileset v2 加载（图集区域）      │
│  scene     tro-scene JSON 解析/载入/热重载      │
│  render    调色板色块 / 图集 tile + 实体渲染    │
│  hotreload inotify 监听（DEBUG，非 Linux no-op）│
│  ipc       tro-ipc TCP 调试服务（DEBUG）        │
├──────────────────────────────────────────────┤
│         raylib 6.0（窗口/GLFW/OpenGL）+ jansson │
└──────────────────────────────────────────────┘
```

设计约定：
- **单线程**：ipc_poll / watcher_poll 在主循环每帧调用，无锁，状态确定性好（AI 调试可预期）。
- **库不做游戏逻辑**：world 只有数据与查询；移动、AI 等属于 App/GAMEPLAY 层。
- **DEBUG no-op**：`TROGUE_DEBUG=OFF` 时 ipc/hotreload 编译为桩，API 形状不变，release 零开销。

## 目录结构

```
trogue/
├── AGENTS.md              # 本文件（唯一权威文档）
├── CHANGELOG.md           # 变更记录（Unreleased 格式见开发流程章节）
├── CMakeLists.txt         # 顶层聚合（add_subdirectory engine + game）
├── engine/                # trogue_engine 库（自包含，可整体取走复用/拆库）
│   ├── CMakeLists.txt     # 依赖查找 + TROGUE_DEBUG option + 库定义
│   ├── include/trogue/    # 公共头：trogue.h(伞) config.h world.h tileset.h
│   │                      #          scene.h render.h hotreload.h ipc.h
│   └── src/               # world.c tileset.c scene.c render.c hotreload.c ipc.c
├── game/                  # 游戏层（引擎消费方；游戏概念禁止流入 engine/）
│   ├── CMakeLists.txt     # 可执行 trogue（输出到 build/bin/）
│   └── src/main.c         # 当前为演示应用，将成长为 roguelike 本体
├── assets/                # 游戏资产（引擎按 CWD assets/ 约定读取）
│   ├── scenes/            # demo.json（手写示例）+ test.json/tile_map_layer.json（Godot 导出）
│   ├── tilesets/          # tro-tileset 导出产物
│   └── textures/          # 导出时自动拷贝的贴图
├── editor/                # Godot 4.7 编辑器项目（画关卡；.godot/ 缓存已忽略）
│   └── addons/scene_exporter/  # 导出插件 v3（菜单 + headless，v2 schema）
├── tools/
│   └── ipc_smoke.py       # IPC 冒烟测试（15 项断言）
├── build/  build-release/ # 构建产物（gitignore）
└── trogue-orign/          # 只读参考（gitignore）
```

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

| 依赖 | 版本 | 说明 |
|------|------|------|
| raylib | 6.0 | 渲染/窗口。**坑**：`+system-glfw` 的 raylib 要求系统 glfw 同时含 X11+Wayland 后端，glfw 的 `X` USE flag 必须开启（即使只用 Wayland 会话），否则链接报 `undefined reference to glfwGetX11Window` |
| jansson | 2.14 | JSON 解析（资产 + IPC） |
| inotify | 内核 | 热重载文件监听，无额外依赖 |

## 资产规范：tro-scene v2

场景文件放在 `assets/scenes/*.json`。手写示例（palette 模式）见 `assets/scenes/demo.json`；Godot 导出示例（图集模式）见 `assets/scenes/test.json`。

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
| 双模式 | `tilesets` 存在 → 图集模式；不存在 → palette 模式（tiles = 调色板索引，语义同 v1）。**两者互斥**，同时出现拒绝载入 |
| tilesets | 1..8 项 `{name, path}`，path 相对 assets/；name 场景内唯一；每个 tileset 的 tile 尺寸必须与场景 tile_width/height 一致 |
| 层 tileset | tilesets 非空时每层必填 `tileset`（引用 name）；palette 模式下层不得携带该字段 |
| tiles | 行主序一维数组，长度必须 = width×height；`-1`=空；图集模式值域 `[0, 所引 tileset.count)`，palette 模式 `[0, palette_count)` |
| solid 层 | 参与碰撞（`tg_world_rect_hits_solid`）；**层矩形之外 = 该层无数据 = 不阻挡**（v0.1.1 语义沿用）；地图边界由关卡自身绘制的边墙表达 |
| origin | 可选 `[ox, oy]` 像素（可负）：层左上角的世界偏移，Godot 负坐标 cell 由它表达 |
| 实体 id | 必填、场景内唯一；运行时 spawn 冲突自动追加 `_N` |
| 实体 type | 默认 `"unknown"`；`"player"` 有特殊语义（输入驱动 + 热重载位置保留） |
| 实体 z | 可选 number（缺省 0，浮点取整）：渲染排序键之一，见渲染顺序 |
| 实体 solid | 可选 bool（缺省 false）：true 时实体 AABB 参与 solid 碰撞（LÖVE Solid 组件的对应物；场景 tile 转出的覆盖物如树靠它阻挡）。仅 `true` 字面量生效，其余值按缺省 false 处理 |
| 实体 sprite | 可选对象，两种形态互斥：图集形态 `{"tileset": name, "tile": id}`（name 必须在场景 tilesets 中；不接受 region/offset，写了被忽略）或独立贴图形态 `{"texture": "textures/x.png", "region": [x,y,w,h]?, "offset": [ox,oy]?}`；region 缺省整图，offset 缺省 `[0,0]`；绘制锚点 = 实体 x/y + offset，贴图按原始像素尺寸绘制（不缩放） |
| color | `#rrggbb` 或 `#rrggbbaa`，缺省白色；有 sprite 时作染色 tint（缺省白 = 原样绘制），无 sprite 时为色块颜色 |
| 渲染顺序 | 先全部 tile 层（层间按数组序），后实体；实体按 **(y 升序, z 升序) 稳定排序**，完全并列时保持资产数组序（导出器把场景 tile 实体排在数组前部，复刻 LÖVE「同行树先画、人后画」） |
| 校验 | format/version 不符、tiles 长度或值域不对、tileset 引用不存在、尺寸不一致 → 拒绝载入并保留旧场景 |
| 限额 | layers ≤4，tilesets ≤8，palette ≤32，实体池 256，实体名 63 字节 |

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

### v1 → v2 迁移（破坏性）

- 手写场景：`version` 改 2；用了 v1.1 tileset 单字段的改写为 `tilesets` 数组 + 层引用；palette 场景仅改 version。
- Godot 导出场景：全部由 scene_exporter v3 重导出，无需手改。

## 热重载规范

- **监听**：`assets/scenes/*.json` 的 CLOSE_WRITE/MOVED_TO/CREATE/MODIFY（inotify），150ms 防抖抑制编辑器原子保存连发。非 Linux 为 no-op。
- **语义（重要）**：重载成功后——
  - `type=="player"` 的实体按 id 保留运行时位置（玩家状态属于运行时所有权）；
  - **其他实体完全以资产为准**：坐标改动生效、新增即生成、删除即消失（保证编辑器/AI 对场景的修改可见）；
  - tilemap/palette/meta 即时生效。
- **失败安全**：解析失败保留旧场景，错误进 TraceLog 与 `tg_scene_last_error()`。
- 手动触发：应用内 F5 键或 IPC `reload` 命令。

## IPC 协议：tro-ipc v1（仅 DEBUG 构建）

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
| `list_entities` | — | `{entities:[{id,type,x,y,w,h,color}], count}` |
| `get_entity` | `id` | `{entity:{...}}` |
| `set_entity` | `id`，`x?` `y?` `color?` | `{entity:{...}}`（改后快照） |
| `spawn` | `x` `y` 必填；`id?` `type?` `w?` `h?` `color?` | `{entity:{...}}` |
| `despawn` | `id` | `{despawned:true}` |
| `reload` | — | `{reloaded:true, reloads:N}` |
| `screenshot` | `path?`（缺省 `screenshot_<时间戳>.png`） | `{path}`；文件在下一帧绘制后写出 |
| `log` | `msg` | `{logged:true}`（打印进引擎日志） |
| `quit` | — | `{bye:true}`（引擎退出主循环） |

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

1. 起服：`cmake --build build && (./build/trogue > /tmp/trogue_run.log 2>&1 &)`
2. 冒烟：`python3 tools/ipc_smoke.py`（15 项断言全过为基线）
3. 调试循环：`status`/`list_entities` 观测 → 改 `assets/scenes/*.json` → 0.5s 后 `status.reloads` 自增即为生效 → `screenshot` 拿画面 → `set_entity`/`spawn` 做运行时实验
4. 收尾：`{"cmd":"quit"}` 让引擎干净退出
5. 日志在 stdout（TraceLog 格式），解析失败原因可在其中检索 `[scene]`

## 编码规范

- C11，库符号一律 `tg_` 前缀；文件内私有函数 `static`。
- **纯数据 struct + 自由函数**，禁止 OOP 宏/metatable 式模拟（延续原项目风格）。
- 错误处理：返回 `bool`/`NULL` + `TraceLog(LOG_ERROR/WARNING)` 记因；用户可见消息用中文，日志统一 `[模块] 消息` 前缀。
- 内存所有权：`tg_world_create/destroy` 管理全程；`world_swap` 转移 tiles 指针所有权，src 置空防双释放。
- 参数名避免与 `TgWorld *w` 冲突（宽度/高度用 `ew/eh/rw/rh`）。
- 注释中文，解释"为什么"而非"是什么"。

## 开发流程（沿用 trogue-orign 流程，用户 2025-09-01 拍板）

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

## CHANGELOG 格式规范

在 `## [Unreleased]` 下按功能模块组织变更，每个模块使用 `### 功能描述` 标题。

必填字段：`- 影响的文件:` 列出所有变更文件路径（用反引号包裹）。新的修改写在最前面。

常用子标题：`#### Added` / `#### Refactored` / `#### Bug Fixes` / `#### Architecture` / `#### Breaking Changes`

## 移植路线（trogue-origin → trogue）

| 原项目 (Lua) | 本引擎 (C) | 备注 |
|--------------|-----------|------|
| TILE_SIZE=16 / SCALE=2 | tile_width/height=16 + 相机 zoom 2 | 对齐 |
| 1-based tile 坐标 | 0-based 像素 | 换算：`px = (tx-1)*16, py = (ty-1)*16` |
| Position/Stats/Actor 组件 | TgEntity 平铺字段 → 移植时扩展 | 组件数据仍为纯 struct |
| Solid 组件 | solid tile 层 | 语义一致 |
| autotile 4-bit bitmask | tro-tileset 透传 `terrain_set`/`terrain`（最终格式待 autotile 阶段定义） | 对接点 |
| custom_data `Ground` (bool) | 透传至 tileset `custom_data`（v1 引擎忽略） | solid 语义改由场景分层表达 |
| RuleEngine 事件管线 | 待定（先移植移动/回合最小闭环） | 见 Roadmap |

## 阶段 2 设计：Godot → tro-scene 资产管线（定稿）

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

## 阶段 3 设计：tro-scene/tro-tileset v2 与素材重整（定稿，2025-09-05）

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
- [ ] autotile/bitmask 渲染（tileset v2 的 peering_bits 已透传）
- [ ] 移植 trogue-origin：移动+碰撞+回合制最小闭环（InputSystem/MovementSystem/TurnSystem 对应物）+ IPC 回合命令
- [ ] RuleEngine 事件管线 C 化
- [ ] 二进制资产格式（可选，JSON 为准）

## 阶段记录

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
