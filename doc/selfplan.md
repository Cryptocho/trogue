# 大背包（Grid Inventory）实现计划

## 概述

实现 Diablo 风格的网格背包系统：10×8 网格，物品按像素尺寸占用格子（武器可占多格），不可堆叠，支持使用/装备/丢弃，鼠标+键盘双操作。

## 用户需求总结

| 需求 | 答案 |
|------|------|
| 物品占格 | 武器/装备占多格，消耗品占 1 格 |
| 获取方式 | 击杀掉落 |
| 背包操作 | 使用 + 装备 + 丢弃 |
| 时间机制 | 回合制，打开背包不消耗回合 |
| UI交互 | 鼠标 + 键盘双支持 |
| 物品堆叠 | 全部不可堆叠 |

## 架构决策（对齐项目规范）

### 组件：纯数据，无逻辑

```
Inventory 组件:
{
    gridCols  = 10,       -- 背包列数
    gridRows  = 8,        -- 背包行数
    slots     = {},       -- slots[row] = {[col] = itemKey}  二维网格占用表
    items     = {},       -- {[itemKey] = {itemId, row, col}}
    nextKey   = 1,        -- 自增 key 生成器
}

Equipment 组件:
{
    slots = {             -- 装备槽映射
        main_hand = nil,  -- itemKey or nil
        off_hand  = nil,
        armor     = nil,
        helmet    = nil,
    }
}
```

### 定义：工厂函数 + builtin 注册表

```
ItemDefinition = {
    id, name, type,              -- "weapon" | "consumable" | "equipment"
    gridWidth, gridHeight,       -- 物品在背包中的占用尺寸
    weaponId,                    -- 指向 weapon.lua 定义的 ID（仅武器类）
    equipSlot,                   -- "main_hand" | "off_hand" | "armor" | "helmet"
    effectId,                    -- 使用时触发的效果 ID（仅消耗品类）
    rarity, description, icon,
}
```

### 系统：纯表 + init/update 模式

```
InventorySystem (priority=2, ECS 系统):
  - 网格操作：canPlace(), findFreeSlot(), placeItem(), removeItem()
  - 拾取：监听 PickupRequest（替代现有 PickupSystem）
  - 丢弃：监听 InventoryDropItem（物品→地面实体）
  - 使用：监听 InventoryUseItem（消耗品→触发效果）
  - 装备/卸下：监听 InventoryEquipItem / InventoryUnequipItem
  - 掉落：监听 EntityDied → 随机战利品表 → spawn 地面物品

InventoryUI (独立模块，非 ECS 系统):
  - 渲染 10×8 网格 + 装备槽 + tooltip
  - 鼠标：悬停预览尺寸/高亮、拖拽拾放、右键丢弃
  - 键盘：方向键导航、Enter 拿起/放下、E 装备、D 丢弃、U 使用
  - 持有物品=鼠标跟随渲染
```

### 事件流（对齐 Request → Process 模式）

```
InputSystem(按 I 键) → ToggleInventoryUI
InputSystem(按 P 键) → PickupRequest → InventorySystem._handlePickup()
EntityDied → InventorySystem._handleDeathDrop() → spawn ground items
InventoryUI(鼠标/键盘) → InventoryEquipItem / InventoryUnequipItem / InventoryDropItem / InventoryUseItem
```

## 实现步骤

### Step 1: 物品定义层

**文件**: `src/data/definitions/item.lua` (新建)

- `createItemDef(config)` 工厂函数
- `builtin` 注册表，MCP 期包含：
  - 武器类（引用已有 weaponId）：greatsword(2×1), shortsword(1×2), battle_axe(2×1), dagger(1×1), spear(1×2), mace(1×1), longbow(1×3), fire_wand(1×1)
  - 消耗品类（新建 effectId + consumable effect 定义）：health_potion(1×1), energy_potion(1×1)
  - 装备类（预留占位）：leather_armor(2×2), iron_helmet(2×2)

### Step 2: 组件重构

**文件**: `src/components/inventory.lua` (重写)
- 纯数据：gridCols, gridRows, slots, items, nextKey
- 移除旧的 `capacity`/`items` 简单结构

**文件**: `src/components/equipment.lua` (新建)
- 纯数据：装备槽 {main_hand, off_hand, armor, helmet}

**文件**: `src/data/prototypes/entities.lua` (修改)
- Player 原型更新 Inventory 为网格结构
- Player 原型添加 Equipment 组件

### Step 3: InventorySystem（核心）

**文件**: `src/systems/inventory_system.lua` (新建)

- `canPlace(invComp, row, col, w, h)` — 检查物品能否放入指定位置
- `findFreeSlot(invComp, w, h)` — 扫描找第一个可用位置
- `placeItem(invComp, itemId, row, col, w, h)` — 占用格子返回 itemKey
- `removeItem(invComp, itemKey)` — 释放格子
- 事件监听：
  - `PickupRequest` → 拾取地面物品（替代 PickupSystem）
  - `EntityDied` → 战利品生成（先简单实现：随机从物品池掉落 0-2 件）
  - `InventoryEquipItem` → 从背包装备到指定槽
  - `InventoryUnequipItem` → 卸下到背包
  - `InventoryDropItem` → 背包物品→地面实体
  - `InventoryUseItem` → 消耗品使用，触发效果

### Step 4: InventoryUI

**文件**: `src/systems/inventory_ui.lua` (重写)

- 渲染：
  - 背包面板（半透明深色背景，6×10 格子）
  - 每个物品用彩色矩形 + 名称缩写显示
  - 多格物品占据对应矩形区域
  - 持有物品跟随鼠标渲染
  - 右侧装备槽面板
  - 鼠标悬停 tooltip（物品名、类型、描述、武器属性）
  - 底部容量显示（已用/总共）

- 键盘：
  - 方向键移动高亮光标
  - Enter：拿起物品（如果光标在物品上）或放下物品（如果持有物品）
  - E：装备（光标处物品可装备时）
  - D：丢弃（光标处物品→地面）
  - U：使用（消耗品→触发效果）
  - I / ESC：关闭

- 鼠标：
  - 左键：拿起/放下（拖拽）
  - 右键：丢弃（物品→地面）
  - 悬停：显示 tooltip，高亮占用区域
  - 拖拽中显示物品预览
  - 点击装备槽：装备/卸下

### Step 5: 集成到 main.lua

**修改**: `src/main.lua`

- 注册 `InventorySystem`（替代 PickupSystem）
- Player 原型添加 Equipment
- 背包 UI 的 `love.draw` 调用保持现有结构
- 背包打开时：
  - `love.keypressed` → 优先发给 InventoryUI 处理键盘
  - `love.mousepressed` → 优先发给 InventoryUI 处理鼠标
  - 不消耗回合（`isInputAllowed` 不受影响）

### Step 6: 清理旧代码

- 移除 `src/systems/pickup_system.lua`（逻辑合并到 InventorySystem）
- 清理 main.lua 中旧的 PickupSystem 引用

## 文件变更清单

| 操作 | 文件 |
|------|------|
| 新建 | `src/data/definitions/item.lua` |
| 新建 | `src/systems/inventory_system.lua` |
| 新建 | `src/components/equipment.lua` |
| 重写 | `src/components/inventory.lua` |
| 重写 | `src/systems/inventory_ui.lua` |
| 修改 | `src/data/prototypes/entities.lua` |
| 修改 | `src/main.lua` |
| 删除 | `src/systems/pickup_system.lua` |

## 不做（本次）

- 物品在 tileset 中的图标（用纯色矩形替代）
- 装备对 Stats computed 的实际修改（预留管道）
- 战利品表系统（先用随机函数代替）
- 物品排序/整理
- 背包扩容机制
