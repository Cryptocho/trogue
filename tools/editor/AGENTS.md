# Trogue TileSet Editor 工作指南

参考根目录 `AGENTS.md` 的开发流程（步骤 1~7）和 CHANGELOG 格式规范。以下仅列出编辑器特有的上下文。

## 技术栈

- **C++17** — 纯 struct，无 class 继承，无 OOP。头文件用 `#pragma once`。
- **SDL3** + **SDL3_image** + **Dear ImGui**（SDLRenderer3 后端）
- **CMake 3.20+** — find_package 链接系统包，不源码构建依赖

## 构建与运行

```bash
# 构建（首次或改 CMakeLists.txt 后需要 cmake -B build）
cmake -B build && cmake --build build

# 快速增量编译（只改 .hpp/.cpp 时）
cmake --build build

# 运行
./build/trogue-tileset-editor

# 运行测试
cmake --build build --target trogue-tileset-editor_tests && ./build/trogue-tileset-editor_tests
```

## 源码结构

```
tools/editor/
├── CMakeLists.txt          # add_executable 含 src/*.cpp；tests 用 BUILD_TESTS 独立目标
├── PLAN.md                 # 10 阶段开发计划（所有实现以此为源）
├── include/
│   ├── tileset.hpp         # 数据模型（TileSetSource → OcclusionRegion → TileData → TileSet → MapCell → GameMap）
│   └── texture_loader.hpp  # 纹理加载函数声明
├── src/
│   ├── main.cpp            # 入口 + SDL3/ImGui 循环
│   └── texture_loader.cpp  # 纹理加载、路径解析实现
├── tests/
│   └── test_data_model.cpp # 数据模型单元测试
```

## 关键约定

- **头文件放 `include/`，实现放 `src/`** — 所有 `.hpp` 文件放在根 `include/` 目录，CMake 通过 `target_include_directories` 添加搜索路径
- **Godot 参考源码在 `../../temp/godot/`** — 所有阶段设计参考 Godot TileSet 编辑器
- **主计划是 `PLAN.md`** — 而非 `PLAN-phase2.md`（后者是子步骤文档）
- **`tileset.hpp` 依赖顺序固定**：TileSetSource → OcclusionRegion → TileData → TileSet → MapCell → GameMap
- **索引哨兵统一用 `-1`** — 空值/无效索引全部为 -1（如 `sourceIndex`, `tileSetIndex`, `tileId`）
- **`GameMap::cells` 是 row-major**：`cells[row * width + col]`
- **`std::array<int,2>` 而非 `int[2]`** — 类型安全，可直接传给 ImGui

## 各阶段输入/输出

| 阶段 | 目标 | 关键产出 |
|------|------|---------|
| 1 | SDL3 + ImGui 窗口 | `main.cpp` |
| 2 | 数据模型 | `tileset.hpp` |
| 3 | 纹理加载 | `texture_loader.hpp/.cpp` |
| 4 | 图集视图 | `atlas_view.hpp/.cpp`, `editor_state.hpp` |
| 5 | Tile 创建/选择 | 扩充 atlas_view |
| 6 | 属性面板 | `inspector.hpp/.cpp` |
| 7 | 遮蔽标记 | 扩充 inspector + atlas_view |
| 8 | 地图编辑器 | `map_editor.hpp/.cpp` |
| 9 | Lua 导出 | `export_lua.hpp/.cpp` |
| 10 | 游戏集成 | 修改 `src/` 下 Lua 文件 |