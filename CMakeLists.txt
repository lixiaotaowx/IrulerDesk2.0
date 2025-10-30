cmake_minimum_required(VERSION 3.16)
project(ScreenStreamApp)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置编译器编码为UTF-8
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 设置正确的Qt路径
set(CMAKE_PREFIX_PATH "C:/Qt/6.8.3/msvc2022_64")

# 自动处理 Qt 的 MOC, UIC, RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 查找 Qt 组件
find_package(Qt6 COMPONENTS Core Widgets WebSockets Gui Network Svg REQUIRED)

# --- VP9视频编解码支持 ---
# 设置 libvpx 的安装路径
set(unofficial-libvpx_DIR "G:/rust_projects/vcpkg/installed/x64-windows-static-md/share/unofficial-libvpx")
find_package(unofficial-libvpx REQUIRED)

# 设置 libyuv 的安装路径
set(libyuv_DIR "G:/rust_projects/vcpkg/installed/x64-windows-static-md/share/libyuv")
find_package(libyuv REQUIRED)

# 主程序源文件
set(MAIN_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/video_components/VideoDisplayWidget.cpp
    src/video_components/VideoDisplayWidget.h
    src/VideoWindow.cpp
    src/VideoWindow.h
    src/ui/TransparentImageList.cpp
    src/ui/TransparentImageList.h
    src/ui/AvatarSettingsWindow.cpp
    src/ui/AvatarSettingsWindow.h
)

# 屏幕捕获编码进程源文件
set(CAPTURE_SOURCES
    src/capture/main_capture.cpp
    src/capture/ScreenCapture.cpp
    src/capture/ScreenCapture.h
    src/capture/VP9Encoder.cpp
    src/capture/VP9Encoder.h
    src/capture/WebSocketClient.cpp
    src/capture/WebSocketClient.h
    src/capture/WebSocketSender.cpp
    src/capture/WebSocketSender.h
    src/capture/MouseCapture.cpp
    src/capture/MouseCapture.h
)

# 解码播放进程源文件
set(PLAYER_SOURCES
    src/player/main_player.cpp
    src/player/VP9Decoder.cpp
    src/player/VP9Decoder.h
    src/player/DxvaVP9Decoder.cpp
    src/player/DxvaVP9Decoder.h
    src/player/VideoRenderer.cpp
    src/player/VideoRenderer.h
    src/player/WebSocketReceiver.cpp
    src/player/WebSocketReceiver.h
)




# 视频组件库源文件
set(VIDEO_COMPONENTS_SOURCES
    src/video_components/VideoDisplayWidget.cpp
    src/video_components/VideoDisplayWidget.h
    src/player/VP9Decoder.cpp
    src/player/VP9Decoder.h
    src/player/DxvaVP9Decoder.cpp
    src/player/DxvaVP9Decoder.h
    src/player/WebSocketReceiver.cpp
    src/player/WebSocketReceiver.h
)

# 创建主程序可执行文件
add_executable(ScreenStreamApp ${MAIN_SOURCES} ${VIDEO_COMPONENTS_SOURCES})
# add_executable(ScreenStreamApp WIN32 ${MAIN_SOURCES} ${VIDEO_COMPONENTS_SOURCES})

# 创建屏幕捕获编码进程可执行文件
add_executable(CaptureProcess ${CAPTURE_SOURCES})

# 创建解码播放进程可执行文件
add_executable(PlayerProcess ${PLAYER_SOURCES} ${VIDEO_COMPONENTS_SOURCES})

# 链接主程序库
target_link_libraries(ScreenStreamApp PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::WebSockets
    Qt6::Gui
    Qt6::Network
    Qt6::Svg
    unofficial::libvpx::libvpx
    yuv
)

# 设置主程序为Windows子系统，完全隐藏控制台窗口
set_target_properties(ScreenStreamApp PROPERTIES
    WIN32_EXECUTABLE TRUE
    LINK_FLAGS "/SUBSYSTEM:WINDOWS"
)

# 链接捕获进程库
target_link_libraries(CaptureProcess PRIVATE
    Qt6::Core
    Qt6::WebSockets
    Qt6::Gui
    unofficial::libvpx::libvpx
    yuv
)

# 链接播放进程库
target_link_libraries(PlayerProcess PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::WebSockets
    Qt6::Gui
    unofficial::libvpx::libvpx
    yuv
)

# 添加Windows特定库
if(WIN32)
    target_link_libraries(CaptureProcess PRIVATE d3d11 dxgi)
    target_link_libraries(ScreenStreamApp PRIVATE d3d11 dxva2 ole32)
    target_link_libraries(PlayerProcess PRIVATE d3d11 dxva2 ole32)
    message(STATUS "检测到Windows平台，正在链接d3d11、dxgi、dxva2和ole32")
endif()

# 设置输出目录
set_target_properties(ScreenStreamApp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
set_target_properties(CaptureProcess PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
set_target_properties(PlayerProcess PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# 自动部署Qt DLL
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${CMAKE_PREFIX_PATH}/bin"
        PATHS "C:/Qt/6.8.3/msvc2022_64/bin"
    )

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ScreenStreamApp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Deploying Qt dependencies..."
            COMMAND ${WINDEPLOYQT_EXECUTABLE}
                --verbose 2
                --no-compiler-runtime
                --no-system-d3d-compiler
                $<TARGET_FILE:ScreenStreamApp>
            COMMENT "Deploying Qt libraries and plugins"
        )
        message(STATUS "Found windeployqt: ${WINDEPLOYQT_EXECUTABLE}")
    else()
        message(WARNING "windeployqt not found! Qt dependencies will not be deployed automatically.")
    endif()

    # 复制头像文件到输出目录
    add_custom_command(TARGET ScreenStreamApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying avatar files..."
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/src/maps
            $<TARGET_FILE_DIR:ScreenStreamApp>/maps
        COMMENT "Copying avatar files to output directory"
    )
endif()