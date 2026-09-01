cmake_minimum_required(VERSION 3.24)
project(trogue C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# DEBUG 工具链（IPC + 热重载）。发布构建：-DTROGUE_DEBUG=OFF
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

# ── 引擎库 ──
add_library(trogue_engine STATIC
    src/world.c
    src/tileset.c
    src/scene.c
    src/render.c
    src/hotreload.c
    src/ipc.c
)
target_include_directories(trogue_engine PUBLIC include)
target_link_libraries(trogue_engine PUBLIC ${TROGUE_RAYLIB} ${TROGUE_JANSSON})
if(TROGUE_DEBUG)
    target_compile_definitions(trogue_engine PUBLIC TROGUE_DEBUG)
endif()

# ── 演示应用 ──
add_executable(trogue src/main.c)
target_link_libraries(trogue PRIVATE trogue_engine)
