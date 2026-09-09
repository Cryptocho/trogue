# 历史实现归档（非当前 API）

> 本文件是 `AGENTS.md`「历史实现」内容的**完整归档**：被后续里程碑取代/删除的设计原文与实现记录。
> 全部为历史资料（非当前 API），与当前架构边界冲突时以 `AGENTS.md` 为准。本文件只读存档——新增里程碑记录不进这里（近期记录仍在 `AGENTS.md`「历史实现阶段记录」，更早的按需迁入）。

---

## 阶段 2 设计：Godot → tro-scene 资产管线（定稿）

> **注（2025-09-05）**：本章 v1/v1.1 格式已被 tro-scene/tro-tileset **v2 取代**（单 tileset 限制、scene tiles 跳过等，见「阶段 3 设计」章节）；权威字段定义以「资产规范：tro-scene v2」为准。本章仅作设计沿革保留。

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

---

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

---

## 历史实现阶段记录（2025-08-31 ~ 2026-09-07 里程碑 4）

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
  - schema v2 破坏性升级（详见上方「阶段 3 设计」）：多 tileset（`tilemap.tilesets` + 层引用，palette 与图集互斥）、实体 `sprite`（图集/独立贴图双形态）、`z`、`solid`；tro-tileset v2 透传 terrain_sets/peering_bits。
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
