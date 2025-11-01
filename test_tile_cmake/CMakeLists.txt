cmake_minimum_required(VERSION 3.16)
project(TileTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加MSVC编译器标志
if(MSVC)
    add_compile_options(/Zc:__cplusplus)
    add_compile_options(/utf-8)
endif()

# 查找Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)

# 设置Qt6自动MOC
set(CMAKE_AUTOMOC ON)

# 包含目录
include_directories(../src/player)

# 创建可执行文件
add_executable(TileTest
    ../simple_tile_test.cpp
    ../src/player/VideoRenderer.cpp
)

# 链接Qt6库
target_link_libraries(TileTest Qt6::Core Qt6::Widgets Qt6::Gui)

# 设置输出目录
set_target_properties(TileTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)