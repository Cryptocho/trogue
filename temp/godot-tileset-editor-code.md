# Godot TileSet 编辑器系统源码分析

## 概述

Godot 4.x 的 TileSet 编辑器系统位于 `editor/scene/2d/tiles/` 目录，包含 **10 对 .h/.cpp 文件**（共 20 个源文件）加 1 个构建脚本。该系统同时包含 **TileSet 资源编辑器**（Dock 面板）和 **TileMapLayer 实例编辑器**（底栏面板），以及贯穿两者的**全局工具单例**。

---

## 目录结构

```
editor/scene/2d/tiles/
├── SCsub                                     # 构建脚本，自动包含 *.cpp
├── tiles_editor_plugin.h/.cpp                # 顶层插件注册 + TilesEditorUtils 单例
├── tile_set_editor.h/.cpp                    # TileSet 资源主编辑器 Dock（~1013 行）
├── tile_set_atlas_source_editor.h/.cpp       # 图集源编辑器（最复杂，~2800 行）
├── tile_set_scenes_collection_source_editor.h/.cpp  # 场景集合源编辑器
├── tile_data_editors.h/.cpp                  # TileData 属性"绘制"编辑类层次（414 行头文件）
├── tile_atlas_view.h/.cpp                    # 图集可视化视图组件
├── tile_map_layer_editor.h/.cpp              # TileMapLayer 画布编辑器（最大，~4500 行）
├── tile_proxies_manager_dialog.h/.cpp        # Tile 代理管理对话框
└── atlas_merging_dialog.h/.cpp              # 图集合并对话框
```

外部依赖：
- `scene/resources/2d/tile_set.h/.cpp` — 核心资源数据模型（1044 行头文件）
- `editor/register_editor_types.cpp:269-270` — 插件注册入口
- `editor/themes/theme_modern.cpp:2905` — `"expand_panel"` 样式
- `editor/themes/theme_classic.cpp:2555` — 同上（经典主题）

---

## 一、TilesEditorUtils（全局单例）

**文件**: `tiles_editor_plugin.h/.cpp`  
**类**: `TilesEditorUtils : Object`  
**生命周期**: 在 `TileSetEditorPlugin` 或 `TileMapEditorPlugin` 构造时首次创建（二者都检查 `!get_singleton()`），在 `TileMapEditorPlugin::NOTIFICATION_EXIT_TREE` 时销毁。

### 职责

1. **跨编辑器源列表同步**：TileSet 编辑器和 TileMapLayer 编辑器各自拥有源列表，通过 `synchronize_sources_list()` 保持选中项一致
2. **图集视图变换同步**：缩放/平移在多个编辑器间同步（`set_atlas_view_transform` / `synchronize_atlas_view`）
3. **源排序**：4 种排序方式（ID 升/降序、名称升/降序），通过 `get_sorted_sources()` 获取排好序的源 ID 列表
4. **图案预览生成**：后台线程渲染图案缩略图（`queue_pattern_preview` → 线程内挂起等待 → `_preview_frame_started` 信号回调触发实际预览生成 → `_pattern_preview_done` 回叫调用者）
5. **快捷键注册**：在其构造函数中注册 `tiles_editor/` 前缀的快捷键（cut/copy/paste/cancel/delete/paint/line/rect/bucket/eraser/picker）
6. **工具函数**：`draw_selection_rect()` 绘制白色选择矩形框，`display_tile_set_editor_panel()` 打开 TileSet 编辑器面板

### 关键数据成员

```cpp
int atlas_sources_lists_current = 0;  // 当前选中的源列表 index
float atlas_view_zoom = 1.0;          // 图集视图缩放
Vector2 atlas_view_scroll;            // 图集视图平移
int source_sort = SOURCE_SORT_ID;     // 源排序方式

// 图案预览线程
List<QueueItem> pattern_preview_queue;
Mutex pattern_preview_mutex;
Semaphore pattern_preview_sem;
Thread pattern_preview_thread;
SafeFlag pattern_thread_exit;
SafeFlag pattern_thread_exited;
```

---

## 二、TileSetEditorPlugin（TileSet 插件）

**类**: `TileSetEditorPlugin : EditorPlugin`  
**注册于**: `editor/register_editor_types.cpp:269`

### 关键方法

| 方法 | 行号 | 说明 |
|------|------|------|
| `handles(Object*)` | tiles_editor_plugin.cpp:541 | 返回 `cast_to<TileSet>(p_object) != nullptr` |
| `edit(Object*)` | tiles_editor_plugin.cpp:532 | 将传入的 TileSet 资源传递给 `TileSetEditor::edit()` |
| `make_visible(bool)` | tiles_editor_plugin.cpp:545 | 显示/隐藏 TileSetEditor Dock |
| `open_editor()` | tiles_editor_plugin.cpp:553 | 直接打开 Dock（从 TileMapLayer 编辑器跳转时使用） |

### 生命周期

```cpp
TileSetEditorPlugin() {
    TilesEditorUtils::get_singleton();  // 确保单例存在
    editor = memnew(TileSetEditor);
    EditorDockManager::get_singleton()->add_dock(editor);
    editor->close();
}
```

---

## 三、TileMapEditorPlugin（TileMapLayer 插件）

**类**: `TileMapEditorPlugin : EditorPlugin`  
**注册于**: `editor/register_editor_types.cpp:270`

### 关键方法

| 方法 | 行号 | 说明 |
|------|------|------|
| `handles(Object*)` | .cpp:475 | 处理 `TileMapLayer`、`TileMap`（已废弃）、`MultiNodeEdit`（纯 TileMapLayer 多选） |
| `edit(Object*)` | .cpp:445 | 分发到 `_edit_tile_map()` 或 `_edit_tile_map_layer()` |
| `_edit_tile_map_layer()` | .cpp:399 | 编辑单个 TileMapLayer，同时自动调用 `TileSetEditorPlugin::edit()` 打开对应的 TileSet |
| `_edit_tile_map()` | .cpp:422 | 编辑整个 TileMap（显示层级选择器），委托给第一层的 `_edit_tile_map_layer()` |
| `make_visible(bool)` | .cpp:491 | 显示 TileMapLayerEditor，关闭 TileSetEditor |
| `forward_canvas_gui_input()` | .cpp:500 | 转发画布输入给 `TileMapLayerEditor` |
| `_update_tile_map()` | .cpp:371 | 延迟更新——当 TileMapLayer 的 TileSet 变化时自动切换 TileSet 编辑器 |

关键集成逻辑：当用户选择 TileMapLayer 时，自动同时打开 TileSet 编辑器以供编辑资源（`.cpp:413`）。

---

## 四、TileSetEditor（TileSet 主编辑器 Dock）

**文件**: `tile_set_editor.h/.cpp`  
**类**: `TileSetEditor : EditorDock`（单例模式，`static singleton`）

### UI 布局

```
main_vb (VBoxContainer, PRESET_FULL_RECT)
├── tile_set_toolbar (HBoxContainer)
│   └── tabs_panel (PanelContainer)
│       └── tabs_bar (TabBar) ["Tile Sources" | "Patterns"]
├── split_container (HSplitContainer)  [visible when tab=0]
│   ├── split_container_left_side (VBoxContainer, stretch_ratio=0.25)
│   │   ├── sources_list (TileSetSourceItemList)  [继承 ItemList, 带 tooltip]
│   │   └── sources_bottom_actions (HBoxContainer)
│   │       ├── sources_delete_button            [Remove icon]
│   │       ├── sources_add_button (MenuButton)  [Add icon; Atlas | Scenes Collection]
│   │       ├── sources_advanced_menu_button (MenuButton) [Open Atlas Merging Tool | Manage Tile Proxies]
│   │       └── source_sort_button (MenuButton)  [Sort icon; 4 种排序]
│   └── split_container_right_side (VBoxContainer)
│       ├── no_source_selected_label (Label, 居中提示)
│       ├── tile_set_atlas_source_editor (TileSetAtlasSourceEditor)  [初始隐藏]
│       └── tile_set_scenes_collection_source_editor (HBoxContainer) [初始隐藏]
├── patterns_mc (MarginContainer)  [visible when tab=1, 64px 缩略图]
│   ├── patterns_item_list (ItemList)
│   └── patterns_help_label (Label, 子控件)
└── expanded_area (PanelContainer, PRESET_LEFT_WIDE)  [初始隐藏]
```

### 构造函数流程（`TileSetEditor()`, 行 835-1013）

1. 设置 Dock 属性：名称 "TileSet"，图标 "TileSet"，快捷键，默认底栏位置，支持横向/浮动布局
2. 创建 TabBar（"Tile Sources" / "Patterns" 两个标签）
3. 创建左侧：源列表（`TileSetSourceItemList`）+ 底部操作按钮（删除/添加/高级/排序）
4. 创建 `AtlasMergingDialog` 和 `TileProxiesManagerDialog` 对话框
5. 创建右侧空白区：`no_source_selected_label` + 两个子编辑器（初始隐藏）
6. 创建图案列表区域（`patterns_mc`，仅 Tab=1 时可见）
7. 创建展开区（`expanded_area`，用于多边形编辑器的全屏展开）
8. 注册 UndoRedo 回调（`_move_tile_set_array_element` 和 `_undo_redo_inspector_callback`）

### 核心方法

| 方法 | 行号 | 说明 |
|------|------|------|
| `edit(Ref<TileSet>)` | 741 | 设置编辑目标，断开/连接 `changed` 信号，更新源列表和图案列表 |
| `_update_sources_list(int)` | 140 | 清空并重建源列表：遍历所有源，区分图集源（显示纹理图标）和场景集合源（显示 PackedScene 图标），恢复选中状态 |
| `_source_selected(int)` | 244 | 切换右侧编辑器：图集源 → `TileSetAtlasSourceEditor`，场景集合源 → `TileSetScenesCollectionSourceEditor` |
| `_source_add_id_pressed(int)` | 294 | 创建新源（Atlas 或 Scenes Collection），图集源弹出文件选择对话框选择纹理 |
| `_source_delete_pressed()` | 276 | 删除当前选中的源（带确认） |
| `_drop_data_fw()` | 52 | 处理纹理文件拖放：拖入纹理到源列表 → 自动创建图集源 |
| `_can_drop_data_fw()` | 67 | 检查拖放的数据是否为 Texture2D 类型文件 |
| `_load_texture_files(Vector<String>)` | 103 | 加载多个纹理文件，每个创建一个新的 TileSetAtlasSource，批量初始化 |
| `_tab_changed(int)` | 477 | 切换 Tile Sources / Patterns 标签页的显示 |
| `_update_patterns_list()` | 457 | 清空并重建图案列表，为每个图案请求异步预览生成 |
| `_set_source_sort(int)` | 347 | 设置源排序方式并刷新列表 |
| `_tile_set_changed()` | 473 | 设置 `tile_set_changed_needs_update = true`，由 `NOTIFICATION_INTERNAL_PROCESS` 延迟处理 |
| `_notification(int)` | 367 | 处理主题变更（更新按钮图标）、翻译变更、内部处理（延迟刷新）、可见性变更（收起展开编辑器） |
| `add_expanded_editor(Control*)` | 780 | 将子编辑器移出到 `expanded_area`（全屏展开），隐藏所有 SplitContainer 的拖拽条 |
| `remove_expanded_editor()` | 810 | 将编辑器移回原父节点，恢复 SplitContainer 拖拽条 |
| `update_layout(DockLayout, DockSlot)` | 416 | 响应 Dock 布局变化：横向布局时对应调整 |

### TileSourceInspectorPlugin

**类**: `TileSourceInspectorPlugin : EditorInspectorPlugin`  
内联在 `tile_set_editor.h` 末尾。为 TileSetSource 在 Inspector 中添加一个修改 Source ID 的对话框按钮。

---

## 五、TileSetAtlasSourceEditor（图集源编辑器）

**文件**: `tile_set_atlas_source_editor.h/.cpp`（最复杂的文件，.cpp ~2800 行）  
**类**: `TileSetAtlasSourceEditor : HSplitContainer`

### 内部类

| 内部类 | 继承自 | 说明 |
|--------|--------|------|
| `TileSelection` | struct | 存储选中的图集坐标 + 替代 tile ID |
| `TileSetAtlasSourceProxyObject` | `RefCounted` | 图集源检查器代理（让源属性可在 Inspector 中编辑） |
| `AtlasTileProxyObject` | `RefCounted` | 选中 tile 的检查器代理（让 tile 属性可在 Inspector 中编辑） |
| `TileAtlasControl` | `Control` | 图集区域的鼠标控制层（自定义 `get_cursor_shape`） |

### 拖放类型枚举（17 种）

```cpp
enum DragType {
    DRAG_TYPE_NONE = 0,
    DRAG_TYPE_CREATE_TILES,
    DRAG_TYPE_CREATE_TILES_USING_RECT,
    DRAG_TYPE_CREATE_BIG_TILE,
    DRAG_TYPE_REMOVE_TILES,
    DRAG_TYPE_REMOVE_TILES_USING_RECT,
    DRAG_TYPE_MOVE_TILE,
    DRAG_TYPE_RECT_SELECT,
    DRAG_TYPE_MAY_POPUP_MENU,
    // WARNING: Keep in this order.
    DRAG_TYPE_RESIZE_TOP_LEFT,
    DRAG_TYPE_RESIZE_TOP,
    DRAG_TYPE_RESIZE_TOP_RIGHT,
    DRAG_TYPE_RESIZE_RIGHT,
    DRAG_TYPE_RESIZE_BOTTOM_RIGHT,
    DRAG_TYPE_RESIZE_BOTTOM,
    DRAG_TYPE_RESIZE_BOTTOM_LEFT,
    DRAG_TYPE_RESIZE_LEFT,
};
```

### UI 布局

构造函数（行 2529）创建以下 UI 结构：

```
HSplitContainer (TileSetAtlasSourceEditor)
├── [中栏] VBoxContainer (middle_vbox_container, 最小宽度 200*EDSCALE)
│   ├── HBoxContainer (toolbox)
│   │   ├── tool_setup_atlas_source_button [Button, "Setup" mode]
│   │   ├── tool_select_button [Button, "Select" mode]
│   │   └── tool_paint_button [Button, "Paint" mode]
│   ├── tile_inspector (EditorInspector)  [选中 tile 的属性面板]
│   ├── tile_data_editors_scroll (ScrollContainer)  [属性绘制工具面板]
│   │   ├── tile_data_editor_dropdown_button [自定义绘制按钮]
│   │   └── tile_data_painting_editor_container [VBoxContainer, 子编辑器的容器]
│   └── atlas_source_inspector (EditorInspector)  [图集源自身属性]
│
└── [右栏] VBoxContainer (right_vbox_container)
    ├── tool_settings (HBoxContainer)
    │   ├── tool_settings_tile_data_toolbar_container [属性绘制工具栏]
    │   ├── tools_settings_erase_button [Button, Eraser toggle]
    │   ├── tool_advanced_menu_button [MenuButton, 自动创建/清理选项]
    │   └── outside_tiles_warning [TextureRect, 越界警告图标]
    ├── right_panel (VBoxContainer)
    │   └── tile_atlas_view (TileAtlasView)  [核心图集视图]
    │       ├── tile_atlas_control (TileAtlasControl)  [鼠标交互层]
    │       ├── tile_atlas_control_unscaled [非缩放叠加层]
    │       ├── alternative_tiles_control [Control, 替代 tile 面板]
    │       └── alternative_tiles_control_unscaled [非缩放叠加层]
    ├── base_tile_popup_menu (PopupMenu)  [已存在 tile 的右键菜单]
    ├── empty_base_tile_popup_menu (PopupMenu)  [空区域的右键菜单]
    └── alternative_tile_popup_menu (PopupMenu)  [替代 tile 的右键菜单]
```

### 核心方法

#### 编辑生命周期

| 方法 | 行号 | 说明 |
|------|------|------|
| `edit(Ref<TileSet>, TileSetAtlasSource*, int)` | 2187 | 设置编辑目标，断开/连接信号，清空选择，调用所有更新方法（`_update_source_inspector` / `_update_atlas_view` / `_update_tile_data_editors` 等共 10 个） |
| `init_new_atlases(Vector<Ref<TileSetAtlasSource>>)` | 2255 | 新图集的初始化：切换至 Setup 模式，弹出确认对话框询问是否自动创建 tile |

#### 图集交互

| 方法 | 行号 | 说明 |
|------|------|------|
| `_tile_atlas_control_gui_input(Ref<InputEvent>)` | 1092 | 图集鼠标事件主入口。Paint 模式下转发给 `current_tile_data_editor`；Setup/Select 模式下处理 17 种拖拽类型 |
| `_tile_atlas_control_draw()` | 1741 | 绘制选择区域（绿色高亮）、调整大小手柄（8 方向）、拖拽预览（创建/删除/矩形选择/悬停） |
| `_tile_atlas_control_unscaled_draw()` | 1880 | 绘制非缩放覆盖层（如坐标标签） |
| `_end_dragging()` | 1395 | 拖拽结束处理：根据拖拽类型提交 UndoRedo action（create/remove/move/resize/select tiles） |
| `_tile_alternatives_control_gui_input()` | 1930 | 替代 tile 区域的鼠标事件 |
| `_tile_alternatives_control_draw()` | 2035 | 绘制替代 tile 列表（黄色/白色选择高亮） |

#### 更新方法

| 方法 | 行号 | 说明 |
|------|------|------|
| `_update_atlas_view()` | 973 | 刷新 TileAtlasView（设置图集源、纹理、渲染控制） |
| `_update_source_inspector()` | 564 | 更新图集源自己的 Inspector 代理对象 |
| `_update_atlas_source_inspector()` | 601 | 更新源检查器 UI |
| `_update_tile_inspector()` | 608 | 更新选中 tile 的 Inspector 代理对象（`AtlasTileProxyObject`） |
| `_update_tile_data_editors()` | 626 | 构建属性绘制工具树（见下节详解） |
| `_update_current_tile_data_editor()` | 880 | 切换当前活动的 TileData 编辑器（根据下拉菜单选择） |
| `_update_tile_id_label()` | 553 | 更新光标位置的 tile 坐标和 ID 显示 |
| `_update_fix_selected_and_hovered_tiles()` | 579 | 修复选中/悬停 tile 数据（确保选择有效） |
| `_update_buttons()` | 1073 | 根据当前模式更新按钮状态 |
| `_update_toolbar()` | 1046 | 切换工具模式时更新工具栏显示 |

#### 右键菜单

| 方法 | 行号 | 说明 |
|------|------|------|
| `_menu_option(int)` | 1611 | 处理右键菜单动作：创建/删除 tile、创建替代 tile、自动创建/移除/清理 tile |

#### 自动 Tile 操作

| 方法 | 行号 | 说明 |
|------|------|------|
| `_auto_create_tiles()` | 2318 | 遍历图集网格，在非透明像素区域自动创建 tile（检查每个像素的 opacity） |
| `_auto_remove_tiles()` | 2369 | 遍历所有 tile，移除位于完全透明区域的 tile |
| `_cleanup_outside_tiles()` | 2287 | 移除超出纹理边界的 tile |
| `_check_outside_tiles()` | 2280 | 检查是否有 tile 超出纹理边界（更新警告图标） |

#### 信号与输入

| 方法 | 行号 | 说明 |
|------|------|------|
| `shortcut_input(Ref<InputEvent>)` | 1707 | 处理快捷键（复制/粘贴选择、删除 tile） |
| `_tile_set_changed()` | 2117 | TileSet 资源变化时的延迟更新 |
| `_tile_proxy_object_changed(String)` | 2128 | tile 属性代理变化 → 更新编辑器和 UndoRedo |
| `_atlas_source_proxy_object_changed(String, Ref<>)` | 2133 | 图集源属性代理变化 → 更新编辑器和 UndoRedo |
| `_undo_redo_inspector_callback(Object*, Object*, String, Variant)` | 2143 | UndoRedo Inspector 钩子 |

### _update_tile_data_editors() 详解（行 626-877）

这是构建属性绘制工具树的核心方法，使用宏 `ADD_TILE_DATA_EDITOR_GROUP` 和 `ADD_TILE_DATA_EDITOR` 创建树形结构：

```
root (TreeItem)
├── [Rendering] (group)
│   ├── Texture Origin (TileDataTextureOriginEditor)
│   ├── Modulate (TileDataDefaultEditor, ColorProperty)
│   ├── Z Index (TileDataDefaultEditor, IntProperty)
│   ├── Y Sort Origin (TileDataDefaultEditor, IntProperty)
│   └── [Occlusion Layer 0..N] (each TileDataOcclusionShapeEditor)
├── [Terrains] (TileDataTerrainsEditor)
├── [Miscellaneous]
│   └── Probability (TileDataDefaultEditor, FloatProperty)
├── [Physics] (group)
│   ├── No physics layers (info label, if 0 layers)
│   └── [Physics Layer 0..N] (each TileDataCollisionEditor)
├── [Navigation] (group)
│   ├── No navigation layers (info label, if 0 layers)
│   └── [Navigation Layer 0..N] (each TileDataNavigationEditor)
└── [Custom Data] (group)
    ├── No custom data layers (info label, if 0 layers)
    └── [Custom Data Layer 0..N] (each TileDataDefaultEditor)
```

每个 `TileDataEditor` 在创建后通过 `HashMap<String, TileDataEditor *>` 缓存。切换 TileSet 时，编辑器会动态添加/移除与图层数对应的子编辑器。所有编辑器存储在 `tile_data_editors` 字典中，通过下拉按钮选择当前活动编辑器。

---

## 六、TileSetScenesCollectionSourceEditor（场景集合源编辑器）

**文件**: `tile_set_scenes_collection_source_editor.h/.cpp`  
**类**: `TileSetScenesCollectionSourceEditor : HBoxContainer`

### 内部类

| 内部类 | 继承自 | 说明 |
|--------|--------|------|
| `TileSetScenesCollectionProxyObject` | `RefCounted` | 场景集合源检查器代理 |
| `SceneTileProxyObject` | `RefCounted` | 单个场景 tile 的检查器代理 |

### UI 布局

```
HBoxContainer (TileSetScenesCollectionSourceEditor)
├── [左栏]
│   ├── scene_tiles_list (ItemList)  [场景 tile 缩略图列表]
│   ├── scene_tile_add_button [Button, 添加场景文件]
│   └── scene_tile_delete_button [Button, 删除选中场景]
├── [中栏] scenes_collection_source_inspector (EditorInspector)
└── [右栏] tile_inspector (EditorInspector)
```

### 核心方法

| 方法 | 行号 | 说明 |
|------|------|------|
| `edit(Ref<TileSet>, TileSetScenesCollectionSource*, int)` | — | 设置编辑目标，连接信号，更新所有 UI |
| `_source_add_pressed()` | — | 打开 `EditorFileDialog` 选择 `.tscn` 场景文件 |
| `_scene_file_selected(String)` | — | 选中的场景文件 → 创建新的场景 tile |
| `_source_delete_pressed()` | — | 删除选中的场景 tile（带确认） |
| `_update_scenes_list()` | — | 刷新场景列表：加载每个 scene 的缩略图（异步生成 `_scene_thumbnail_done`） |
| `_update_source_inspector()` | — | 更新场景集合源的检查器 |
| `_update_tile_inspector()` | — | 更新选中场景 tile 的检查器 |
| `_drop_data_fw()` | — | 处理 `.tscn` 文件的拖放 |
| `_scenes_list_item_activated(int)` | — | 双击打开场景文件 |

---

## 七、TileDataEditor 类层次结构

**文件**: `tile_data_editors.h/.cpp`  
**类层次**:

```
TileDataEditor (VBoxContainer)  [基类]
├── TileDataDefaultEditor       [通用属性编辑器，带 paint/picker 支持]
│   ├── TileDataTextureOriginEditor  [纹理原点，绘制偏移箭头]
│   ├── TileDataPositionEditor       [位置偏移]
│   ├── TileDataYSortEditor          [Y 排序原点]
│   ├── TileDataOcclusionShapeEditor [遮挡多边形编辑器，含 GenericTilePolygonEditor]
│   ├── TileDataCollisionEditor      [碰撞多边形编辑器，含物理属性面板]
│   └── TileDataNavigationEditor     [导航多边形编辑器]
└── TileDataTerrainsEditor      [地形编辑器，含 terrain_set/terrain 选择器]

DummyObject (Object)  [虚拟属性持有者，用于检查器编辑]
GenericTilePolygonEditor (VBoxContainer)  [通用多边形编辑工具]
```

### TileDataEditor 基类（行 42-69）

提供虚拟方法供子类覆盖：

| 虚拟方法 | 说明 |
|---------|------|
| `get_toolbar()` | 返回工具栏 Control（用于放置在 tile_set_atlas_source_editor 的上方工具栏） |
| `forward_draw_over_atlas()` | 在图集视图上绘制属性覆盖层（如碰撞多边形、地形颜色） |
| `forward_draw_over_alternatives()` | 在替代 tile 面板上绘制覆盖层 |
| `forward_painting_atlas_gui_input()` | 处理图集上的绘制鼠标事件（点击/拖拽应用属性值） |
| `forward_painting_alternatives_gui_input()` | 处理替代 tile 面板上的绘制事件 |
| `draw_over_tile()` | 在单个 tile 上绘制属性可视化标记 |

### TileDataDefaultEditor（行 193-249）

核心抽象——所有"绘制"属性编辑器的基础：
- 通过 `setup_property_editor(Variant::Type, String, String, Variant)` 配置编辑器 UI
- 包含 `picker_button`（取色器模式，点击现有 tile 获取属性值）
- `DragType` 枚举：`PAINT`（单个绘制）、`PAINT_RECT`（矩形区域绘制）
- 关键虚拟方法子类必须实现：
  - `_get_painted_value()` / `_set_painted_value()` — 获取/设置当前绘制值
  - `_set_value()` / `_get_value()` — 读写单个 tile 的指定属性
  - `_setup_undo_redo_action()` — 自定义 UndoRedo 逻辑

### TileDataOcclusionShapeEditor（行 272-300）

- 管理单个遮挡层的多边形
- 包含 `GenericTilePolygonEditor` 子编辑器（展开模式）
- `_polygon_changed()` 回调响应多边形编辑

### TileDataCollisionEditor（行 302-334）

- 管理单个物理层的碰撞多边形
- 包含 `GenericTilePolygonEditor` + 额外属性编辑器（one_way、one_way_margin）
- 通过 `DummyObject` 持有物理层属性（linear_velocity, angular_velocity）
- `_polygons_changed()` 回调响应多边形编辑

### TileDataTerrainsEditor（行 336-383）

- 管理地形属性（terrain_set + terrain 选择和绘制）
- 包含 `EditorPropertyEnum` 两个下拉选择器（地形集、地形）
- DragType 枚举：`PAINT_TERRAIN_SET`、`PAINT_TERRAIN_SET_RECT`、`PAINT_TERRAIN_BITS`、`PAINT_TERRAIN_BITS_RECT`
- `_update_terrain_selector()` 根据地形集更新地形选择器

### TileDataNavigationEditor（行 385-414）

- 管理单个导航层的导航多边形
- 包含 `GenericTilePolygonEditor` 子编辑器
- `_polygon_changed()` 回调

### GenericTilePolygonEditor（行 87-191）

通用多边形点编辑工具，被遮挡/碰撞/导航编辑器复用：
- 支持多多边形模式（`multiple_polygon_mode`）
- 内置 Snap 系统（无吸附/半像素/网格吸附）
- DragType：`DRAG_POINT`、`CREATE_POINT`、`PAN`
- 背景瓦片显示（`set_background_tile()`）
- 高级菜单：重置、清除、旋转、翻转
- 包含 `EditorZoomWidget` 缩放控制
- `_grab_polygon_point()` / `_grab_polygon_segment_point()` 检测鼠标下的多边形顶点/线段

### DummyObject（行 71-85）

虚拟 Object，用于在检查器中显示非标准属性。通过 `add_dummy_property(name)` 动态添加属性，`_set`/`_get` 重定向到内部 `HashMap<String, Variant>`。

---

## 八、TileAtlasView（图集视图组件）

**文件**: `tile_atlas_view.h/.cpp`  
**类**: `TileAtlasView : Control`

### UI 结构

```
TileAtlasView (Control)
├── margin_container (MarginContainer)
│   └── hbox (HBoxContainer)
│       ├── background_left (Control, 左背景绘制)
│       ├── [Left: Base tiles area]
│       │   ├── base_tiles_root_control (Control, 鼠标事件主入口)
│       │   │   ├── base_tiles_drawing_root (Control, 子控件容器，缩放)
│       │   │   │   ├── base_tiles_draw (Control, 纹理绘制)
│       │   │   │   ├── base_tiles_texture_grid (Control, 纹理网格线)
│       │   │   │   └── base_tiles_shape_grid (Control, 瓦片形状网格)
│       │   │   └── [子编辑器通过 add_control_over_atlas_tiles() 添加]
│       │   └── background_right (Control, 右背景绘制)
│       └── [Right: Alternative tiles area]
│           └── alternative_tiles_root_control (Control)
│               ├── alternative_tiles_drawing_root (Control, 子控件容器，缩放)
│               │   └── alternatives_draw (Control, 替代 tile 绘制)
│               └── [子编辑器通过 add_control_over_alternative_tiles() 添加]
├── zoom_widget (EditorZoomWidget)  [右上角缩放控件]
├── button_center_view (Button)  [居中视图按钮]
├── center_container (CenterContainer)  [居中容器]
└── missing_source_label (Label)  [无源时提示]
```

### 核心方法

| 方法 | 行号 | 说明 |
|------|------|------|
| `set_atlas_source(TileSet*, TileSetAtlasSource*, int)` | — | 设置显示的图集源，更新纹理和源数据 |
| `set_transform(float, Vector2i)` | — | 设置缩放/平移变换（用于多编辑器同步） |
| `set_padding(Side, int)` | — | 设置边缘内边距（用于多 SplitContainer 布局） |
| `get_zoom()` | — | 返回当前缩放值 |
| `get_atlas_tile_coords_at_pos(Vector2, bool)` | — | 根据屏幕位置计算对应的图集瓦片坐标 |
| `get_alternative_tile_at_pos(Vector2)` | — | 获取鼠标下的替代 tile 坐标（返回 Vector3i: x,y,alternative） |
| `get_alternative_tile_rect(Vector2i, int)` | — | 返回指定替代 tile 在视图中的矩形区域 |
| `add_control_over_atlas_tiles(Control*, bool)` | — | 在图集瓦片区域添加覆盖控件（由 TileSetAtlasSourceEditor / TileMapLayerEditor 使用） |
| `add_control_over_alternative_tiles(Control*, bool)` | — | 在替代 tile 区域添加覆盖控件 |
| `set_texture_grid_visible(bool)` | — | 显示/隐藏纹理网格线 |
| `set_tile_shape_grid_visible(bool)` | — | 显示/隐藏瓦片形状网格 |
| `queue_redraw()` | — | 刷新所有绘制 |
| `_draw_base_tiles()` | — | 绘制所有基础 tile 纹理（支持材质分离） |
| `_draw_base_tiles_texture_grid()` | — | 绘制纹理网格分割线 |
| `_draw_base_tiles_shape_grid()` | — | 根据 TileShape（Square/Isometric/HalfOffset/Hexagon）绘制瓦片轮廓 |
| `_draw_alternatives()` | — | 绘制替代 tile 列表 |
| `_update_alternative_tiles_rect_cache()` | — | 缓存替代 tile 的布局位置 |
| `gui_input(Ref<InputEvent>)` | — | 处理缩放和平移（鼠标滚轮缩放、拖拽平移） |
| `_zoom_widget_changed()` | — | 缩放控件变化回调 |
| `_center_view()` | — | 居中视图 |

材质支持：通过 `_get_canvas_item_to_draw()` 方法，为每个不同材质的 tile 创建独立的 CanvasItem，确保材质正确应用。材质缓存通过 `material_tiles_draw` 和 `material_alternatives_draw` 两个 HashMap 管理。

---

## 九、TileMapLayerEditor（TileMapLayer 画布编辑器）

**文件**: `tile_map_layer_editor.h/.cpp`（最大文件，.cpp ~4500 行）  
**类**: `TileMapLayerEditor : EditorDock`

### 内部子编辑器插件

```
TileMapLayerSubEditorPlugin (Object, GDSOFTCLASS)  [基类]
├── TileMapLayerEditorTilesPlugin   [Tile 绘制子编辑器]
└── TileMapLayerEditorTerrainsPlugin  [地形绘制子编辑器]
```

### TileMapLayerEditor 自身 UI

```
EditorDock (TileMapLayerEditor)
├── main_box_container (GridContainer)
├── tile_map_wide_toolbar (VBoxContainer)
├── tile_map_toolbar (FlowContainer)
├── [层选择器]
│   ├── layer_selection_hbox (HBoxContainer)
│   │   ├── select_previous_layer [←]
│   │   ├── select_next_layer [→]
│   │   ├── select_all_layers [Button]
│   │   └── layers_selection_button (OptionButton)
│   ├── toggle_highlight_selected_layer_button [Button]
│   └── toggle_grid_button [Button]
├── advanced_menu_button (MenuButton) [Replace with proxies | Extract TileMap layers]
├── cant_edit_label (Label)
├── tabs_bar (TabBar)  [由插件动态创建标签]
└── tabs_panel (PanelContainer)
```

### TileMapLayerEditorTilesPlugin（行 94-272）

**Tile 绘制子编辑器**，实现画布上的各种绘制操作：

| 方法 | 说明 |
|------|------|
| `forward_canvas_gui_input()` | 处理画布输入：选择/绘制/划线/矩形/桶填/取色 |
| `_draw_line()` | 划线算法：Bresenham 线 → 生成要修改的 cells |
| `_draw_rect()` | 画矩形区域 |
| `_draw_bucket_fill()` | 桶状填充（支持连续/非连续模式） |
| `_pick_random_tile(Ref<TileMapPattern>)` | 从图案中随机选取 tile（支持散布率 `scattering`） |
| `_apply_transform(TileTransformType)` | 对选中的 tile 应用旋转变换 |
| `_get_transformed_alternative(int, TileTransformType)` | 获取变换后的替代 tile ID |
| `_update_selection_pattern_from_tilemap_selection()` | 从 TileMap 选择同步到图案 |
| `_update_selection_pattern_from_tileset_tiles_selection()` | 从 TileSet 选择同步到图案 |
| `_fix_invalid_tiles_in_tile_map_selection()` | 修复 tile 地图选择中的无效 tile |
| `edit(ObjectID)` | 设置编辑目标，更新 UI |
| `get_tabs()` | 返回 Tab 数据（工具按钮 + 面板 UI） |

**工具按钮**：select / paint / line / rect / bucket + picker / eraser toggle  
**变换按钮**：rotate_left / rotate_right / flip_h / flip_v  
**设置**：scattering（散布率）、bucket_contiguous（连续填充）、random_tile_toggle（随机 tile）

**画布 DragType**：`SELECT`, `MOVE`, `PAINT`, `LINE`, `RECT`, `BUCKET`, `PICK`, `CLIPBOARD_PASTE`

### TileMapLayerEditorTerrainsPlugin（行 274-366）

**地形绘制子编辑器**：

| 方法 | 说明 |
|------|------|
| `_draw_terrain_path_or_connect()` | 地形路径/连接绘制：匹配相邻地形 |
| `_draw_terrain_pattern()` | 地形图案绘制：根据 TerrainsPattern 批量设置 |
| `_draw_line()` | 划线（地形模式） |
| `_draw_rect()` | 画矩形（地形模式） |
| `_draw_bucket_fill()` | 桶状填充（地形模式） |
| `_update_terrains_cache()` | 从 TileSet 缓存所有地形的可用 tile 图案 |
| `_update_terrains_tree()` | 更新地形树形选择器 |
| `_update_tiles_list()` | 更新选中地形可用 tile 列表 |
| `edit(ObjectID)` | 设置编辑目标 |

**画布 DragType**：`PAINT`, `LINE`, `RECT`, `BUCKET`, `PICK`  
**选择类型**：`CONNECT`（连接模式）、`PATH`（路径模式）、`PATTERN`（图案模式）

---

## 十、TileProxiesManagerDialog（代理管理对话框）

**文件**: `tile_proxies_manager_dialog.h/.cpp`  
**类**: `TileProxiesManagerDialog : ConfirmationDialog`

用于管理 TileSet 中三级 tile 代理映射（资源迁移/重定向工具）：

```
代理层级:
1. 源级代理 (source_level):  source_id_from → source_id_to
2. 坐标级代理 (coords_level): source_id + coords → source_id + coords
3. 替代级代理 (alternative_level): source_id + coords + alt → source_id + coords + alt
```

### UI

```
ConfirmationDialog (TileProxiesManagerDialog)
├── [三级列表]
│   ├── source_level_list (ItemList)       [源级代理列表]
│   ├── coords_level_list (ItemList)       [坐标级代理列表]
│   └── alternative_level_list (ItemList)  [替代级代理列表]
├── [属性编辑器（6 个，用于编辑 from/to 的值）]
│   ├── source_from_property_editor (EditorPropertyInteger)
│   ├── coords_from_property_editor (EditorPropertyVector2i)
│   ├── alternative_from_property_editor (EditorPropertyInteger)
│   ├── source_to_property_editor (EditorPropertyInteger)
│   ├── coords_to_property_editor (EditorPropertyVector2i)
│   └── alternative_to_property_editor (EditorPropertyInteger)
├── Add Button [+]
└── Right-click context menu: Delete, Clear Invalid, Clear All
```

### 核心方法

| 方法 | 说明 |
|------|------|
| `update_tile_set(Ref<TileSet>)` | 设置关联的 TileSet |
| `_update_lists()` | 刷新三个代理列表 |
| `_update_enabled_property_editors()` | 根据选择的层级启用/禁用对应的属性编辑器 |
| `_add_button_pressed()` | 添加新的代理绑定 |
| `_delete_selected_bindings()` | 删除选中的代理绑定 |
| `_clear_invalid_button_pressed()` | 清除所有无效代理（指向不存在 source/coords/alternative 的） |
| `_clear_all_button_pressed()` | 清除所有代理 |

---

## 十一、AtlasMergingDialog（图集合并对话框）

**文件**: `atlas_merging_dialog.h/.cpp`  
**类**: `AtlasMergingDialog : ConfirmationDialog`

用于将多个 `TileSetAtlasSource` 合并到一个新的纹理图集中。

### UI

```
ConfirmationDialog (AtlasMergingDialog)
├── atlas_merging_atlases_list (ItemList)  [源图集列表，可多选]
├── columns_editor_property (EditorPropertyInteger)  [每行列数设置]
├── preview (TextureRect)  [合并后的纹理预览]
├── merge_button [Button, 确认合并]
└── select_2_atlases_label (Label, 提示至少选 2 个)
```

### 核心方法

| 方法 | 说明 |
|------|------|
| `update_tile_set(Ref<TileSet>)` | 设置关联的 TileSet |
| `_generate_merged(Vector<Ref<TileSetAtlasSource>>, int)` | 生成合并纹理：将所有源纹理拼接到一张大图上，记录映射关系 |
| `_update_texture()` | 更新预览纹理显示 |
| `_merge_confirmed(String)` | 确认合并：将合并结果保存为新图集源并添加到 TileSet |
| `ok_pressed()` | 弹出保存文件对话框选择保存路径 |
| `_property_changed()` | 列数设置属性变更 |

---

## 十二、核心数据模型（scene/resources/2d/tile_set.h/.cpp）

### 类层次

```
Resource
├── TileMapPattern         — 图案（cell 坐标 → TileMapCell 的映射）
└── TileSet                — 主资源（形状/布局/源管理/层管理/地形/代理/图案）

Resource (TileSetSource 基类)
├── TileSetAtlasSource             — 图集源（纹理 + 网格 + 替代瓦片 + 动画）
└── TileSetScenesCollectionSource  — 场景集合源（场景文件列表）

Object
└── TileData                       — 单个 tile 的全部数据
```

### 关键常量

| 常量 | 值 | 定义位置 |
|------|-----|---------|
| `TileSet::INVALID_SOURCE` | `-1` | tile_set.cpp:321 |
| `TileSetSource::INVALID_ATLAS_COORDS` | `Vector2i(-1, -1)` | tile_set.cpp:3356 |
| `TileSetSource::INVALID_TILE_ALTERNATIVE` | `-1` | tile_set.cpp:3357 |
| `TileSetAtlasSource::TRANSFORM_FLIP_H` | `1 << 12` | tile_set.h:632 |
| `TileSetAtlasSource::TRANSFORM_FLIP_V` | `1 << 13` | tile_set.h:633 |
| `TileSetAtlasSource::TRANSFORM_TRANSPOSE` | `1 << 14` | tile_set.h:634 |

### ID 生成机制

| ID 类型 | 所属类 | 初始值 | 模数 |
|---------|--------|--------|------|
| `next_source_id` | TileSet | 0 | `1073741824 (2³⁰)` |
| `next_alternative_id` | TileSetAtlasSource (per tile) | 1 | `1073741823 (2³⁰−1)` |
| `next_scene_id` | TileSetScenesCollectionSource | 1 | `1073741823 (2³⁰−1)` |

所有 ID 生成遵循相同 pattern：`while (container.has(next_id)) { next_id = (next_id + 1) % MODULUS }`

### TileSet 数据结构（tile_set.h:314-391）

```cpp
// 基础形状与布局
TileShape tile_shape = TILE_SHAPE_SQUARE;
TileLayout tile_layout = TILE_LAYOUT_STACKED;
TileOffsetAxis tile_offset_axis = TILE_OFFSET_AXIS_HORIZONTAL;
Size2i tile_size = Size2i(16, 16);

// 渲染
bool uv_clipping = false;
Vector<OcclusionLayer> occlusion_layers;

// 物理
Vector<PhysicsLayer> physics_layers;

// 地形
Vector<TerrainSet> terrain_sets;

// 导航
Vector<NavigationLayer> navigation_layers;

// 自定义数据
Vector<CustomDataLayer> custom_data_layers;

// 源管理
HashMap<int, Ref<TileSetSource>> sources;
Vector<int> source_ids;
int next_source_id = 0;

// 图案
LocalVector<Ref<TileMapPattern>> patterns;

// Tile 代理（三级）
RBMap<int, int> source_level_proxies;
RBMap<Array, Array> coords_level_proxies;
RBMap<Array, Array> alternative_level_proxies;
```

### TileSetAtlasSource 核心数据（tile_set.h:639-665）

```cpp
struct TileAlternativesData {
    Vector2i size_in_atlas = Vector2i(1, 1);  // tile 在图集中的大小
    Vector2i texture_offset;                    // 纹理偏移
    int animation_columns = 0;                  // 动画列数
    Vector2i animation_separation;              // 动画帧间距
    real_t animation_speed = 1.0;               // 动画速度
    TileAnimationMode animation_mode;            // 动画模式
    LocalVector<real_t> animation_frames_durations; // 每帧时长
    HashMap<int, TileData *> alternatives;       // 替代 tile 映射
    Vector<int> alternatives_ids;                // 排序后的替代 ID 列表
    int next_alternative_id = 1;                 // 下一个可用 ID
};

Ref<Texture2D> texture;             // 源纹理
Vector2i margins;                   // 纹理边距
Vector2i separation;                // 瓦片间距
Size2i texture_region_size;         // 每个瓦片区域大小 (默认 16x16)
bool use_texture_padding = true;    // 是否使用纹理填充（防止 mipmap 溢出）
```

### TileData 数据结构（tile_set.h:846-1036）

```cpp
bool flip_h, flip_v, transpose;          // 变换
Vector2i texture_origin;                 // 纹理原点
Ref<Material> material;                  // 材质
Color modulate;                          // 色调
int z_index;                             // Z 排序
int y_sort_origin;                       // Y 排序原点
Vector<OcclusionLayerTileData> occluders; // 遮挡多边形（每层）
Vector<PhysicsLayerTileData> physics;    // 碰撞多边形（每层）
int terrain_set = -1;                    // 地形集
int terrain = -1;                        // 地形
int terrain_peering_bits[16];            // 地形邻接位
Vector<NavigationLayerTileData> navigation; // 导航多边形（每层）
double probability = 1.0;                // 散布概率
Vector<Variant> custom_data;             // 自定义数据
```

---

## 十三、EditorPropertyTilePolygon 与 EditorInspectorPluginTileData

**定义在**: `tile_set_atlas_source_editor.h:305-333`

### EditorPropertyTilePolygon

自定义 Inspector 属性编辑器，用于编辑导航/遮挡多边形：
- **单模式** (`setup_single_mode`)：单个多边形属性（如遮挡层）
- **多模式** (`setup_multiple_mode`)：多个多边形属性，计数属性 + 元素模板（如碰撞层的多个多边形）
- 包含 `GenericTilePolygonEditor` 实例

### EditorInspectorPluginTileData

TileData 的 Inspector 插件：
- `can_handle()`：处理 `TileData` 对象
- `parse_property()`：为 `occlusion_layer_*_polygon`、`navigation_polygon_*` 等属性创建 `EditorPropertyTilePolygon` 编辑器

---

## 十四、插件注册与编辑器集成

### 注册入口

`editor/register_editor_types.cpp:269-270`：
```cpp
EditorPlugins::add_by_type<TileSetEditorPlugin>();
EditorPlugins::add_by_type<TileMapEditorPlugin>();
```

### 构建系统

`editor/scene/2d/tiles/SCsub`：
```python
env.add_source_files(env.editor_sources, "*.cpp")
```
自动包含目录下所有 `.cpp` 文件。

### 主题集成

`editor/themes/theme_modern.cpp:2900-2905`：
```cpp
// 为 TileSetEditor 的 expanded_area 提供特定样式（因 Tree 透明背景冲突）
Ref<StyleBoxFlat> tile_expand_style = p_config.base_style->duplicate();
tile_expand_style->set_corner_radius_all(0);
p_theme->set_stylebox("expand_panel", "TileSetEditor", tile_expand_style);
```

### 跨编辑器联动

1. 选择 TileMapLayer → `TileMapEditorPlugin::_edit_tile_map_layer()` → 自动调用 `TileSetEditorPlugin::edit(tile_set)` + `open_editor()`
2. TileMapLayer 的 TileSet 变更 → `_update_tile_map()` → 自动打开/关闭 TileSet 编辑器
3. `TilesEditorUtils` 单例同步多个编辑器的源列表选中状态和图集视图变换
4. `TileSetEditor::close()` 同时关闭所有展开的编辑器

---

## 十五、设计模式总结

| 模式 | 应用 |
|------|------|
| **单例 (Singleton)** | `TileSetEditor::singleton`、`TilesEditorUtils::singleton`、`tile_set_plugin_singleton`、`TileMapPlugin` |
| **策略 (Strategy)** | `TileMapLayerSubEditorPlugin` 基类 + Tiles/Terrains 两个实现 |
| **代理 (Proxy)** | `TileSetAtlasSourceProxyObject`、`AtlasTileProxyObject`、`TileSetScenesCollectionProxyObject`、`SceneTileProxyObject` — 用 `RefCounted` 对象包装 TileSet 内部数据，使其可通过 Inspector 编辑 |
| **观察者 (Observer)** | EventBus 的 `changed` 信号链：TileSet → TileSetEditor → TileSetAtlasSourceEditor |
| **回调 (Callback)** | `EditorNode::get_editor_data().add_undo_redo_inspector_hook_callback()` — 注入 UndoRedo 处理 |
| **模板方法 (Template Method)** | `TileDataEditor` 基类定义绘制/输入虚方法，子类实现具体属性操作 |
| **延迟更新 (Deferred Update)** | `tile_set_changed_needs_update` 标记 + `NOTIFICATION_INTERNAL_PROCESS` — 批量处理减少刷新次数 |
| **线程 (Thread)** | `TilesEditorUtils` 的图案预览线程（`pattern_preview_thread`）+ 信号同步 |
| **宏 (Macro)** | `ADD_TILE_DATA_EDITOR_GROUP` / `ADD_TILE_DATA_EDITOR` — 简化 Tree 构建 |