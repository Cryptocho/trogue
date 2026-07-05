# Trogue TileSet Editor — 开发计划

## 概述

独立的 TileSet/Map 编辑器，使用 C++ + SDL3 + Dear ImGui 实现。
编辑器输出 Lua 数据文件，供 Trogue (LÖVE2D) 游戏加载。

## 技术栈

- **语言**: C++17（C 风格，ImGui 要求 C++）
- **窗口**: SDL3 3.4.10
- **GUI**: Dear ImGui（SDL3 + SDLRenderer3 后端）
- **图片加载**: SDL3_image（PNG）
- **构建**: CMake 3.20+
- **输出**: Lua 5.x table 格式
- **CMake 目标**: `SDL3::SDL3-shared`, `SDL3_image::SDL3_image-shared`, `imgui::imgui`

## 核心需求

1. **TileSet 管理**: 加载多个 .png 纹理资源，从纹理中选取区域创建 tile
2. **非方形 tile**: 支持多格 tile（如树木占 2 格，以下方格为放置基准）
3. **属性标记**: 给 tile 标记属性（wall、floor、occlusion 等）
4. **遮蔽标记**: 对异形 tile 的特定区域标记遮蔽（玩家在被遮蔽区域时不显示，类似 Z-order）
5. **场景地图编辑**: 使用 tile 绘制游戏场景地图
6. **Lua 导出**: 导出 TileSet 和 Map 数据为 Lua table 文件

## Godot 参考源码

所有阶段的实现均参考 Godot 引擎的 TileSet 编辑器源码（位于 `temp/godot/`）。
详细分析文档见 `temp/godot-tileset-editor-code.md`。

关键参考文件：

| 文件 | 内容 | 参考阶段 |
|------|------|---------|
| `temp/godot/scene/resources/2d/tile_set.h` | TileSet/TileSetSource/TileSetAtlasSource/TileData 数据结构 | 阶段 2 |
| `temp/godot/scene/resources/2d/tile_set.cpp` | 数据模型实现（ID 生成、源管理、地形、代理） | 阶段 2, 5 |
| `temp/godot/editor/scene/2d/tiles/tile_set_editor.h/.cpp` | TileSet 主编辑器 Dock（源列表、Tab、图案） | 阶段 4, 5, 6 |
| `temp/godot/editor/scene/2d/tiles/tile_set_atlas_source_editor.h/.cpp` | 图集源编辑器（最复杂：17 种拖拽类型、属性绘制树、tile 创建/删除/调整） | 阶段 4, 5, 6, 7 |
| `temp/godot/editor/scene/2d/tiles/tile_data_editors.h/.cpp` | TileData 属性编辑器类层次（碰撞/遮挡/导航/地形多边形编辑器） | 阶段 6, 7 |
| `temp/godot/editor/scene/2d/tiles/tile_atlas_view.h/.cpp` | 图集视图组件（缩放/平移/网格/替代瓦片布局） | 阶段 4 |
| `temp/godot/editor/scene/2d/tiles/tile_map_layer_editor.h/.cpp` | TileMapLayer 画布编辑器（tile/地形绘制、桶填、散布） | 阶段 8 |
| `temp/godot/editor/scene/2d/tiles/tiles_editor_plugin.h/.cpp` | 插件注册 + TilesEditorUtils 全局单例（跨编辑器同步、图案预览线程） | 全局架构 |
| `temp/godot/editor/scene/2d/tiles/tile_proxies_manager_dialog.h/.cpp` | Tile 代理管理（三级代理映射：源级/坐标级/替代级） | — |
| `temp/godot/editor/scene/2d/tiles/atlas_merging_dialog.h/.cpp` | 图集合并工具（多源合并为单纹理） | — |

### Godot 核心设计模式（本编辑器复用）

1. **ID 生成**: `while (container.has(next_id)) { next_id = (next_id + 1) % MODULUS }`，删除后留空洞不回收
2. **数据结构**: 纯数据 struct（无 OOP 继承），`TileSet` → `TileSetSource[]` → `TileData` 三层结构
3. **代理对象**: `AtlasTileProxyObject` 包装 tile 数据供 Inspector 编辑（本编辑器直接用 ImGui 绑定，不需要代理）
4. **延迟更新**: `tile_set_changed_needs_update` 标记 + `NOTIFICATION_INTERNAL_PROCESS` 批量刷新
5. **拖拽类型枚举**: Godot 用 17 种 `DragType` 区分不同交互，本编辑器简化为创建/删除/选择/调整大小 4 种
6. **多格 tile**: `sizeInAtlas` 记录占据格数，`textureOffset` 处理像素偏移，放置基准以下方格为准

## 项目结构

```
tools/editor/
├── CMakeLists.txt
├── PLAN.md
└── src/
    ├── main.cpp           # 入口 + 主循环 + ImGui 初始化
    └── (后续阶段逐步添加)
```

纹理资源位于 `src/assets/`（项目根目录），编辑器从项目根目录的绝对路径或相对于可执行文件的路径加载。

现有可用素材：
- `src/assets/tileset.png` — 当前主 tileset
- `src/assets/TopDownFantasy-Forest/Tiles/Tileset.png` — 森林 tileset
- `src/assets/TopDownFantasy-Forest/Tiles/Tileset1xPadding.png` — 森林 tileset（带 padding）
- `src/assets/TopDownFantasy-Forest/Decorations/Decorations.png` — 装饰物
- `src/assets/pixel-set-library/dungen-tile/Tile Set.png` — 地牢 tileset

---

## 阶段 1: 最小可运行窗口

**目标**: 空白 SDL3 窗口 + ImGui 渲染循环

### 1.1 CMake 构建验证

- 影响的文件: `CMakeLists.txt`, `src/main.cpp`
- `src/main.cpp` 创建一个空的 `main()` 函数返回 0
- CMakeLists.txt 使用正确的 cmake 目标：`SDL3::SDL3-shared`, `SDL3_image::SDL3_image-shared`, `imgui::imgui`
- 运行 `cmake -B build && cmake --build build` 验证编译通过
- **验证**: `build/trogue-tileset-editor` 存在，`ldd build/trogue-tileset-editor` 显示 libSDL3、libimgui 已链接

### 1.2 SDL3 窗口创建

- 影响的文件: `src/main.cpp`
- 调用 `SDL_Init(SDL_INIT_VIDEO)` 初始化 SDL3
- 使用 `SDL_CreateWindow("Trogue TileSet Editor", 1280, 720, 0)` 创建窗口
- 使用 `SDL_CreateRenderer(window, NULL)` 创建渲染器（让 SDL3 自动选择最佳后端）
- 设置 `SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1")` 启用垂直同步
- 主循环：`SDL_PollEvent` + `SDL_RenderClear` + `SDL_RenderPresent`
- 处理 `SDL_EVENT_QUIT` 和 `SDL_EVENT_KEY_DOWN`（ESC 键）退出
- 退出时调用 `SDL_DestroyRenderer` + `SDL_DestroyWindow` + `SDL_Quit()`
- **验证**: 窗口弹出，显示黑色背景，ESC 或关闭窗口正常退出，无内存泄漏

### 1.3 ImGui 初始化

- 影响的文件: `src/main.cpp`
- 调用 `IMGUI_CHECKVERSION()` + `ImGui::CreateContext()`
- 设置 `ImGui::GetIO().IniFilename = nullptr`（避免生成 imgui.ini 文件）
- 调用 `ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)`
- 调用 `ImGui_ImplSDLRenderer3_Init(renderer)`
- 主循环中：
  - `ImGui_ImplSDL3_NewFrame()` + `ImGui_ImplSDLRenderer3_NewFrame()` + `ImGui::NewFrame()`
  - `ImGui::ShowDemoWindow()` （临时验证 ImGui 工作）
  - `ImGui::Render()` + `ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData())`
- 退出时调用 `ImGui_ImplSDLRenderer3_Shutdown()` + `ImGui_ImplSDL3_Shutdown()` + `ImGui::DestroyContext()`
- **验证**: 窗口中显示 ImGui Demo 窗口，可交互，关闭无崩溃

---

## 阶段 2: 数据模型

**目标**: 定义编辑器核心数据结构
**Godot 参考**: `temp/godot/scene/resources/2d/tile_set.h` — TileSet 类层次（行 149-1044）

### 2.1 TileSet 数据结构

- 影响的文件: `src/tileset.hpp`（新建）
- 定义 `struct TileSetSource`：
  - `std::string name` — 源名称
  - `std::string texturePath` — .png 文件路径（相对于项目根目录）
  - `std::array<int,2> margins = {0, 0}` — 纹理边距
  - `std::array<int,2> separation = {0, 0}` — tile 间距
  - `std::array<int,2> regionSize = {16, 16}` — 每个区域大小
- 定义 `struct TileSet`：
  - `std::string name` — TileSet 名称
  - `int tileWidth = 16, tileHeight = 16` — 每个 tile 的像素大小
  - `std::vector<TileSetSource> sources` — 纹理源列表
  - `std::vector<TileData> tiles` — 已创建的 tile 列表
- **验证**: 在 main.cpp 中 `#include "tileset.hpp"`，构造 `TileSet ts; ts.name = "test";`，编译通过

### 2.2 TileData 数据结构

- 影响的文件: `src/tileset.hpp`
- 定义 `struct OcclusionRegion`（必须在 TileData 之前定义）：
  - `int x = 0, y = 0, w = 0, h = 0` — 相对于 tile 的矩形（像素坐标），使用命名字段便于 Lua 导出
  - `int zOrder = 0` — 遮蔽优先级（数值越大越优先遮蔽）
- 定义 `struct TileData`：
  - `int id = -1` — tile 在 TileSet 中的唯一 ID（生成规则：`max(existing ids) + 1`，删除后留空洞，不回收）
  - `int sourceIndex = 0` — 所属源索引
  - `std::array<int,2> atlasCoords = {0, 0}` — 在图集中的坐标 [col, row]
  - `std::array<int,2> sizeInAtlas = {1, 1}` — 在图集中占的格数 [w, h]
  - `std::array<int,2> textureOffset = {0, 0}` — 纹理像素偏移
  - `bool flipH = false, flipV = false, transpose = false` — 变换
  - `bool isWall = false` — 是否为墙（不可通行）
  - `std::string placementAnchor = "bottom"` — 放置基准点（"bottom" 或 "top"），多格 tile 以下方格为基准
  - `std::vector<OcclusionRegion> occlusionRegions` — 遮蔽区域列表
- **验证**: 构造 `TileData td; td.isWall = true; td.sizeInAtlas = {1, 2};`，编译通过

### 2.3 Map 数据结构

- 影响的文件: `src/tileset.hpp`
- 定义 `struct MapCell`：
  - `int tileSetIndex = -1` — 使用哪个 TileSet（-1 = 空）
  - `int tileId = -1` — tile 在 TileSet.tiles 中的 ID
- 定义 `struct GameMap`：
  - `std::string name` — 地图名称
  - `int width = 60, height = 60` — 地图尺寸（tile 数）
  - `std::vector<MapCell> cells` — 行优先存储：`cells[row * width + col]`
  - `std::vector<TileSet> tileSets` — 使用的 TileSet 列表
- cells 使用 row-major 顺序：`cells[row * width + col]`
- `MapCell.tileSetIndex` 索引到 `GameMap.tileSets[]`（地图自己的列表，非全局）
- `MapCell.tileId` 索引到对应 TileSet 的 `tiles[]` 数组
- **验证**: 构造 `GameMap m; m.cells.resize(60*60);`，编译通过

---

## 阶段 3: 纹理加载与显示

**目标**: 加载 PNG 纹理并在 ImGui 中显示

### 3.1 纹理加载函数

- 影响的文件: `src/texture_loader.hpp`（新建）, `src/texture_loader.cpp`（新建）, `CMakeLists.txt`
- 将 `.cpp` 添加到 CMakeLists.txt 的 `add_executable`
- 实现 `SDL_Texture* loadTexture(SDL_Renderer*, const char* path)`
- 使用 `IMG_Load(path)` 加载 PNG 为 `SDL_Surface*`
- 使用 `SDL_CreateTextureFromSurface(renderer, surface)` 转换为 `SDL_Texture*`
- 加载失败时返回 nullptr 并用 `SDL_LogError()` 打印错误
- **验证**: 在 main.cpp 中加载 `src/assets/tileset.png`，检查返回值非 nullptr，`SDL_Log` 输出纹理尺寸

### 3.2 ImGui 纹理显示

- 影响的文件: `src/texture_loader.hpp`, `src/texture_loader.cpp`
- 实现内联函数：
  ```cpp
  inline ImTextureID toImTextureID(SDL_Texture* tex) {
      return (ImTextureID)(intptr_t)tex;
  }
  ```
- 使用 `SDL_GetTextureSize(tex, &w, &h)` 获取纹理尺寸（SDL3 返回 float）
- 在 ImGui 窗口中用 `ImGui::Image(toImTextureID(tex), ImVec2(w, h))` 显示
- **验证**: 窗口中正确显示 PNG 图片，尺寸与原始纹理一致

### 3.3 默认资源加载与路径解析

- 影响的文件: `src/texture_loader.hpp`, `src/texture_loader.cpp`
- 实现 `std::string resolveProjectRoot()` — 从可执行文件路径向上查找包含 `src/assets/` 的目录
- 默认加载 `{projectRoot}/src/assets/tileset.png`
- 编辑器启动时自动加载该文件
- 加载失败时在 ImGui 窗口中显示错误信息而非崩溃
- 纹理生命周期：退出时调用 `SDL_DestroyTexture()` 清理
- **验证**: 从 `build/` 目录运行编辑器，自动找到并加载 `src/assets/tileset.png`

---

## 阶段 4: 图集视图（Atlas View）

**目标**: 在 ImGui 中以可缩放/平移的画布显示纹理，并叠加网格
**Godot 参考**: `temp/godot/editor/scene/2d/tiles/tile_atlas_view.h:43-166` — TileAtlasView 类定义（缩放/平移/网格/替代瓦片布局）
**Godot 参考**: `temp/godot/editor/scene/2d/tiles/tile_atlas_view.cpp:88-96` — `_draw_base_tiles()` 纹理绘制 + 材质分离
**Godot 参考**: `temp/godot/editor/scene/2d/tiles/tile_set_atlas_source_editor.cpp:973` — `_update_atlas_view()` 图集视图更新逻辑

### 4.1 编辑器状态结构

- 影响的文件: `src/editor_state.hpp`（新建）
- 定义 `struct EditorState`：
  - `TileSet tileSet` — 当前编辑的 TileSet
  - `SDL_Texture* atlasTexture = nullptr` — 当前图集纹理
  - `float atlasZoom = 1.0f` — 图集缩放
  - `ImVec2 atlasPan = {0, 0}` — 图集平移
  - `int selectedTileId = -1` — 选中的 tile ID
  - `int hoveredCol = -1, hoveredRow = -1` — 鼠标悬停的 tile 坐标
  - `GameMap map` — 当前编辑的地图
  - `float mapZoom = 1.0f` — 地图编辑器缩放
  - `ImVec2 mapPan = {0, 0}` — 地图编辑器平移
  - `int hoveredMapCol = -1, hoveredMapRow = -1` — 地图悬停坐标
- 在 main.cpp 中构造 `EditorState state;`
- **验证**: 编译通过，EditorState 可在 main 中构造和访问

### 4.2 图集画布基础显示

- 影响的文件: `src/atlas_view.hpp`（新建）, `src/atlas_view.cpp`（新建）, `CMakeLists.txt`
- 实现 `void drawAtlasView(EditorState& state)`
- 使用 `ImGui::Begin("Atlas View")` 创建窗口
- 使用 `ImGui::GetWindowDrawList()` 获取绘制列表
- 使用 `draw_list->AddImage(toImTextureID(state.atlasTexture), p_min, p_max)` 绘制纹理
- 纹理以 `state.atlasZoom` 缩放，以 `state.atlasPan` 偏移
- **验证**: Atlas View 窗口中显示纹理，初始 1x 大小

### 4.3 缩放与平移

- 影响的文件: `src/atlas_view.hpp`, `src/atlas_view.cpp`
- 鼠标滚轮缩放：`state.atlasZoom += io.MouseWheel * 0.1f`，范围 [0.25, 16.0]
- 鼠标中键拖拽平移：`state.atlasPan += io.MouseDelta`
- 缩放以鼠标位置为中心（缩放后调整 pan 保持鼠标下的点不变）
- **验证**: 滚轮缩放纹理，中键拖拽平移，操作流畅

### 4.4 网格叠加

- 影响的文件: `src/atlas_view.hpp`, `src/atlas_view.cpp`
- 根据 `state.tileSet.tileWidth`, `tileHeight`, `source.margins`, `source.separation`, `source.regionSize` 计算网格
- 使用 `draw_list->AddLine()` 绘制网格线，颜色 `ImVec4(0.5, 0.5, 0.5, 0.3)`
- 网格线跟随缩放/平移变换
- **验证**: 纹理上正确显示 tile 网格，网格与 tile 边界对齐

### 4.5 坐标转换与悬停提示

- 影响的文件: `src/atlas_view.hpp`, `src/atlas_view.cpp`
- 实现 `bool screenToAtlasCoords(EditorState& state, ImVec2 screenPos, int& outCol, int& outRow)`
  - 需要考虑：窗口偏移、zoom、pan、margins、separation、regionSize
- 实现 `ImVec2 atlasCoordsToScreen(EditorState& state, int col, int row)`
- 鼠标悬停时在 Atlas View 底部用 `ImGui::Text()` 显示 `(col, row)` 坐标
- **验证**: 鼠标悬停不同 tile 时，底部文字正确显示坐标

---

## 阶段 5: Tile 创建与选择

**目标**: 从图集纹理中创建 tile 并选中
**Godot 参考**: `temp/godot/editor/scene/2d/tiles/tile_set_atlas_source_editor.h:163-186` — DragType 枚举（17 种拖拽类型，本编辑器简化为 4 种）
**Godot 参考**: `temp/godot/editor/scene/2d/tiles/tile_set_atlas_source_editor.cpp:1092` — `_tile_atlas_control_gui_input()` 鼠标交互主入口
**Godot 参考**: `temp/godot/editor/scene/2d/tiles/tile_set_atlas_source_editor.cpp:1395` — `_end_dragging()` 拖拽结束处理（UndoRedo 提交）

### 5.1 点击创建 Tile

- 影响的文件: `src/atlas_view.hpp`, `src/atlas_view.cpp`, `src/tileset.hpp`
- 在图集视图中左键点击空区域 → 创建新 `TileData`，分配唯一 `id`
- 新 tile 默认 `sizeInAtlas = {1, 1}`，`isWall = false`
- 使用 `draw_list->AddRect()` 绘制已创建 tile 的绿色边框
- 选中该新创建的 tile
- **验证**: 点击图集网格中的空格子，出现绿色边框，右侧面板显示新 tile 信息

### 5.2 选择已有 Tile

- 影响的文件: `src/atlas_view.hpp`, `src/atlas_view.cpp`
- 左键点击已有 tile → 选中（`state.selectedTileId = tile.id`）
- 选中的 tile 用白色边框高亮
- 未选中的 tile 保持绿色边框
- **验证**: 点击不同 tile 切换选中状态，白色边框跟随

### 5.3 多格 Tile 创建（拖拽）

- 影响的文件: `src/atlas_view.hpp`, `src/atlas_view.cpp`
- 左键拖拽创建大于 1x1 的 tile
- 拖拽过程中用半透明蓝色矩形预览选择区域
- 释放鼠标后创建 `TileData`，设置 `sizeInAtlas` 为拖拽区域大小
- 放置基准点：`placementAnchor = "bottom"`，即 `(atlasCoords)` 是底部格，tile 向上延伸
- **验证**: 拖拽创建 1x2 tile，图集中正确显示占据两个格子，下方为基准

### 5.4 右键删除 Tile

- 影响的文件: `src/atlas_view.hpp`, `src/atlas_view.cpp`
- 右键点击 tile → 弹出 `ImGui::BeginPopupContextItem()` 上下文菜单
- 菜单项：`Delete`
- 删除后从 `state.tileSet.tiles` 中移除，清空选择
- **验证**: 右键删除 tile，绿色边框消失，数据中移除

---

## 阶段 6: 属性标记面板

**目标**: 右侧 Inspector 面板，编辑选中 tile 的属性
**Godot 参考**: `temp/godot/editor/scene/2d/tiles/tile_set_atlas_source_editor.cpp:626` — `_update_tile_data_editors()` 构建属性绘制工具树（Rendering/Terrains/Physics/Navigation/CustomData 分组）
**Godot 参考**: `temp/godot/editor/scene/2d/tiles/tile_data_editors.h:193-249` — TileDataDefaultEditor（通用属性绘制编辑器基类，带 paint/picker 支持）

### 6.1 Inspector 面板基础

- 影响的文件: `src/inspector.hpp`（新建）, `src/inspector.cpp`（新建）, `CMakeLists.txt`
- 使用 `ImGui::Begin("Inspector")` 创建右侧面板
- 显示选中 tile 的只读信息：ID、atlasCoords、sizeInAtlas、所属 source
- 未选中时显示 "No tile selected"
- **验证**: 选中 tile 后，Inspector 显示正确的 tile 信息

### 6.2 布尔属性编辑

- 影响的文件: `src/inspector.hpp`, `src/inspector.cpp`
- 使用 `ImGui::Checkbox("Is Wall", &tile.isWall)` 标记墙体
- 使用 `ImGui::Checkbox("Flip H/V", &tile.flipH/flipV)` 变换
- 属性修改后实时生效（下一帧图集视图更新边框颜色）
- 墙体 tile 边框变为红色（区分普通 tile 的绿色）
- **验证**: 勾选 isWall，图集视图中该 tile 边框变为红色

### 6.3 多格 Tile 属性

- 影响的文件: `src/inspector.hpp`, `src/inspector.cpp`
- 选中多格 tile 时，用 `ImGui::DragInt2("Size", tile.sizeInAtlas.data())` 编辑大小
- 用 `ImGui::Combo("Anchor", &anchorIndex, "Bottom\0Top\0")` 编辑放置基准
- 修改后图集视图中边框大小实时更新
- **验证**: 修改 sizeInAtlas，图集中边框正确更新

---

## 阶段 7: 遮蔽标记系统

**目标**: 对异形 tile 的特定区域进行遮蔽标记
**Godot 参考**: `temp/godot/editor/scene/2d/tiles/tile_data_editors.h:272-300` — TileDataOcclusionShapeEditor（遮挡多边形编辑器，含 GenericTilePolygonEditor）
**Godot 参考**: `temp/godot/editor/scene/2d/tiles/tile_data_editors.h:87-191` — GenericTilePolygonEditor（通用多边形点编辑工具，支持吸附/缩放/旋转/翻转）
**Godot 参考**: `temp/godot/scene/resources/2d/tile_set.h:862-869` — TileData 的 `OcclusionLayerTileData` 数据结构（多边形遮挡器）
**简化说明**: Godot 使用任意多边形（`OccluderPolygon2D`）进行遮挡编辑，本编辑器简化为轴对齐矩形（`OcclusionRegion {x, y, w, h}`）。对于 2D tile-based 游戏，矩形遮蔽足够覆盖绝大多数场景（树木、建筑等），且实现复杂度大幅降低。

### 7.1 遮蔽区域列表

- 影响的文件: `src/inspector.hpp`, `src/inspector.cpp`
- Inspector 中添加 "Occlusion Regions" 折叠区域（`ImGui::TreeNodeEx`）
- 每个遮蔽区域显示 4 个 `ImGui::DragInt`（x, y, w, h）+ zOrder
- `ImGui::Button("+")` 添加新遮蔽区域（默认覆盖整个 tile）
- 每个区域旁 `ImGui::Button("X")` 删除
- **验证**: 添加/删除遮蔽区域，数据正确存储在 `TileData.occlusionRegions`

### 7.2 遮蔽区域可视化

- 影响的文件: `src/atlas_view.hpp`, `src/atlas_view.cpp`
- 在图集视图中，用 `draw_list->AddRectFilled()` 绘制半透明红色矩形
- 遮蔽区域坐标相对于 tile 的纹理原点绘制
- 选中的遮蔽区域用更高亮度（alpha 0.4 vs 0.2）
- **验证**: 在图集中可以看到遮蔽区域的红色半透明覆盖

### 7.3 遮蔽区域选中

- 影响的文件: `src/atlas_view.hpp`, `src/atlas_view.cpp`
- 点击遮蔽区域选中它（Inspector 中高亮对应条目）
- 使用 `draw_list->AddRect()` 在选中区域绘制蓝色边框
- **验证**: 点击不同遮蔽区域，Inspector 中对应条目高亮

### 7.4 遮蔽区域拖拽调整大小

- 影响的文件: `src/atlas_view.hpp`, `src/atlas_view.cpp`
- 选中遮蔽区域后，显示 4 个边框调整手柄（上/下/左/右）
- 拖拽手柄调整遮蔽区域大小
- 大小实时更新到 `OcclusionRegion` 的 x/y/w/h
- **验证**: 拖拽手柄调整大小，Inspector 中数值同步更新

---

## 阶段 8: 地图编辑器

**目标**: 使用 TileSet 绘制游戏场景地图
**Godot 参考**: `temp/godot/editor/scene/2d/tiles/tile_map_layer_editor.h:94-272` — TileMapLayerEditorTilesPlugin（tile 绘制子编辑器：select/paint/line/rect/bucket 工具、散布率、随机 tile、变换）
**Godot 参考**: `temp/godot/editor/scene/2d/tiles/tile_map_layer_editor.cpp:1069` — `_draw_line()` 划线算法（Bresenham 线 → 生成要修改的 cells）
**Godot 参考**: `temp/godot/editor/scene/2d/tiles/tile_map_layer_editor.cpp:1116` — `_draw_rect()` 画矩形区域
**Godot 参考**: `temp/godot/editor/scene/2d/tiles/tile_map_layer_editor.cpp:1172` — `_draw_bucket_fill()` 桶状填充（支持连续/非连续模式）

### 8.1 地图网格视图

- 影响的文件: `src/map_editor.hpp`（新建）, `src/map_editor.cpp`（新建）, `CMakeLists.txt`
- 使用 `ImGui::Begin("Map Editor")` 创建新窗口
- 显示空的 tile 网格（默认 60x60）
- 使用 `draw_list->AddLine()` 绘制网格线
- 支持缩放/平移（与图集视图相同的交互模式，独立的 zoom/pan 状态）
- **验证**: Map Editor 窗口中显示空白网格

### 8.2 Tile 选择与放置

- 影响的文件: `src/map_editor.hpp`, `src/map_editor.cpp`
- 从 Atlas View 选中 tile 后（`state.selectedTileId`），在 Map Editor 中左键放置
- 放置时在鼠标位置绘制 tile 纹理预览
- 写入 `state.map.cells[row * width + col]`
- 支持拖拽连续放置
- **验证**: 选中 tile，左键点击地图放置，tile 纹理正确显示

### 8.3 多格 Tile 放置

- 影响的文件: `src/map_editor.hpp`, `src/map_editor.cpp`
- 放置多格 tile 时，以 `placementAnchor` 决定基准：
  - "bottom": 点击的格子是底部，tile 向上占据 `sizeInAtlas.h - 1` 格
  - "top": 点击的格子是顶部，tile 向下占据 `sizeInAtlas.h - 1` 格
- 放置前检查所有占据的格子是否为空
- 如果有格子已被占据，显示红色半透明预览（不允许放置）
- 放置成功后，所有占据的格子都写入相同的 `tileId`
- **验证**: 放置 1x2 树木 tile，占据 2 格，下方为基准；已有 tile 位置显示红色

### 8.4 Tile 删除与替换

- 影响的文件: `src/map_editor.hpp`, `src/map_editor.cpp`
- 右键点击地图上的 tile → 删除（恢复为空 cell）
- 删除多格 tile 时：查找该 tile 的 `sizeInAtlas`，从点击的基准格推算出所有占据的格子（根据 `placementAnchor` 向上或向下延伸），全部清空
- 左键点击已有 tile 位置 → 替换为当前选中的 tile（先删除旧 tile，再放置新 tile）
- **验证**: 右键删除、放置替换正常工作

### 8.5 地图尺寸编辑

- 影响的文件: `src/map_editor.hpp`, `src/map_editor.cpp`, `src/editor_state.hpp`
- Inspector 中添加地图属性：name（`ImGui::InputText`）、width/height（`ImGui::DragInt`）
- 修改尺寸时重新分配 cells 数组（保留重叠区域数据）
- **验证**: 修改地图尺寸，网格正确更新

---

## 阶段 9: Lua 导出

**目标**: 将 TileSet 和 Map 数据导出为 Lua table 文件
**Godot 参考**: `temp/godot/scene/resources/2d/tile_set.cpp` — `_get_property_list()` / `_set()` 序列化机制（了解 TileData 中哪些字段需要序列化）
**Godot 参考**: `temp/godot/scene/resources/2d/tile_set.h:846-1036` — TileData 完整字段列表（确保导出不遗漏）

### 9.1 TileSet 导出

- 影响的文件: `src/export_lua.hpp`（新建）, `src/export_lua.cpp`（新建）, `CMakeLists.txt`
- 实现 `bool exportTileSet(const TileSet& ts, const char* path)`
- 输出格式：
```lua
return {
    tileWidth = 16,
    tileHeight = 16,
    sources = {
        {
            name = "forest",
            texture = "src/assets/TopDownFantasy-Forest/Tiles/Tileset.png",
            margins = {0, 0},
            separation = {0, 0},
            regionSize = {16, 16},
        },
    },
    tiles = {
        [0] = {sourceIndex = 0, atlasCoords = {0, 0}, sizeInAtlas = {1, 1}, isWall = false, flipH = false, flipV = false, transpose = false, textureOffset = {0, 0}},
        [1] = {sourceIndex = 0, atlasCoords = {1, 0}, sizeInAtlas = {1, 2}, isWall = false, flipH = false, flipV = false, transpose = false, textureOffset = {0, 0},
            placementAnchor = "bottom",
            occlusionRegions = {{x = 0, y = 0, w = 16, h = 8, zOrder = 1}},
        },
    },
}
```
- texture 路径使用相对于项目根目录的路径
- tile 的 `id` 字段不写入表内——它作为 Lua table 的键名隐式表示（`[0] = {...}` 中的 `0` 即为 id）
- **验证**: 导出的 .lua 文件可用 `dofile()` 加载，`lua -e "dofile('test.lua')"` 无错误

### 9.2 Map 导出

- 影响的文件: `src/export_lua.hpp`, `src/export_lua.cpp`
- 实现 `bool exportMap(const GameMap& map, const char* path)`
- 注意：`GameMap.tileSets` 在内存中是 `std::vector<TileSet>` 对象，但导出时只写入路径引用（需要先单独导出 TileSet 文件，再在 Map 中引用路径）
- 输出格式：
```lua
return {
    name = "forest_01",
    width = 60,
    height = 60,
    tileSets = {
        "src/data/tilesets/forest.lua",
    },
    cells = {
        -- 1-based 索引: cells[(row * width + col) + 1]
        -- 只写入非空 cell
        [1] = {tileSetIndex = 0, tileId = 0},
        [61] = {tileSetIndex = 0, tileId = 1},
    },
}
```
- cells 使用 1-based 索引（Lua 惯例），空 cell 不写入
- **验证**: 导出的 .lua 文件可用 `dofile()` 加载

### 9.3 导出菜单

- 影响的文件: `src/main.cpp`
- 菜单栏：`ImGui::BeginMainMenuBar()`
  - `File` → `Export TileSet...` / `Export Map...`
- 文件保存使用 `SDL_ShowSaveFileDialog()`（SDL3 异步回调）
  - 回调签名：`void callback(void* userdata, const char* const* filelist, int filter)`
  - 在回调中调用 `exportTileSet()` 或 `exportMap()`
  - 注意：SDL3 的文件对话框回调在大多数平台上运行在主线程，但为安全起见，回调中只设置一个标志（`state.pendingExportPath = filelist[0]`），在主循环下一帧检测到标志后执行实际导出
- 导出成功/失败在下一帧用 `ImGui::OpenPopup()` 显示提示
- **验证**: 通过菜单导出，文件正确生成

---

## 阶段 10: 游戏集成

**目标**: Trogue 游戏代码修改以加载编辑器导出的数据
**Godot 参考**: `temp/godot/scene/resources/2d/tile_set.h:846-1036` — TileData 运行时数据结构（渲染/物理/地形/导航/自定义数据完整字段）
**Godot 参考**: `temp/godot/scene/2d/tile_map_layer.h:381-402` — TileMapLayer 类核心数据成员（tile_set 引用、layer 索引、rendering/quadrant 设置）
**Godot 参考**: `temp/godot/scene/2d/tile_map_layer.cpp:291-350` — `_rendering_update()` 渲染更新（遍历 cell → 绘制 tile 纹理）

### 10.1 TileSet 数据加载

- 影响的文件: `src/data/tilesets.lua`（新建）, `src/systems/map_renderer.lua`
- 新建 `src/data/tilesets/` 目录存放编辑器导出的 TileSet 文件
- `src/data/tilesets.lua` 提供加载函数：`Tilesets.load(path) → tileset table`
- **验证**: `dofile()` 加载导出的 TileSet 文件无错误

### 10.2 Map 数据加载

- 影响的文件: `src/data/maps.lua`（新建）, `src/systems/map_renderer.lua`
- 新建 `src/data/maps/` 目录存放编辑器导出的 Map 文件
- `src/data/maps.lua` 提供加载函数：`Maps.load(path) → map table`
- **验证**: `dofile()` 加载导出的 Map 文件无错误

### 10.3 MapRenderer 修改

- 影响的文件: `src/systems/map_renderer.lua`
- 修改 `MapRenderer:init()` 以支持从导出的 Map 数据加载
- 加载 Map 中引用的所有 TileSet 纹理
- 根据 cells 数据渲染每个 tile（考虑 sizeInAtlas、flipH/V、textureOffset）
- 处理多格 tile 的渲染（下方格为基准，向上延伸）
- **验证**: 游戏中正确显示编辑器创建的地图

### 10.4 Wall 属性识别

- 影响的文件: `src/systems/map_renderer.lua`, `src/systems/movement.lua`
- 根据 TileData 的 `isWall` 属性设置 Solid 组件
- 移动系统检查目标格是否为墙
- **验证**: 标记为墙的 tile 不可通行

### 10.5 遮蔽系统

- 影响的文件: `src/systems/render.lua`, `src/systems/map_renderer.lua`
- 根据 TileData 的 `occlusionRegions` 实现遮蔽效果
- 渲染时检查玩家位置是否在遮蔽区域内：
  - 是：降低玩家渲染优先级或隐藏玩家（被遮挡物覆盖）
  - 否：正常渲染
- zOrder 决定遮蔽优先级（数值越大越优先遮蔽）
- **验证**: 玩家走到树木遮蔽区域后面时，玩家被树木遮挡

---

## 阶段依赖关系

```
阶段 1 (窗口) → 阶段 2 (数据模型)
                → 阶段 3 (纹理加载) → 阶段 4 (图集视图 + 状态)
                                      → 阶段 5 (Tile 创建) → 阶段 6 (属性标记)
                                                              → 阶段 7 (遮蔽标记)
                                      → 阶段 8 (地图编辑器) ← 阶段 5
                                                              → 阶段 9 (Lua 导出) ← 阶段 6, 7, 8
                                                                                    → 阶段 10 (游戏集成)
```

阶段 1→2→3→4→5 是基础流水线，必须按顺序。
阶段 6、7 依赖阶段 5，可并行。
阶段 8 依赖阶段 5。
阶段 9 依赖阶段 6、7、8。
阶段 10 依赖阶段 9。
