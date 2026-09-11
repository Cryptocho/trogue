cmake_minimum_required(VERSION 3.24)
project(trogue_game CXX)

# ── 语言基线 ──
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)            # clangd 自动使用

# 引擎与游戏代码统一零告警基线
add_compile_options(-Wall -Wextra -Wpedantic)

# ── 选项：TROGUE_DEBUG 控制 IPC/热重载（Release 下桩化，API 形状不变） ──
option(TROGUE_DEBUG "Enable debug IPC & hot-reload tooling" ON)

add_subdirectory(engine)

# tools/：离线场景生成 CLI（pixellab 管线的 C 机制半，复用引擎 pick_tile）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt)
  add_subdirectory(tools)
endif()

add_subdirectory(game)
