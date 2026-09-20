# 教堂场景集成计划

## 设计决策

- **入口**: 走到主地图特定格子自动进入教堂
- **敌人**: 教堂内部无敌人
- **出口**: 回到主地图原来的位置

---

## 当前进度

### 已完成

#### Step 1: map_generator.lua — 教堂布局 ✅
- 添加 `type == "church"` 分支
- 预制 32x30 字符数组布局
- 返回 mapData, nil, nil

#### Step 2: map_renderer.lua — 教堂瓦片 + 物体渲染 ✅
- 加载教堂 tileset (`assets/church/tileset/tileset.png`)
- 加载教堂物体精灵 (`assets/church/objects/*.png`)
- Wang tile 角落匹配渲染（4-bit corner key）
- 新增字符映射：`#` → TILE_CHURCH_WALL, `.` → TILE_CHURCH_FLOOR
- 物体字符（A/P/C/S/L/T/O）渲染为 scene tiles
- `isExitTile()` 方法检测出口格子
- 入口/出口视觉标记（金色/绿色方块）

#### Step 3: main.lua — 地图切换逻辑 ✅
- 主地图自动找空地放置教堂入口（`E` 字符）
- `loadChurchMap()` — 切换到教堂内部，玩家传送到出口上方
- `returnFromChurch()` — 恢复主地图，玩家回到原位
- 出口格子检测：玩家踩到 `E` 触发返回
- 切换时清理/重建敌人和 FogOfWar

### 待处理

#### 布局问题（用户反馈）
- 柱子像边框，缺乏内部结构感
- 彩窗直接贴地，没有靠墙逻辑
- 中央灰色方块突兀
- 祭坛/蜡烛位置散乱，缺乏仪式感焦点

---

## 教堂重做计划

### Phase 1: 资源生成 (Pixellab)

游戏现有规格：tile_size=16x16，view=high top-down，角落匹配 Wang tile（4-bit corner key）。

#### 1.1 Tileset：木地板（standalone，无墙壁过渡）✅

- **Pixellab ID**: `18a9b304-4911-40fb-ad8a-b86c085bcd57`
- **base_tile_ids**:
  - lower: `028c3d75-8d29-468d-9ae6-21b73e405c04`
  - upper: `4d2eb971-c3cc-4325-b879-660a1746495a`
- **文件**: 已下载替换到 `assets/church/tileset/` (旧文件已备份为 `*_old.*`)
- **参数**: tile_size=16, view="high top-down", transition_size=0, outline=lineless, detail=highly detailed

### Phase 2: 布局重设计 + map_renderer 适配

> 待 Phase 1 资源就绪后进行

- [ ] 将新木地板 tileset 替换现有石地板 tileset（替换 `assets/church/tileset/` 下的 PNG 和 metadata）
- [ ] 重新设计 32x30 字符数组布局：柱子改为内部双列对称，彩窗靠墙，祭坛居中
- [ ] 墙壁/地毯等后续再替换（本次只改地板）

---

## 字符定义

```
#  = 墙壁（不可通行）
.  = 木地板（可通行）
A  = 祭坛（不可通行，物体精灵）
P  = 长椅（不可通行，物体精灵）
C  = 十字架（不可通行，物体精灵）
S  = 彩色玻璃窗（不可通行，物体精灵）
L  = 烛台（不可通行，物体精灵）
T  = 讲台（物体精灵）
O  = 石柱（不可通行，物体精灵）
E  = 出口（可通行，触发返回主地图）
```

## 技术细节

### Wang Tile 渲染
- 使用角落匹配而非边缘匹配
- 4-bit key: NW + NE*2 + SW*4 + SE*8（1=wall, 0=floor）
- 从 metadata.json 解析 bounding_box 构建 quads

### 物体渲染
- 64x64 物体居中在16x16 瓦片上绘制
- 类似树木的 scene tile 方式
- 物体格子不可通行（出口除外）
