cmake_minimum_required(VERSION 3.24)
project(trogue_engine CXX)

# trogue_engine：基于 raylib 的轻量引擎库。
# 本目录自包含（依赖查找 + 构建定义），可整体取走独立复用或拆库。
# 游戏层（game/）是它的消费方——引擎概念不得反向流入游戏，游戏概念不得流入引擎。

# ── 语言基线：独立 configure（cmake -S engine）与 add_subdirectory 两种用法下一致 ──
# CMAKE_CXX_STANDARD 等是目录作用域变量：根 CMakeLists 的设置只在本目录被 add_subdirectory
# 引入时继承；独立构建不经过根文件，缺了这里就没有 -std=c++20（GCC 15 默认 gnu++17，
# <coroutine>/coro.hpp 直接 #error「requires -fcoroutines」），告警基线同样缺失。
# set 同值变量对 add_subdirectory 用法无副作用（目录作用域覆盖，不产生重复 flag）；
# 告警只在独立构建时自备——经根引入时根已提供，避免编译行出现两份 -Wall/-Wextra/-Wpedantic。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

option(TROGUE_DEBUG "Enable debug IPC & hot-reload tooling" ON)

# ── 依赖：raylib（优先包配置，回退 pkg-config） ──
# 引擎内部直接调 raylib C API（不引入 raylib-cpp，AGENTS「引擎实现语言决策」）；
# 公共 API 不暴露 raylib 类型，此处链接供 engine 内部模块与显式使用 raylib 的 game 用。
find_package(raylib 4.0 QUIET)
if(NOT raylib_FOUND)
    # 常见安装前缀兜底（Gentoo 等把 raylib-config.cmake 放 /usr/lib64/cmake/raylib，
    # 部分 cmake 版本默认搜索不含该目录）
    find_path(RAYLIB_CMAKE_DIR raylib-config.cmake
        PATHS /usr/lib64/cmake/raylib /usr/lib/cmake/raylib
              /usr/local/lib64/cmake/raylib /usr/local/lib/cmake/raylib
        NO_DEFAULT_PATH)
    if(RAYLIB_CMAKE_DIR)
        set(raylib_DIR "${RAYLIB_CMAKE_DIR}" CACHE PATH "raylib cmake dir" FORCE)
        find_package(raylib 4.0 QUIET)
    endif()
endif()
if(NOT raylib_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RAYLIB REQUIRED IMPORTED_TARGET raylib)
    set(TROGUE_RAYLIB PkgConfig::RAYLIB)
else()
    set(TROGUE_RAYLIB raylib)
endif()

# ── 依赖：nlohmann/json（替换 jansson；plan-5.1 §5.1，校验语义不放宽） ──
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    # 回退：仅提供 include 路径的 INTERFACE 库
    find_path(NLOHMANN_JSON_INC nlohmann/json.hpp REQUIRED)
    add_library(trogue_nlohmann INTERFACE)
    target_include_directories(trogue_nlohmann INTERFACE ${NLOHMANN_JSON_INC})
    set(TROGUE_NLOHMANN trogue_nlohmann)
else()
    set(TROGUE_NLOHMANN nlohmann_json::nlohmann_json)
endif()

# ── 依赖：tl::expected（系统包/包管理器安装；错误载体，plan-5.1 §6） ──
# types.hpp 的 tg::expected/ErrorOr 别名直接把 tl::expected 暴露给公共 API 消费方，
# 因此 tl::expected 随库 PUBLIC 链接传播（consumer 必须能编译 <tl/expected.hpp>）。
# 注意：缺失时**不要自行安装**——通知用户用包管理器安装（AGENTS「依赖管理」）。
find_package(tl-expected REQUIRED)

# ── 引擎实现源文件（分卷 5.2~5.5 按 plan-5.1 §4 布局逐个落地） ──
set(ENGINE_SOURCES
    src/engine.cpp
    src/scene_asset.cpp
    src/render.cpp
    src/animation.cpp
    src/tween.cpp
    src/hotreload.cpp
    src/ipc.cpp
    src/terrain.cpp
    src/util/json_check.cpp
    src/util/path_check.cpp
    src/util/color_check.cpp
)

# ── 生产库：不带测试 seam ──
add_library(trogue_engine STATIC ${ENGINE_SOURCES})
target_include_directories(trogue_engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src   # 私有 detail 头（util/*.hpp 等）
)
target_link_libraries(trogue_engine
    # nlohmann 必须 PUBLIC：公共伞头 trogue.hpp → ipc.hpp 直接 #include
    # <nlohmann/json.hpp> 且 tg::Json 是公共别名，consumer 需要该 include 路径
    # （plan-5.1 §7.1 明确 PUBLIC 链接）。
    PUBLIC  ${TROGUE_RAYLIB} tl::expected ${TROGUE_NLOHMANN}
)
if(TROGUE_DEBUG)
    target_compile_definitions(trogue_engine PUBLIC TROGUE_DEBUG)
endif()

# ── 测试专用库（plan-5.1 §7.2 seam 契约）：仅当 smoke+testing 时构建 ──
# 同一源文件 + TROGUE_TEST_SEAMS；只有 trogue_engine_test 供测试目标链接，
# 生产 trogue_engine 不含测试符号。trogue_engine_test 额外把 src/ 加入私有
# include（仅测试可达私有 detail 实现）。
if(TROGUE_BUILD_CONSUMER_SMOKES AND BUILD_TESTING)
    add_library(trogue_engine_test STATIC ${ENGINE_SOURCES})
    target_include_directories(trogue_engine_test
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(trogue_engine_test PRIVATE TROGUE_TEST_SEAMS=1)
    target_link_libraries(trogue_engine_test
        PUBLIC  ${TROGUE_RAYLIB} tl::expected ${TROGUE_NLOHMANN}
    )
    if(TROGUE_DEBUG)
        target_compile_definitions(trogue_engine_test PUBLIC TROGUE_DEBUG)
    endif()
endif()
