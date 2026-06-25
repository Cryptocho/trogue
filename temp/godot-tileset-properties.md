# Godot TileSet 自定义属性系统分析

## 数据模型：层级化 Schema

Godot 采用**两层模型**：

### 1. TileSet 层（Schema 定义）

```cpp
struct CustomDataLayer {
    String name;           // 属性名，如 "health", "is_solid"
    Variant::Type type;    // 固定类型：INT, FLOAT, STRING, BOOL, VECTOR2, COLOR, ...
};
Vector<CustomDataLayer> custom_data_layers;  // 有序图层列表
```

- 属性在 TileSet 级别**预定义**（名称 + 固定类型）
- 支持添加/删除/重排图层
- 类型在创建时选定，**不可运行时改变**

### 2. TileData 层（每 tile 的值）

```cpp
Vector<Variant> custom_data;  // 与 layers 数组平行，index = layer_id
```

- 每个 tile 持有与 layer 数量相同的值数组
- 未设置的值 = Variant::NIL（零值、空串、false）
- 序列化时 NIL 值**不写入**（`PROPERTY_USAGE_NIL_IS_VARIANT`）

### 属性路径

Inspector 中通过 `custom_data_0`, `custom_data_1` 等路径访问。

---

## 属性类型系统

| 类型 | Godot Variant | 值示例 |
|------|-------------|--------|
| 整数 | INT | 42 |
| 浮点 | FLOAT | 3.14 |
| 字符串 | STRING | "grass" |
| 布尔 | BOOL | true |
| 颜色 | COLOR | Color(1,0,0) |
| 向量 | VECTOR2/VECTOR2I | (10, 20) |
| 对象 | OBJECT | 资源引用 |

**关键设计决策**：类型绑定在 layer 上，而非 per-tile。所有 tile 的同一 layer 值类型一致。

---

## 属性编辑 UI

### Layer 管理（TileSet Inspector）

- 顶层显示 "Custom Data" 分组
- 每个 layer 有名称输入框 + 类型下拉菜单
- 支持添加/删除/重排（Array 风格的 PROPERTY_USAGE_ARRAY）

### 每 tile 值编辑（Tile Inspector）

- 选中 tile 时在 Inspector 中显示所有 layer 的编辑器
- 编辑器类型由 layer 类型决定：int→SpinBox, string→LineEdit, bool→CheckBox, color→ColorPicker
- 多选时仅显示**公共属性**

---

## 属性绘制 (Property Paint) 工具

### 工具入口

工具栏 Paint 按钮激活后，出现属性选择下拉菜单，按分类组织：
```
Painting:
  ├── Rendering
  │   ├── Texture Origin
  │   └── Y Sort Origin
  ├── Physics
  │   ├── Physics Layer 0
  │   └── ...
  └── Custom Data
      ├── health
      └── is_solid
```

### 绘制流程

1. 用户在下拉菜单选择目标属性（如 `health`）
2. 该属性的值编辑器出现（如 int SpinBox）
3. 用户设定期望值
4. 在 atlas 视图点击/拖拽 → Bresenham 线填充 → 每格写入值
5. **Ctrl+Click** → 取色器：从点击的 tile **读取**当前值到编辑器
6. **Ctrl+Shift+拖拽** → 矩形区域绘制

### 撤销/重做

鼠标释放时创建 undo/redo action，记录修改前的旧值。

### 视觉反馈（draw_over_tile）

绘制模式中每个 tile 显示当前值：
- **BOOL**: 显示勾/叉图标
- **COLOR**: 显示色块
- **数值/字符串**: 文字居中显示，带轮廓便于阅读
- 与当前画笔值**相同**的 tile 显示为淡灰色（已绘制标记）
- 选中 tile 显示选择高亮

---

## 与 tile.py 当前实现的对比

| 维度 | Godot | tile.py (当前) |
|------|-------|--------------|
| Schema 定义 | 预定义 layer（名称+固定类型） | 无 schema，自由 key-value dict |
| 类型系统 | 固定类型（创建时选择） | 自动推断（parse_value） |
| 属性存储 | 每 tile 有固定长度数组（含 NIL） | 每 tile 有动态 dict（无 key 则不存） |
| 值编辑器 | 类型对应的原生控件（SpinBox/CheckBox/ColorPicker） | 文本 Entry（纯字符串输入） |
| 属性刷 | 下拉菜单选属性 → 值编辑器 → 画布绘制 → Ctrl+拾色 | 点击属性行 → 画布绘制 |
| 视觉反馈 | 值文本覆盖显示 + 已绘制 tile 变灰 | 仅橙色悬停高亮 |
| 拾色器 | Ctrl+Click 采样 | 无 |
| 撤销 | 完整 undo/redo | 无 |
| 多选编辑 | Inspector 显示公共属性 | 不支持 |

---

## 可借鉴的改进建议

### 1. 值编辑器类型感知（高优先级）

当前所有值都用 Text Entry，改进为根据推断类型使用不同控件：
- **bool**: CheckBox 或 True/False 下拉
- **int/float**: SpinBox 
- **string**: 保持 Text Entry

降低输入错误，避免 "ture" 被当成字符串。

### 2. 属性刷视觉反馈（中优先级）

参考 Godot 的 draw_over_tile：
- 绘制模式中，在已绘制 tile 上显示属性值文字
- 与画笔值相同的 tile 用特殊颜色标记

### 3. 拾色器（低优先级）

Ctrl+Click 在属性刷模式中**采样**目标 tile 的属性值，更新画笔。

### 4. 属性值下拉预设（低优先级）

对 bool 类型提供 True/False 下拉替代文本输入，对常见 string 值提供 autocomplete。
