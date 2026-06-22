# Tileset 格式规范

本文件定义 tile.py 导出 Lua 的精确格式，MapRenderer 直接消费该格式。

---

## 1. 文件定位

Tileset 定义文件存放于游戏 `assets/` 目录下，与源图片同名，扩展名为 `.lua`。

- 图片路径：`assets/<name>.png`
- 定义路径：`assets/<name>.lua`

MapRenderer 初始化时扫描 `assets/*.lua`，加载所有 tileset 定义并建立注册表。

---

## 2. 顶层字段

Lua 模块返回一个单层表，包含以下字段：

| 字段 | 类型 | 含义 | 示例值 |
|------|------|------|--------|
| `source` | string | 源图片相对路径（相对于 `assets/`） | `"tileset.png"` |
| `cols` | int | 源图片横向 tile 数 | `8` |
| `rows` | int | 源图片纵向 tile 数 | `4` |
| `tile_width` | int | 单个 tile 像素宽 | `16` |
| `tile_height` | int | 单个 tile 像素高 | `16` |
| `padding` | int | tile 间距（像素） | `0` |
| `count` | int | 有效 tile 总数 | `24` |

---

## 3. tiles 数组

`tiles` 是一个以 tile index 为键的数组（Lua table），格式：

```lua
tiles = {
    [index] = {
        col = 0,         -- 源图片列坐标（0-based）
        row = 0,         -- 源图片行坐标（0-based）
        properties = {}, -- 可选，键值对表
        bitmask = {},    -- 可选，3×3 数组，值 0/1/2
    },
}
```

### 3.1 `col` / `row`

源图片中的 tile 坐标（0-based），用于 quad 裁剪。

### 3.2 `properties`

可选键值对表，key 为字符串，value 可以是 string / number / boolean。

```lua
properties = {
    solid = true,
    terrain = "grass",
    walkCost = 2,
}
```

常见 property 键：
- `solid`：boolean，true 表示不可通行

### 3.3 `bitmask`

可选 3×3 数组，描述 Autotile 场景掩码。值为：
- `0` = Off
- `1` = On
- `2` = Ignore

中心位 `(1,1)` 必须为 `1`（On），否则该瓦片永远不会被引擎选用。

```lua
bitmask = {
    {0, 0, 0},
    {0, 1, 0},
    {0, 0, 0},
}
```

---

## 4. groups 表

`groups` 用于将多个 tile 归为一组，引擎按组进行 Autotile 匹配：

```lua
groups = {
    [groupName] = {
        tiles = { index1, index2, ... },
    },
}
```

同一 tile 可属于多个组。

---

## 5. Autotile 规则

引用 `doc/bitmask_autotile.md` 第 4.3 节。

**全部 group 默认启用 Autotile**，引擎按以下流程匹配：

1. 读取 TileMap 当前格子，扫描周围 8 邻域，生成**场景掩码**
2. 遍历该 group 内所有瓦片的预设 Bitmask：
   - 对比所有 On/Off 位，必须**全部严格吻合**；Ignore 位跳过
   - 中心位必须 On，否则跳过
3. 多个瓦片匹配成功：按 Priority 权重随机选一张
4. 无任何匹配：使用该 group 的 Icon 兜底瓦片（组内第一个 tile）

---

## 6. MapRenderer 消费流程

```
MapRenderer:init()
  → TilesetLoader.loadAllTilesets()  扫描 assets/*.lua
  → 对每个有效文件：
      → dofile 加载 Lua 表
      → love.graphics.newImage(source) 加载图片
      → 遍历 tiles 表，按 col/row 创建 love.graphics.newQuad
      → 注册到 tilesetRegistry[source]
  → 选第一个 tileset 作为 activeTileset
  → self.quads = activeTileset.quads
  → self.tileset = activeTileset.image

MapRenderer:draw()
  → 遍历可见 tile
  → 查 self.quads[tileIndex]
  → 查 activeTileset.definition.tiles[tileIndex].properties.solid
  → 绘制 quad 或 fallback

MapRenderer:isSolid(x, y)
  → 查 tileIndex
  → 优先读 activeTileset.definition.tiles[tileIndex].properties.solid
  → 无 tileset 或该 tile 无 properties 时 fallback 到 tileIndex == 1 or tileIndex == 8
```

---

## 7. 完整示例

```lua
return {
    source = "tileset.png",
    cols = 8,
    rows = 4,
    tile_width = 16,
    tile_height = 16,
    padding = 0,
    count = 24,

    tiles = {
        [0] = { col = 0, row = 0, properties = { terrain = "grass" } },
        [1] = { col = 1, row = 0, properties = { solid = true } },
        [2] = {
            col = 2, row = 0,
            properties = { solid = true },
            bitmask = {
                {0, 1, 0},
                {1, 1, 1},
                {0, 1, 0},
            },
        },
    },

    groups = {
        wall = {
            tiles = { 1, 2 },
        },
        floor = {
            tiles = { 0 },
        },
    },
}
```
