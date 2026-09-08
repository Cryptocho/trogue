# trogue 编辑器使用指南（scene_exporter v4）

用 Godot 4.7 画关卡，一键导出成引擎资产（tro-scene v2（含 bare）/ tro-tileset v2 / tro-animations v1 JSON），引擎端自动热重载。
字段级权威定义见仓库根 `AGENTS.md`「资产规范：tro-scene v2.1」，本文件只讲怎么操作。

## 插件入口

- 菜单：`Project > Tools > Export tro-tileset...` / `Export tro-animations...`
- headless（Agent / 批处理，在仓库根 `trogue/` 下执行）：
  ```bash
  # 首次或资源变更后先导入
  godot --headless --path editor --import
  # 导出场景（可多个，逗号分隔；连同其 TileSet 与实体贴图一起导出；纯实体场景 → bare）
  godot --headless --path editor --script res://addons/scene_exporter/headless_export.gd -- scene=res://assets/a.tscn,scene=res://assets/b.tscn
  # 仅导出 TileSet
  godot --headless --path editor --script res://addons/scene_exporter/headless_export.gd -- tileset=res://assets/tile_set.tres
  # 导出动画素材（AnimatedSprite2D → tro-animations v1；可多个，逗号分隔）
  godot --headless --path editor --script res://addons/scene_exporter/headless_export.gd -- animations=res://assets/soldier_animated_sprite_2d.tscn
  ```

产物直接写进引擎的 `../assets/`（scenes / tilesets / animations / textures），引擎运行中改动会 150ms 内热重载。

## 素材摆放

源素材放 `editor/assets/`（建议 `tilesets/`、`sprites/`、`ui/` 分目录）。导出时贴图会自动拷贝到引擎的 `assets/textures/`——**文件名不能重名**（会互相覆盖并告警）。

## 建 TileSet（.tres）

1. 新建 TileSet 资源并**保存为 .tres 文件**（内联在场景里的无法导出），tile size 16×16。
2. 每张贴图加一个 TileSetAtlasSource。
3. 地板想 autotile：给该 source 配 terrain set（匹配边/角），然后用**地形画笔**画图——Godot 会把选好的变体烘进每个 cell，引擎直接使用，无需运行时 autotile。peering_bits 会被透传（供以后动态改图）。
4. 树/大覆盖物：加 TileSetScenesCollectionSource，添加场景模板。参考现成的 `assets/tree.tscn`：根为 Sprite2D（AtlasTexture 取 region + offset），子级 Area2D + RectangleShape2D 定碰撞足印。
5. custom_data 随意加，会透传（引擎暂不消费）。

## 搭场景

- 根节点 Node2D，可选 metadata `background`（背景色）。
- 每层一个 **TileMapLayer**；需要碰撞的层加 metadata `solid = true`。
- 实体：Node2D 派生节点（Marker2D / Sprite2D / **AnimatedSprite2D** 都行），**节点名 = 实体 id**。
- 实体贴图：实体自身或子节点挂 Sprite2D（AtlasTexture 的 region 若恰为一格 tile，自动走图集引用；否则独立贴图）。
- **动画实体**：实体节点或子节点挂 **AnimatedSprite2D**（SpriteFrames 每个动画的帧建议用 AtlasTexture 序列帧）。导出时 `sprite` = 默认动画首帧（引擎立即可渲染静态画面），`animations` = 完整帧表（fps/loop 透传，引擎暂不播放，属玩法移植阶段）。
- 纯实体场景（没有 TileMapLayer 也没关系）：直接导出，产物为 bare 场景（无 tilesets/palette/层），引擎正常载入。
- 建议每种贴图一个 TileMapLayer（导出约束见下）。

## metadata 速查表

| 名 | 挂在哪 | 必填 | 说明 |
|---|---|---|---|
| `type` | 实体节点 / 场景 tile 模板根 | 实体必填 | 引擎按它区分行为（`"player"` 有特殊语义） |
| `solid` | TileMapLayer | 否 | `true` = 该层参与碰撞（缺省 false） |
| `solid` | 实体节点 | 否 | `true` = 实体 AABB 参与碰撞（缺省 false） |
| `solid` | 场景 tile 模板根 | 否 | **缺省 true**（树类覆盖物要阻挡），写 `false` 关闭 |
| `w` / `h` | 实体节点 / 场景 tile 模板根 | 否 | 碰撞足印，缺省 16×16（场景 tile 优先取模板内 RectangleShape2D 的 size） |
| `color` | 实体节点 | 否 | `#rrggbb`；有贴图时作染色 tint |
| `z` | 实体节点 / 场景 tile 模板根 | 否 | 渲染排序微调；默认 (y, z) y-sort 已够用 |
| `background` | 场景根 | 否 | 背景色 |

其余 metadata 全部收进 `props`（引擎保留字段，供玩法层使用）。

## 导出后验证

```bash
./build/bin/trogue --scene assets/scenes/xxx.json   # 从仓库根运行，直接看画面
```

运行中按 F12 截图，或用 IPC（`python3 tools/ipc_smoke.py` 先冒烟）。改 Godot → 重导出 → 引擎 150ms 内热重载，无需重启。

## 约束与坑（v2.1）

- **一层一贴图**：TileMapLayer 里混用多张贴图会导出报错——按贴图拆层。
- 不同层可以用**不同 TileSet**（多贴图场景的标准做法），但所有 TileSet 的 tile 尺寸必须一致。
- TileSet 里增删 tile 会让 tile id 漂移：改完 tileset 记得**重导出场景**（单次编辑会话内 tileset+scene 成对重导）。
- 场景 tile 模板的 `z_index` 被忽略（层级由引擎 y-sort 表达），需要微调时用模板根 metadata `z`。
- alternative tile 变体忽略 + warning；Sprite2D/AnimatedSprite2D 的翻转/缩放忽略 + warning。
- 动画帧用 AtlasTexture（图集取 region）或普通贴图均可；非磁盘贴图（如占位图）的帧会跳过 + warning。
- 场景导出菜单（`Export tro-scene...`）已暂撤，场景导出走 headless `scene=`；UI 恢复另行规划。
- TileSet / 贴图文件本身不参与热重载——改了贴图请重启引擎或重新导出场景。
