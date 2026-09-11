# tools/ —— 离线场景生成 CLI（pixellab 管线用；本文件是模板自有文件）。
#
# 何时需要：用 `python3 pixellab/pxlab.py import-map` 把 ASCII 地形网格转成
# tro-scene 时，pixellab/scene.py 会调用本目标产出的二进制
# （build/tools/trogue_scene_gen）。不用地图管线可以整个删掉 tools/。
#
# 游戏自己的测试（把纯逻辑模块编成无窗口用例）请加在 game/CMakeLists.txt。

add_executable(trogue_scene_gen scene_gen.cpp)
target_link_libraries(trogue_scene_gen PRIVATE trogue_engine)
