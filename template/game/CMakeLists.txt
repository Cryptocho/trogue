# 游戏层：起步骨架（在此成长为你的游戏）。
# 只允许通过 engine/include/trogue/ 公共头使用引擎；资产按 CWD assets/ 约定读取。

# 可执行文件统一输出到 build/bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(trogue src/main.cpp)
target_link_libraries(trogue PRIVATE trogue_engine)

# 游戏层无窗口单测：把纯逻辑模块编进测试目标（示例注释，按需启用）。
# 建议把输入/规则/AI 等无渲染逻辑抽成独立 .cpp（不含 raylib），再这样测试：
#
# add_executable(my_game_test tests/my_game_test.cpp
#                src/game_core.cpp)
# target_link_libraries(my_game_test PRIVATE trogue_engine)
# target_include_directories(my_game_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# add_test(NAME my_game_test COMMAND my_game_test)
# set_tests_properties(my_game_test PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
# 顶层 CMakeLists.txt 已 include(CTest)，取消注释即可被 ctest 收集。
#
# 可选 CJK 烘焙字体参考实现：只有需要图集字体时才把 ui_font.cpp 加入目标：
# target_sources(trogue PRIVATE src/ui_font.cpp)
# target_include_directories(trogue PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
