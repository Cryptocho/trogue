# tools/ —— 无窗口测试与 consumer smoke（仅当 TROGUE_BUILD_CONSUMER_SMOKES
# AND BUILD_TESTING 时由顶层 CMakeLists 加入）。
#
# 本文件是**模板自有**文件（不随 template/scripts/sync_from_source.sh 覆盖）：
# 新项目若新增引擎级测试，在此登记；game 层测试由本游戏自行新建（模板不含
# roguelike 的 game_core_test）。
#
# 测试目标分工：
#   - 含私有 detail 访问的单测（schema 解析、seam）链接 trogue_engine_test，
#     并 include engine/src/。
#   - 纯公共 API 的消费者验证链接生产 trogue_engine。
# 所有测试 working directory = ${CMAKE_SOURCE_DIR}（资产路径相对项目根约定）。

# 单测需要构造/解析 JSON：复用系统的 nlohmann_json 包（与 engine 相同的查找回退）
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    find_path(NLOHMANN_JSON_INC nlohmann/json.hpp REQUIRED)
    add_library(trogue_tools_nlohmann INTERFACE)
    target_include_directories(trogue_tools_nlohmann INTERFACE ${NLOHMANN_JSON_INC})
    set(TROGUE_TOOLS_NLOHMANN trogue_tools_nlohmann)
else()
    set(TROGUE_TOOLS_NLOHMANN nlohmann_json::nlohmann_json)
endif()

# ── schema 拒绝规则全集 + 加载回归（链接测试库，访问 detail seam） ──
add_executable(trogue_scene_schema_test tests/scene_schema_test.cpp
                                         tests/test_util.hpp)
target_link_libraries(trogue_scene_schema_test PRIVATE trogue_engine_test
                                                    ${TROGUE_TOOLS_NLOHMANN})
target_include_directories(trogue_scene_schema_test PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/src)  # seam 头（scene_test_seams.hpp）
add_test(NAME scene_schema_test COMMAND trogue_scene_schema_test)
set_tests_properties(scene_schema_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── tile 查询/asset_id 单测（链接测试库，访问 asset_test_* seam） ──
add_executable(trogue_scene_query_test tests/scene_query_test.cpp
                                       tests/test_util.hpp)
target_link_libraries(trogue_scene_query_test PRIVATE trogue_engine_test
                                                   ${TROGUE_TOOLS_NLOHMANN})
target_include_directories(trogue_scene_query_test PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/src)
add_test(NAME scene_query_test COMMAND trogue_scene_query_test)
set_tests_properties(scene_query_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── render 三段计数 seam 单测 ──
add_executable(trogue_render_test tests/render_test.cpp tests/test_util.hpp)
target_link_libraries(trogue_render_test PRIVATE trogue_engine_test
                                                 ${TROGUE_TOOLS_NLOHMANN})
target_include_directories(trogue_render_test PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/src)  # seam 头（scene_test_seams.hpp）
add_test(NAME render_test COMMAND trogue_render_test)
set_tests_properties(render_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── 动画/Tween 虚拟时钟单测（纯公共 API） ──
add_executable(trogue_anim_tween_test tests/anim_tween_test.cpp tests/test_util.hpp)
target_link_libraries(trogue_anim_tween_test PRIVATE trogue_engine
                                                     ${TROGUE_TOOLS_NLOHMANN})
add_test(NAME anim_tween_test COMMAND trogue_anim_tween_test)
set_tests_properties(anim_tween_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── terrain 匹配表/选择器单测（纯公共 API） ──
add_executable(trogue_terrain_test tests/terrain_test.cpp tests/test_util.hpp)
target_link_libraries(trogue_terrain_test PRIVATE trogue_engine
                                                 ${TROGUE_TOOLS_NLOHMANN})
add_test(NAME terrain_test COMMAND trogue_terrain_test)
set_tests_properties(terrain_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── pixellab 映射单测（无网络 Python 单测包一层 ctest） ──
add_test(NAME pixellab_mapping_test
    COMMAND python3 -m unittest discover -s ${CMAKE_SOURCE_DIR}/pixellab/tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/pixellab)
# scene_gen 确定性回归依赖已构建的 trogue_scene_gen 二进制
set_tests_properties(pixellab_mapping_test PROPERTIES
    ENVIRONMENT "TROGUE_SCENE_GEN=$<TARGET_FILE:trogue_scene_gen>")

# ── scene_gen 离线场景生成 CLI（复用 pick_tile） ──
add_executable(trogue_scene_gen scene_gen.cpp)
target_link_libraries(trogue_scene_gen PRIVATE trogue_engine
                                              ${TROGUE_TOOLS_NLOHMANN})
# 无窗口纯离线工具；不注册 ctest（E2E 由 pixellab/ 侧驱动）

# ── Watcher 分类纯函数 + Release 桩 + Ipc 集成 ──
add_executable(trogue_watcher_ipc_test tests/watcher_ipc_test.cpp tests/test_util.hpp)
target_link_libraries(trogue_watcher_ipc_test PRIVATE trogue_engine
                                                     ${TROGUE_TOOLS_NLOHMANN})
add_test(NAME watcher_ipc_test COMMAND trogue_watcher_ipc_test)
set_tests_properties(watcher_ipc_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── Ipc 事件通道单测 ──
add_executable(trogue_ipc_test tests/ipc_test.cpp tests/test_util.hpp)
target_link_libraries(trogue_ipc_test PRIVATE trogue_engine
                                              ${TROGUE_TOOLS_NLOHMANN})
add_test(NAME ipc_test COMMAND trogue_ipc_test)
set_tests_properties(ipc_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── consumer smoke：两种对象模型（OOP / ECS）消费生产库纯公共 API ──
add_executable(trogue_oop_client_smoke tests/oop_client_smoke.cpp tests/test_util.hpp)
target_link_libraries(trogue_oop_client_smoke PRIVATE trogue_engine
                                                      ${TROGUE_TOOLS_NLOHMANN})
add_test(NAME oop_client_smoke COMMAND trogue_oop_client_smoke)
set_tests_properties(oop_client_smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(trogue_ecs_client_smoke tests/ecs_client_smoke.cpp tests/test_util.hpp)
target_link_libraries(trogue_ecs_client_smoke PRIVATE trogue_engine
                                                      ${TROGUE_TOOLS_NLOHMANN})
add_test(NAME ecs_client_smoke COMMAND trogue_ecs_client_smoke)
set_tests_properties(ecs_client_smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
