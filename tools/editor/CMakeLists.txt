cmake_minimum_required(VERSION 3.20)
project(trogue-tileset-editor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(imgui REQUIRED)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/texture_loader.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE include)

target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL3::SDL3-shared
    SDL3_image::SDL3_image-shared
    imgui::imgui
)

option(BUILD_TESTS "Build test executables" ON)

if(BUILD_TESTS)
    add_executable(${PROJECT_NAME}_tests
        tests/test_data_model.cpp
    )
    add_test(NAME DataModel COMMAND ${PROJECT_NAME}_tests)
endif()
