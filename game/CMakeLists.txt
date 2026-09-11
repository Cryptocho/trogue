# 游戏层：当前为演示应用，将成长为 roguelike 本体。
# 只允许通过 engine/include/trogue/ 公共头使用引擎；资产按 CWD assets/ 约定读取。

# 可执行文件统一输出到 build/bin/（否则会散落在 build/game/）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(trogue src/main.cpp src/anim_util.cpp src/game_core.cpp
                      src/nav.cpp src/rules.cpp src/ai.cpp)
target_link_libraries(trogue PRIVATE trogue_engine)

# 动画查看器：帧动画触发/切换最小交互验证台，独立端点 48765
add_executable(anim_viewer src/anim_util.cpp src/anim_viewer.cpp)
target_link_libraries(anim_viewer PRIVATE trogue_engine)