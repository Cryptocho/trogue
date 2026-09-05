# ASSETS：素材清单与整理指南

> 面向素材整理的对照文档：说明 LÖVE 版 demo（`trogue-orign/`，只读参考）当前实际使用的素材类型、去向与整理要求。
> 权威资产格式见 `AGENTS.md`「资产规范：tro-scene v2」；本文只回答「有哪些素材、放哪、怎么挂」。
> 已核实（grep 源码引用）：demo 实际引用的贴图只有 9 张 PNG；字体零引用；音频被 `conf.lua` 禁用。

## 一、素材类型总览

| # | 类型 | LÖVE 源文件 | 用途 | editor/assets 整理去向 | 导出后进引擎哪里 |
|---|------|------------|------|----------------------|----------------|
| A1 | 地板 autotile 图集 | `src/assets/Tile Set.png` | 地板 16 变体（4-bit bitmask），地形画笔烘进地图 | `tilesets/floor_*.png` | TileSet atlas source → `ground` 层图集 |
| A2 | 墙/怪图集 | `src/assets/tileset.png` | 墙(1)、goblin(3)、rat(4)、orc(5)、毒池(6)、火池(7)、地面物品(9)；树占位(8)不画 | `tilesets/actors_*.png`（或墙怪分两张） | 墙 tile → `walls` 层图集；怪 tile → 敌人实体的图集 sprite |
| B | 独立角色贴图 | `src/assets/image.png` | 玩家 | `sprites/player.png` | `player` 实体的 Sprite2D → 独立贴图 sprite |
| C | 覆盖物（多格贴图） | `src/assets/Decorations.png` | 树（region 70×98，居中，偏上，脚下一格碰撞） | `sprites/decorations.png` | TileSet 场景 tile（tree.tscn 模板）→ 自动转 solid 实体 |
| D | UI 图标 | `src/assets/hit.png` `heal.png` `defend.png` `fireball.png` | 技能栏图标（punch/heal/shield/fireball），UI 显示 48px | `ui/icons/*.png` | 暂不进 schema，游戏层 UI 直接 `LoadTexture` |
| E | 字体 | `fonts/BigBlueTerm...ttf` | **未被引用**（LÖVE 用默认字体）——闲置 | `fonts/`（可选保留） | 游戏层 `LoadFont`，暂未消费 |
| — | 背景色 | 无文件 | `main.lua` 清屏纯黑 | 场景根节点 metadata `background`（Color） | `meta.background` |
| — | 音频 | 无 | `conf.lua` 禁用 | 不需要 | — |

## 二、demo 用到的具体素材规格

### A1 地板 autotile 图集（`Tile Set.png`）
- 16×16 tile 网格；**有效变体位于 col 1–4 / row 3–6**（4-bit bitmask 0–15，LÖVE `tileset.lua` 的 `bitmask_map`）
- Godot 挂法：TileSet 加 atlas source + **terrain set（Match Corners 模式，当前 tile_set.tres 用的 corners）**，用地形画笔画 `ground` 层——变体选择由 Godot 完成，引擎无需运行时 autotile
- 替换素材时：保持 16×16；变体数量/布局可变，重导出即可（terrain peering_bits 会透传）

### A2 墙/怪图集（`tileset.png`）
- 16×16，**每行 8 格**（LÖVE `TILES_PER_ROW=8`，tile index = row*8+col）
- tile 1=墙（画进 solid 层）；tile 3/4/5=goblin/rat/orc（**不画进层**，作为敌人实体参考图）
- 毒池/火池/物品是玩法期素材，本轮可不整理
- 替换素材时：若增删 tile 导致 tile id 漂移，**tileset + 场景需成对重导出**

### B 玩家独立贴图（`image.png`）
- ⚠️ 语义差异：LÖVE 会把玩家图**缩放对齐 16px 格底**；trogue v2 的独立贴图 sprite **按原始像素尺寸绘制、不缩放**
- 替换素材时：直接按目标显示尺寸出图（建议 16×16，或最终想要的大小），对齐左下角的角色请预留好 offset（Sprite2D `offset`/`centered` 由导出器自动换算）

### C 覆盖物（`Decorations.png` + `tree.tscn` 模板）
- 当前树：贴图 region `(11, 143, 70, 98)`、`offset (0, -38)`、centered、足印 16×16（来自模板里的 RectangleShape2D）
- 换树：改 `tree.tscn` 模板的 AtlasTexture region 与 offset 即可（导出器自动换算）；模板根可加 metadata 覆盖 `type`/`w`/`h`/`z`/`solid`（solid 缺省 **true**）
- 每种覆盖物一个 `.tscn` 模板（Sprite2D 可挂 AtlasTexture），在 TileSet 里配成场景 tile 后正常画层

### D 技能图标（4 张）
- 仅游戏层 UI 用，**不进场景 schema**；尺寸随意（UI 里按 48px 缩放显示），像素风即可

## 三、editor/assets 现状盘点（整理时处理）

**有效（保留/沿用）**：
- `Tile Set.png`（地板 autotile 源）、`tile_set.tres`（现役 TileSet：地板图集 + tree 场景 tile）
- `Decorations.png`、`tree.tscn`（树模板）
- `test.tscn`、`tile_map_layer.tscn`（管线测试场景）

**杂物（建议清理）**：
- `tile.png` — 零引用
- `Tileset.png`、`new_tile_set.tres`、`temp.lua`、`temp.tscn` — 仅被废弃的 temp 场景引用（注意与 `Tile Set.png` 大小写重名，容易混）
- `tileset.json`、`tileset.lua` — 旧导出产物（现产物在引擎侧 `../assets/tilesets/`），可删

**demo 需要但 editor/assets 目前没有的**：`tileset.png`（墙/怪）、`image.png`（玩家）、4 张技能图标 —— 从 `trogue-orign/src/assets/` 取用或替换

## 四、目录约定（整理后）

```
editor/assets/
├── tilesets/        # A 类：进 TileSet 的图集源图
├── sprites/         # B/C 类：独立角色贴图、覆盖物贴图
├── ui/              # D 类：图标等（游戏层直接用）
├── fonts/           # E 类：字体（可选）
├── *.tres           # TileSet 资源
└── *.tscn           # 关卡场景（每个关卡一个）
```

- 引擎侧产物（`assets/scenes`、`assets/tilesets`、`assets/textures`）全部由导出器生成，**不要手动编辑**
- ⚠️ 独立贴图拷贝时按**文件名**平铺进 `assets/textures/`，不同目录下**同名贴图会互相覆盖**——素材命名请避免重名

## 五、整理 checklist

- [ ] 清理 editor/assets 杂物（第三节列表）
- [ ] 素材按目录归位（第四节），全部 PNG、像素风、tile 类保持 16×16
- [ ] 建 TileSet（.tres，必须保存为文件）：每张图集一个 atlas source；地板 source 配 terrain set；需要场景 tile 的配置场景 tile（引用 sprites/ 下的模板 tscn）
- [ ] 画关卡：TileMapLayer 按贴图拆层（**v2 限一层一贴图**）；碰撞层加 metadata `solid=true`
- [ ] 摆实体：Node2D 节点，名 = id，metadata `type` 必填，贴图挂 Sprite2D；可选 `w`/`h`/`color`/`z`/`solid`
- [ ] 场景根可加 metadata `background`
- [ ] 导出并验证：菜单导出或 headless；产物落 `../assets/`，引擎热重载直接看效果