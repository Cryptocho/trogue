cmake_minimum_required(VERSION 3.24)
project(trogue C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 顶层只做聚合：引擎库与游戏层物理分离（见 AGENTS.md 目录结构）
add_subdirectory(engine)
add_subdirectory(game)
