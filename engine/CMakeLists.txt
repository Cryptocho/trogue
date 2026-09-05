cmake_minimum_required(VERSION 3.24)
project(trogue_engine C)

# trogue_engine：基于 raylib 的轻量 2D 引擎库。
# 本目录自包含（依赖查找 + 构建定义），可整体取走独立复用或拆库。
# 游戏层（game/）是它的消费方——引擎概念不得反向流入游戏，游戏概念不得流入引擎。

option(TROGUE_DEBUG "Enable debug IPC & hot-reload tooling" ON)

# ── 依赖：raylib（优先包配置，回退 pkg-config） ──
find_package(raylib 4.0 QUIET)
if(NOT raylib_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RAYLIB REQUIRED IMPORTED_TARGET raylib)
    set(TROGUE_RAYLIB PkgConfig::RAYLIB)
else()
    set(TROGUE_RAYLIB raylib)
endif()

# ── 依赖：jansson（JSON 解析） ──
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(JANSSON IMPORTED_TARGET jansson)
endif()
if(JANSSON_FOUND)
    set(TROGUE_JANSSON PkgConfig::JANSSON)
else()
    find_library(TROGUE_JANSSON_LIB jansson REQUIRED)
    find_path(TROGUE_JANSSON_INC jansson.h REQUIRED)
    add_library(trogue_jansson UNKNOWN IMPORTED)
    set_target_properties(trogue_jansson PROPERTIES
        IMPORTED_LOCATION ${TROGUE_JANSSON_LIB}
        INTERFACE_INCLUDE_DIRECTORIES ${TROGUE_JANSSON_INC})
    set(TROGUE_JANSSON trogue_jansson)
endif()

add_library(trogue_engine STATIC
    src/world.c
    src/tileset.c
    src/scene.c
    src/render.c
    src/hotreload.c
    src/ipc.c
)
target_include_directories(trogue_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(trogue_engine PUBLIC ${TROGUE_RAYLIB} ${TROGUE_JANSSON})
if(TROGUE_DEBUG)
    target_compile_definitions(trogue_engine PUBLIC TROGUE_DEBUG)
endif()
