# Trogue 开发 TODO

## 已完成

1. 明确bit-mask和auto-tile机制并写成文档 [x]
2. 设计统一的tile-set格式 [x]

---

## 升级 tile.py — 瓦片编辑器 (TODO #3 + #4)

> 目标：将 tools/tile.py 从"标记+导出"的单向工具升级为**完整的瓦片集编辑器**，
> 支持属性编辑、分组管理、文件回读、撤销重做。

### Phase 0: 环境准备 (0 步，已满足)

> ✅ Python 3 + tkinter + Pillow 可用
> ✅ 基础图片加载/缩放/网格覆盖已实现
> ✅ 3×3 bitmask 弹窗编辑器已实现
> ✅ 格式刷已实现
> ✅ Lua 导出已实现

---

### Phase 1: 数据模型层 — 建立可编辑的内部结构

当前问题：编辑器状态（`state` dict）是 ad-hoc 的 `{(col,row): ...}` 结构，无法承载
tile properties 和 groups。需要建立数据模型层。

#### Step 1.1: 引入 `TilesetProject` 数据类

- [x] 影响的文件: `tools/tile.py`
- 创建 `TilesetMeta` dataclass: `source, cols, rows, tile_width, tile_height, padding`
- 创建 `TileDef` dataclass: `index, col, row, properties: dict, bitmask: list|None`
- 创建 `GroupDef` dataclass: `name, tile_indices: list[int]`
- 创建 `TilesetProject` dataclass: `meta: TilesetMeta, tiles: list[TileDef], groups: list[GroupDef]`
- 所有字段可读写，不再用 tuple key 散落在 `state["bitmasks"]` 等字典中
- 不需要 `version` 字段，项目未 release，无兼容负担

#### Step 1.2: 迁移现有 `state` 到 `TilesetProject`

- [x] 移除 `state["marked_cells"]` 中的 tuple 存储，改用 `tiles` 列表
- 移除 `state["bitmasks"]` 字典，迁移到 `TileDef.bitmask`
- 确保网格变更、bitmask 编辑、清除/反选等全部通过 `TilesetProject` 操作
- 更新所有 UI 回调 (`on_left_press`, `on_right_press`, `on_clear`, `on_invert` 等)
- 验证：功能与当前 944 行的 tile.py 行为一致

#### Step 1.3: 实现 `.lua` 文件回读（Import）

- [x] 影响的文件: `tools/tile.py` (新增 `load_lua_tileset` 函数)
- 新增 `Ctrl+O` 快捷键打开已有 `.lua` 文件
- 解析 `tileset-format.md` 定义的 Lua 表结构：
  - 逐行 parse，识别 `source`, `cols`, `rows`, `tile_width`, `tile_height`, `padding`, `count`
  - 解析 `tiles` 表：extract `col`, `row`, `properties`, `bitmask`
  - 解析 `groups` 表：extract `groupName`, `tiles` 列表
- 回读后自动加载对应 PNG 图片（按 `source` 字段路径）
- 填充 `TilesetProject` 并重建 UI
- 验证：打开任何 .lua → 完整还原标记、properties、bitmask、groups
- 注意：只需确保编辑器自身的 save↔open 往返一致，不涉及旧格式迁移

#### Step 1.4: 强化 Lua 导出（Export）— 确保往返一致

- [x] 影响的文件: `tools/tile.py` (修改 `on_export`)
- 在导出时遍历 `TilesetProject.tiles`（而非 `state["marked_cells"]`）
- 输出 `properties` 字段（当前导出缺失 properties）
- 输出 `groups` 表（当前导出缺失 groups）
- 验证：编辑器内标记 → 保存 .lua → Ctrl+O 重新打开 → 所有数据完整还原

---

### Phase 2: 属性编辑器 (对应原 TODO #3)

当前问题：`properties` 在 tileset-format.md 中已定义但 tile.py 完全不能编辑。
导出时直接丢弃了 properties 数据。

#### Step 2.1: 右侧属性面板骨架

- [ ] 影响的文件: `tools/tile.py`
- 在 sidebar 底部新增 "属性" 区域（带分隔线）
- 当无选中 tile 时显示灰色提示 "选择瓦片以编辑属性"
- 当选中 tile 时显示属性列表

#### Step 2.2: Key-Value 属性列表控件

- [ ] 每个属性行：`[Key 输入框] [Value 输入框] [删除按钮]`
- Key 支持 string/freeform 输入
- Value 自动推断类型：`true`/`false` → boolean，纯数字 → number，其他 → string
- 删除按钮移除该行
- 底部 `[+ 添加属性]` 按钮

#### Step 2.3: 属性读写双向绑定

- [ ] 属性列表变更 → 立即更新 `TileDef.properties`
- 选中切换 tile → 属性列表立即刷新显示新 tile 的属性
- 验证：tile A 设置 `{solid=true, terrain="grass"}` → 切换到 tile B → 切回 tile A → 属性还在

#### Step 2.4: 属性刷 (Paint) 工具

- [ ] 影响的文件: `tools/tile.py`
- 在属性面板选中某个 key-value 行时，该行高亮，"格式刷"激活
- 激活后点击/拖动图集 tile → 将该属性写入目标 tile 的 properties
- 右键或 Escape 退出属性刷模式
- 验证：选中 "solid=true" → 点击 5 个 tile → 5 个 tile 都有 `solid=true`

---

### Phase 3: 分组管理 (对应原 TODO #4)

当前问题：groups 在 tileset-format.md 已定义但 tile.py 完全不能编辑。

#### Step 3.1: Group 列表面板

- [ ] 影响的文件: `tools/tile.py`
- 在 sidebar 中间（标记区域下方）新增 "分组" 区域
- 显示所有 Group 的列表，每行 `[Group名] [tile 数量] [删除]`
- 底部 `[+ 新建分组]` 按钮，弹出输入框输入组名

#### Step 3.2: 拖拽添加 tile 到分组

- [ ] 单击 group → 进入 "编辑分组模式"
- 该模式下点击/拖动图集 tile → 将 tile 加入/移出该 group
- 加入的 tile 以特殊颜色高亮（不同于普通标记蓝色）
- 再次点击该 group 或点击空白区域 → 退出编辑分组模式

#### Step 3.3: 分组内 tile 排序

- [ ] group 中有序列表：第一个 tile = Icon（兜底瓦片）
- 右键 group → "设为首个" → 将被选 tile 移到列表最前
- 左右方向键调整顺序（可选）

---

### Phase 4: 撤销/重做 (Undo/Redo)

#### Step 4.1: 命令模式封装

- [ ] 影响的文件: `tools/tile.py` (新增 `Command` / `UndoManager` 类)
- `Command` 抽象：`execute()` + `undo()` + `description`
- `UndoManager`：栈管理，`push(command)` → 执行并入栈
- Ctrl+Z → `undo()`，Ctrl+Shift+Z → `redo()`

#### Step 4.2: 迁移操作到命令模式

- [ ] 至少覆盖以下操作：
  - 标记/取消标记 tile
  - bitmask 编辑（单个 cell 切换 + 批量格式刷）
  - properties 增删改
  - group 增删改、tile 加入/移出 group
- 每项操作包装为 `Command` 子类

---

### Phase 5: UI 打磨

#### Step 5.1: 快捷键系统

- [ ] Ctrl+S: 保存到 .lua 文件
- [ ] Ctrl+O: 打开已有 .lua 文件
- [ ] Ctrl+Z / Ctrl+Shift+Z: 撤销/重做
- [ ] Delete: 删除选中 tile
- [ ] Ctrl+A: 全选所有 tile
- [ ] Escape: 退出属性刷/格式刷/分组编辑模式

#### Step 5.2: Autotile 实时预览窗口

- [ ] 影响的文件: `tools/tile.py` (新增预览弹窗)
- 选择 group → "预览 Autotile"
- 在小网格中放置 tile → 实时看到引擎级别的 bitmask 匹配结果
- 本质是模拟 `doc/bitmask_autotile.md` 第 4.3 节的匹配算法
- 验证：确认 47 种 Minimal 3×3 模板都能正确匹配

#### Step 5.3: 多 tileset 项目

- [ ] 左侧 source 列表面板（类似 Godot）
- 在同一个 tile.py 实例中管理多个 .lua 定义
- 支持新建/打开/切换/关闭

---

### Phase 6: 文档与格式更新

#### Step 6.1: 完善 tileset-format.md 文档

- [ ] 影响的文件: `doc/tileset-format.md`
- 完善 `properties` 字段文档（当前只有示例 `solid=true`，缺少正式说明和推荐 key 列表）
- 完善 `groups` 字段文档（补充约束：组内第一个 tile 为 Icon 兜底瓦片、同 tile 可属多组）
- 新增 **编辑器往返一致性** 说明：tile.py 的 save↔open 必须数据不丢失

#### Step 6.2: 同步更新 AGENTS.md

- [ ] 影响的文件: `AGENTS.md`
- 在 "程序化地图生成管道" 章节附近补充 tileset 编辑流程说明
- 添加 tile.py 的完整用途描述和文件路径引用

---

## 游戏本体 TODO (不动)

5. 在map render中实现tile set的自动加载;属性读取auto-tile [ ]
6. 完成大体积实体的遮掩效果,并实装tree的实际游戏效果 [ ]

---

## 进度追踪

| Phase | 状态 | 预计工作量 |
|-------|------|-----------|
| Phase 1: 数据模型层 | ✅ 已完成 | 2-3h |
| Phase 2: 属性编辑器 | ⬜ 待开始 | 3-4h |
| Phase 3: 分组管理 | ⬜ 待开始 | 2-3h |
| Phase 4: 撤销/重做 | ⬜ 待开始 | 1-2h |
| Phase 5: UI 打磨 | ⬜ 待开始 | 2-3h |
| Phase 6: 文档 | ⬜ 待开始 | 0.5h |

**总计估算**: 10.5-15.5h，按 Baby Steps 每个 Step 独立可验证交付。
