# 测试程序CMakeLists.txt
# 这个文件管理所有的测试程序，与主项目分离

cmake_minimum_required(VERSION 3.16)

# 设置测试输出目录
set(TEST_OUTPUT_DIR ${CMAKE_BINARY_DIR}/tests)

# 测试程序配置

# WebSocket瓦片传输测试
add_executable(TestWebSocketTiles 
    test_websocket_tiles.cpp
    ${CMAKE_SOURCE_DIR}/src/capture/WebSocketSender.cpp
)
target_link_libraries(TestWebSocketTiles 
    Qt6::Core 
    Qt6::WebSockets 
    Qt6::Gui
)
target_include_directories(TestWebSocketTiles PRIVATE ${CMAKE_SOURCE_DIR}/src/capture)
set_target_properties(TestWebSocketTiles PROPERTIES
    AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}
)

# WebSocket接收器瓦片处理测试
add_executable(TestWebSocketReceiverTiles test_websocket_receiver_tiles.cpp)
target_link_libraries(TestWebSocketReceiverTiles 
    Qt6::Core 
    Qt6::WebSockets 
    Qt6::Gui
)
target_include_directories(TestWebSocketReceiverTiles PRIVATE ${CMAKE_SOURCE_DIR}/src/player)
set_target_properties(TestWebSocketReceiverTiles PROPERTIES
    AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}
)

# 瓦片管理器测试
add_executable(TestTileManager test_tile_manager.cpp)
target_link_libraries(TestTileManager 
    Qt6::Core 
    Qt6::Gui
)
target_include_directories(TestTileManager PRIVATE ${CMAKE_SOURCE_DIR}/src/capture)
set_target_properties(TestTileManager PROPERTIES
    AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}
)

# 瓦片系统测试
add_executable(TestTileSystem test_tile_system.cpp)
target_link_libraries(TestTileSystem 
    Qt6::Core 
    Qt6::Gui
)
target_include_directories(TestTileSystem PRIVATE ${CMAKE_SOURCE_DIR}/src/capture)
set_target_properties(TestTileSystem PROPERTIES
    AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}
)

# WebSocket接收器与VideoRenderer集成测试
add_executable(TestIntegrationWebSocketRenderer 
    test_integration_websocket_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/player/WebSocketReceiver.cpp
    ${CMAKE_SOURCE_DIR}/src/player/VideoRenderer.cpp
)
target_link_libraries(TestIntegrationWebSocketRenderer 
    Qt6::Core 
    Qt6::Widgets
    Qt6::WebSockets 
    Qt6::Gui
)
target_include_directories(TestIntegrationWebSocketRenderer PRIVATE 
    ${CMAKE_SOURCE_DIR}/src/player
    ${CMAKE_SOURCE_DIR}/src
)
set_target_properties(TestIntegrationWebSocketRenderer PROPERTIES
    AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}
)