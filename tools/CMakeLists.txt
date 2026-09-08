# tools/ —— 无窗口测试与 consumer smoke（仅当 TROGUE_BUILD_CONSUMER_SMOKES
# AND BUILD_TESTING 时由顶层 CMakeLists 加入；plan-5.1 §7.2）。
#
# 测试目标分工：
#   - 含私有 detail 访问的单测（schema 解析、seam）链接 trogue_engine_test，
#     并 include engine/src/（trogue_engine_test 的接口本来就传播私有 include）。
#   - 纯公共 API 的消费者验证链接生产 trogue_engine（consumer smoke，5.6 起）。
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

# ── render 三段计数 seam 单测（plan-5.3 §8） ──
add_executable(trogue_render_test tests/render_test.cpp tests/test_util.hpp)
target_link_libraries(trogue_render_test PRIVATE trogue_engine_test
                                                 ${TROGUE_TOOLS_NLOHMANN})
target_include_directories(trogue_render_test PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/src)  # seam 头（scene_test_seams.hpp）
add_test(NAME render_test COMMAND trogue_render_test)
set_tests_properties(render_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── 动画/Tween 虚拟时钟单测（plan-5.4 §8；链接生产库即可，纯公共 API） ──
add_executable(trogue_anim_tween_test tests/anim_tween_test.cpp tests/test_util.hpp)
target_link_libraries(trogue_anim_tween_test PRIVATE trogue_engine
                                                     ${TROGUE_TOOLS_NLOHMANN})
add_test(NAME anim_tween_test COMMAND trogue_anim_tween_test)
set_tests_properties(anim_tween_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── Watcher 分类纯函数 + Release 桩 + Ipc 集成（plan-5.5 §4） ──
add_executable(trogue_watcher_ipc_test tests/watcher_ipc_test.cpp tests/test_util.hpp)
target_link_libraries(trogue_watcher_ipc_test PRIVATE trogue_engine
                                                     ${TROGUE_TOOLS_NLOHMANN})
# Debug 需要访问 detail::classify_event_name（私有实现符号，非 seam —— 生产/
# 测试库都带）→ 链接生产 trogue_engine 即可；Release 桩行为（valid==false
# /poll==nullopt）也由同一可执行按 TROGUE_DEBUG 宏分派。
add_test(NAME watcher_ipc_test COMMAND trogue_watcher_ipc_test)
set_tests_properties(watcher_ipc_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── consumer smoke（plan-5.6 §2；无窗口、链接生产 trogue_engine） ──
# 纯公共 API 面（trogue.hpp）的两种对象模型消费验证：证明引擎与使用者模型无关。
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