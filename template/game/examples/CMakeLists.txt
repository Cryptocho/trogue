# game/examples/ —— 两个范式范例（模板自有文件；整目录可删）。
#
# 目的：同一份引擎公共 API 能自然支撑两种对象模型——
#   swarm/      类幸存者（同质实体上百个、逐系统遍历）→ ECS 风格
#   platformer/ 单角色平台跳跃（少数对象、富状态机、多态敌人）→ OOP 风格
# 两者都只链接 trogue_engine（只经公共头用引擎）。示例使用 assets/textures/pixellab/
# 下的离线 PixelLab 贴图（PNG + sha256 manifest），场景几何在内存构造（零 JSON 资产）。
# 这是「起手式参考」，不是引擎/模板的推荐架构：不需要就删掉整个 examples/ 目录
# （game/CMakeLists.txt 里的 EXISTS 守卫会让构建照常成立）。
#
# 每个范例两个目标：
#   <name>           窗口可运行（--headless / --seconds / --shot 见 common/harness.hpp）
#   <name>_sim_test  纯逻辑、无窗口，注册进 ctest
# 产物目录 build/bin 由父级 game/CMakeLists.txt 设定（CMake 目录变量向子目录继承），此处不重复声明。

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/swarm/main.cpp)
  add_executable(swarm swarm/main.cpp swarm/sim.cpp)
  target_link_libraries(swarm PRIVATE trogue_engine)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/swarm/sim_test.cpp)
  add_executable(swarm_sim_test swarm/sim_test.cpp swarm/sim.cpp)
  target_link_libraries(swarm_sim_test PRIVATE trogue_engine)
  add_test(NAME swarm_sim_test COMMAND swarm_sim_test)
  set_tests_properties(swarm_sim_test PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/platformer/main.cpp)
  add_executable(platformer platformer/main.cpp platformer/sim.cpp)
  target_link_libraries(platformer PRIVATE trogue_engine)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/platformer/sim_test.cpp)
  add_executable(platformer_sim_test platformer/sim_test.cpp platformer/sim.cpp)
  target_link_libraries(platformer_sim_test PRIVATE trogue_engine)
  add_test(NAME platformer_sim_test COMMAND platformer_sim_test)
  set_tests_properties(platformer_sim_test PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
