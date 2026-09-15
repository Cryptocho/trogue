# tools/ —— 离线占位资产工具（本文件是模板自有文件）。
#
# 何时需要：美术/瓦片集尚未就位时，用这两个工具把「网格」变成能跑的资产：
#   python3 tools/placeholder_tileset.py [--name placeholder]   # 生成已标注的占位 tro-tileset + 贴图
#   ./build/tools/trogue_scene_gen <spec.json> <out_scene.json> # 网格 → autotile 采样 → tro-scene
# 不想用就整个删掉 tools/（game/ 不依赖它）。
#
# 游戏自己的测试（把纯逻辑模块编成无窗口用例）请加在 game/CMakeLists.txt。

add_executable(trogue_scene_gen scene_gen.cpp)
target_link_libraries(trogue_scene_gen PRIVATE trogue_engine)
