cmake_minimum_required(VERSION 3.24)
project(trogue CXX)

# ── 语言基线（plan-5.1 §2） ──
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 引擎与游戏代码统一零告警基线（plan-5.1 §2：-Wall -Wextra -Wpedantic）
add_compile_options(-Wall -Wextra -Wpedantic)

# ── 测试与选项：顺序固定（plan-5.1 §7.1） ──
include(CTest)                                   # 先于一切（BUILD_TESTING）
option(TROGUE_BUILD_CONSUMER_SMOKES
       "Build consumer smoke executables and no-window unit tests (requires BUILD_TESTING)" ON)
option(TROGUE_DEBUG
       "Debug tooling (ipc/watcher 桩化；Animation/Tween 为通用功能不桩化)" ON)

add_subdirectory(engine)
# tools/ 在首个无窗口测试落地时加入（5.2 起）；smoke 与 BUILD_TESTING 共同门控
if(TROGUE_BUILD_CONSUMER_SMOKES AND BUILD_TESTING
   AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt)
  add_subdirectory(tools)
endif()
add_subdirectory(game)