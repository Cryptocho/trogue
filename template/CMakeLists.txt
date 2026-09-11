cmake_minimum_required(VERSION 3.24)
project(trogue_game CXX)

# ── 语言基线 ──
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)            # clangd 自动使用

# 引擎与游戏代码统一零告警基线
add_compile_options(-Wall -Wextra -Wpedantic)

# ── 测试与选项 ──
include(CTest)                                   # 先于一切（BUILD_TESTING）
option(TROGUE_BUILD_CONSUMER_SMOKES
       "Build consumer smoke executables and no-window unit tests (requires BUILD_TESTING)" ON)
option(TROGUE_DEBUG
       "Debug tooling (ipc/watcher 桩化；Animation/Tween 为通用功能不桩化)" ON)

add_subdirectory(engine)
# 引擎级无窗口测试（tools/）；game 层测试由本游戏自行扩展（见 game/）
if(TROGUE_BUILD_CONSUMER_SMOKES AND BUILD_TESTING
   AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt)
  add_subdirectory(tools)
endif()
add_subdirectory(game)
