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
find_package(Qt6 COMPONENTS Core Widgets WebSockets Gui Network Svg Multimedia REQUIRED)

# Opus audio codec
set(Opus_DIR "G:/rust_projects/vcpkg/installed/x64-windows-static-md/share/opus")
find_package(Opus CONFIG REQUIRED)

# --- VP9视频编解码支持 ---
# 设置 libvpx 的安装路径
set(unofficial-libvpx_DIR "G:/rust_projects/vcpkg/installed/x64-windows-static-md/share/unofficial-libvpx")
find_package(unofficial-libvpx REQUIRED)

# 设置 libyuv 的安装路径
set(libyuv_DIR "G:/rust_projects/vcpkg/installed/x64-windows-static-md/share/libyuv")
find_package(libyuv REQUIRED)

# 主程序源文件
set(MAIN_SOURCES
    src/main.cpp                          # 程序入口：初始化应用并启动主窗口
    src/MainWindow.cpp                    # 主窗口实现：主界面布局、交互逻辑、控制流程
    src/MainWindow.h                      # 主窗口声明：成员与信号槽声明
    src/video_components/VideoDisplayWidget.cpp # 视频显示控件实现：绘制视频帧与批注事件处理
    src/video_components/VideoDisplayWidget.h   # 视频显示控件声明：接口与状态
    src/VideoWindow.cpp                   # 独立视频窗口实现：承载显示控件、拦截原生事件
    src/VideoWindow.h                     # 独立视频窗口声明：公开接口与成员
    src/ui/TransparentImageList.cpp       # 透明贴图列表实现：管理/选择叠加贴图资源
    src/ui/TransparentImageList.h         # 透明贴图列表声明：信号槽与模型接口
    src/ui/AvatarSettingsWindow.cpp       # 头像设置窗口实现：用户头像资源的配置与预览
    src/ui/AvatarSettingsWindow.h         # 头像设置窗口声明：UI与交互接口
    src/ui/SystemSettingsWindow.cpp       # 系统设置窗口实现：应用参数与行为配置
    src/ui/SystemSettingsWindow.h         # 系统设置窗口声明：设置项与信号槽
    src/ui/ColorCircleButton.cpp          # 圆形颜色按钮：自绘抗锯齿纯圆显示
    src/ui/ColorCircleButton.h            # 圆形颜色按钮声明
)

# 屏幕捕获编码进程源文件
set(CAPTURE_SOURCES
    src/capture/main_capture.cpp          # 捕获进程入口：初始化捕获、编码与传输流程
    src/capture/ScreenCapture.cpp         # 屏幕捕获实现：抓取屏幕帧/区域
    src/capture/ScreenCapture.h           # 屏幕捕获声明
    src/capture/VP9Encoder.cpp            # VP9 编码器实现：将原始帧编码为 VP9
    src/capture/VP9Encoder.h              # VP9 编码器声明
    src/capture/WebSocketClient.cpp       # WebSocket 客户端实现：连接服务器、维护会话
    src/capture/WebSocketClient.h         # WebSocket 客户端声明
    src/capture/WebSocketSender.cpp       # WebSocket 发送端实现：发送瓦片/帧/批注事件
    src/capture/WebSocketSender.h         # WebSocket 发送端声明
    src/capture/MouseCapture.cpp          # 鼠标捕获实现：记录鼠标位置与状态用于批注
    src/capture/MouseCapture.h            # 鼠标捕获声明
    src/capture/TileManager.cpp           # 瓦片管理实现：切片/合并/调度传输
    src/capture/TileManager.h             # 瓦片管理声明
    src/capture/PerformanceMonitor.cpp    # 性能监控实现：统计编码/网络/帧率指标
    src/capture/PerformanceMonitor.h      # 性能监控声明
    src/capture/AnnotationOverlay.cpp     # 批注叠加层实现：在采集端绘制/组合批注图层
    src/capture/AnnotationOverlay.h       # 批注叠加层声明
)

# 解码播放进程源文件
set(PLAYER_SOURCES
    src/player/main_player.cpp            # 播放器进程入口：初始化解码与渲染管线
    src/player/VP9Decoder.cpp             # VP9 软件解码器实现
    src/player/VP9Decoder.h               # VP9 软件解码器声明
    src/player/DxvaVP9Decoder.cpp         # DXVA 硬件加速 VP9 解码实现
    src/player/DxvaVP9Decoder.h           # DXVA VP9 解码器声明
    src/player/VideoRenderer.cpp          # 视频渲染器实现：呈现解码后的帧/瓦片
    src/player/VideoRenderer.h            # 视频渲染器声明
    src/player/WebSocketReceiver.cpp      # WebSocket 接收端实现：接收瓦片与批注事件
    src/player/WebSocketReceiver.h        # WebSocket 接收端声明
)




# 视频组件库源文件
set(VIDEO_COMPONENTS_SOURCES
    src/video_components/VideoDisplayWidget.cpp # 视频显示与批注控件实现（复用到播放器）
    src/video_components/VideoDisplayWidget.h   # 视频显示与批注控件声明
    src/player/VP9Decoder.cpp                   # VP9 软件解码器实现
    src/player/VP9Decoder.h                     # VP9 软件解码器声明
    src/player/DxvaVP9Decoder.cpp               # 基于 DXVA 的硬件加速 VP9 解码实现
    src/player/DxvaVP9Decoder.h                 # DXVA VP9 解码器声明
    src/player/WebSocketReceiver.cpp            # WebSocket 接收端实现：接收瓦片与批注事件
    src/player/WebSocketReceiver.h              # WebSocket 接收端声明
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
    Qt6::Multimedia
    Opus::opus
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
    Qt6::Widgets
    Qt6::WebSockets
    Qt6::Gui
    Qt6::Multimedia
    Opus::opus
    unofficial::libvpx::libvpx
    yuv
)

# 链接播放进程库
target_link_libraries(PlayerProcess PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::WebSockets
    Qt6::Gui
    Qt6::Multimedia
    Opus::opus
    unofficial::libvpx::libvpx
    yuv
)

# 一键禁用所有日志输出（qDebug/qInfo/qWarning），并提供总开关
option(DISABLE_ALL_LOGS "Disable all application logging output" ON)
if(DISABLE_ALL_LOGS)
    message(STATUS "日志输出已禁用: 使用 QT_NO_DEBUG_OUTPUT/INFO/WARNING 并定义 DISABLE_ALL_LOGS")
    target_compile_definitions(ScreenStreamApp PRIVATE DISABLE_ALL_LOGS QT_NO_DEBUG_OUTPUT QT_NO_INFO_OUTPUT QT_NO_WARNING_OUTPUT)
    target_compile_definitions(CaptureProcess PRIVATE DISABLE_ALL_LOGS QT_NO_DEBUG_OUTPUT QT_NO_INFO_OUTPUT QT_NO_WARNING_OUTPUT)
    target_compile_definitions(PlayerProcess PRIVATE DISABLE_ALL_LOGS QT_NO_DEBUG_OUTPUT QT_NO_INFO_OUTPUT QT_NO_WARNING_OUTPUT)
endif()

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

# 可选：添加测试子目录（仅在需要时启用）
# 通过选项控制是否编译测试（默认关闭）