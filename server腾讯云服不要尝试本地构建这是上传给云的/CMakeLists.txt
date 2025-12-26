# 独立WebSocket服务器的CMakeLists.txt
# 使用方法：将此文件重命名为CMakeLists.txt，与websocket_server_with_routing.cpp放在同一目录

cmake_minimum_required(VERSION 3.16)
project(WebSocketServer)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动处理 Qt 的 MOC
set(CMAKE_AUTOMOC ON)

# 查找 Qt 组件
find_package(Qt6 COMPONENTS Core WebSockets Network REQUIRED)

# 如果找不到Qt6，尝试Qt5
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core WebSockets Network REQUIRED)
    set(QT_VERSION_MAJOR 5)
else()
    set(QT_VERSION_MAJOR 6)
endif()

# 创建可执行文件 - 使用带路由功能的服务器
add_executable(WebSocketServer websocket_server_with_routing.cpp)

# 链接Qt库
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(WebSocketServer PRIVATE
        Qt6::Core
        Qt6::WebSockets
        Qt6::Network
    )
else()
    target_link_libraries(WebSocketServer PRIVATE
        Qt5::Core
        Qt5::WebSockets
        Qt5::Network
    )
endif()

# 设置编译选项
if(MSVC)
    target_compile_options(WebSocketServer PRIVATE /utf-8)
else()
    target_compile_options(WebSocketServer PRIVATE -Wall -Wextra)
endif()

# 设置输出目录
set_target_properties(WebSocketServer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# 安装规则
install(TARGETS WebSocketServer
    RUNTIME DESTINATION bin
)

# 打印配置信息
message(STATUS "Qt版本: ${QT_VERSION_MAJOR}")
message(STATUS "编译器: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")

# 如果是Release模式，启用优化
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(WebSocketServer PRIVATE /O2)
    else()
        target_compile_options(WebSocketServer PRIVATE -O3)
    endif()
endif()
