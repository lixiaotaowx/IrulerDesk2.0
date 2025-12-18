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
    src/NewUi/NewUiWindow.cpp             # 新UI窗口实现
    src/NewUi/NewUiWindow.h               # 新UI窗口声明
    src/common/ConsoleLogger.cpp          # 控制台日志重定向
    src/common/ConsoleLogger.h            # 控制台日志重定向声明
    src/common/CrashGuard.cpp             # 崩溃守护：未处理异常栈打印
    src/common/CrashGuard.h               # 崩溃守护声明
    src/video_components/VideoDisplayWidget.cpp # 视频显示控件实现：绘制视频帧与批注事件处理
    src/video_components/VideoDisplayWidget.h   # 视频显示控件声明：接口与状态
    src/VideoWindow.cpp                   # 独立视频窗口实现：承载显示控件、拦截原生事件
    src/VideoWindow.h                     # 独立视频窗口声明：公开接口与成员
    src/ui/TransparentImageList.cpp       # 透明贴图列表实现：管理/选择叠加贴图资源
    src/ui/ScreenAnnotationWidget.cpp     # 屏幕批注透明层实现
    src/ui/ScreenAnnotationWidget.h       # 屏幕批注透明层声明
    src/ui/AnnotationToolbar.cpp          # 批注工具条实现
    src/ui/AnnotationToolbar.h            # 批注工具条声明
    src/ui/TransparentImageList.h         # 透明贴图列表声明：信号槽与模型接口
    src/ui/BubbleTipWidget.cpp            # 气泡提示控件
    src/ui/BubbleTipWidget.h              # 气泡提示控件声明
    src/ui/AvatarSettingsWindow.cpp       # 头像设置窗口实现：用户头像资源的配置与预览
    src/ui/AvatarSettingsWindow.h         # 头像设置窗口声明：UI与交互接口
    src/ui/SystemSettingsWindow.cpp       # 系统设置窗口实现：应用参数与行为配置
    src/ui/SystemSettingsWindow.h         # 系统设置窗口声明：设置项与信号槽
    src/ui/ColorCircleButton.cpp          # 圆形颜色按钮：自绘抗锯齿纯圆显示
    src/ui/ColorCircleButton.h            # 圆形颜色按钮声明
    src/ui/RainbowToolButton.cpp          # 彩虹工具按钮
    src/ui/RainbowToolButton.h            # 彩虹工具按钮声明
    src/ui/FirstLaunchWizard.cpp          # 首次启动引导设置对话框
    src/ui/FirstLaunchWizard.h            # 首次启动引导设置对话框声明
    src/ui/StreamingIslandWidget.cpp      # 推流状态灵动岛悬浮窗
    src/ui/StreamingIslandWidget.h        # 推流状态灵动岛悬浮窗声明
    src/ui/SnippetOverlay.cpp             # 截图选择遮罩层
    src/ui/SnippetOverlay.h               # 截图选择遮罩层声明
)

# 屏幕捕获编码进程源文件
set(CAPTURE_SOURCES
    src/capture/main_capture.cpp          # 捕获进程入口：初始化捕获、编码与传输流程
    src/common/ConsoleLogger.cpp          # 控制台日志重定向
    src/common/ConsoleLogger.h            # 控制台日志重定向声明
    src/common/CrashGuard.cpp             # 崩溃守护：未处理异常栈打印
    src/common/CrashGuard.h               # 崩溃守护声明
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
    src/capture/PerformanceMonitor.cpp    # 性能监控实现：统计编码/网络/帧率指标
    src/capture/PerformanceMonitor.h      # 性能监控声明
    src/capture/AnnotationOverlay.cpp     # 批注叠加层实现：在采集端绘制/组合批注图层
    src/capture/AnnotationOverlay.h       # 批注叠加层声明
    src/capture/CursorOverlay.cpp         # 鼠标叠加层实现：独立绘制光标与昵称
    src/capture/CursorOverlay.h           # 鼠标叠加层声明
)

# 解码播放进程源文件
set(PLAYER_SOURCES
    src/player/main_player.cpp            # 播放器进程入口：初始化解码与渲染管线
    src/common/ConsoleLogger.cpp          # 控制台日志重定向
    src/common/ConsoleLogger.h            # 控制台日志重定向声明
    src/common/CrashGuard.cpp             # 崩溃守护：未处理异常栈打印
    src/common/CrashGuard.h               # 崩溃守护声明
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
    src/video_components/AudioPlayer.cpp        # 音频播放组件实现
    src/video_components/AudioPlayer.h          # 音频播放组件声明
    src/video_components/VideoDisplayWidget.cpp # 视频显示与批注控件实现（复用到播放器）
    src/video_components/VideoDisplayWidget.h   # 视频显示与批注控件声明
    src/player/VP9Decoder.cpp                   # VP9 软件解码器实现
    src/player/VP9Decoder.h                     # VP9 软件解码器声明
    src/player/DxvaVP9Decoder.cpp               # 基于 DXVA 的硬件加速 VP9 解码实现
    src/player/DxvaVP9Decoder.h                 # DXVA VP9 解码器声明
    src/player/WebSocketReceiver.cpp            # WebSocket 接收端实现：接收瓦片与批注事件
    src/player/WebSocketReceiver.h              # WebSocket 接收端声明
    src/ui/TransparentImageList.cpp             # 透明头像列表（用于动画目标识别）
    src/ui/TransparentImageList.h               # 透明头像列表声明
    src/ui/BubbleTipWidget.cpp                  # 气泡提示控件
    src/ui/BubbleTipWidget.h                    # 气泡提示控件声明
    src/ui/ScreenAnnotationWidget.cpp           # 屏幕批注透明层实现
    src/ui/ScreenAnnotationWidget.h             # 屏幕批注透明层声明
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
    OUTPUT_NAME irulerdesk
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
    Qt6::Svg
    Qt6::Multimedia
    Opus::opus
    unofficial::libvpx::libvpx
    yuv
)

# 一键禁用所有日志输出（qDebug/qInfo/qWarning），并提供总开关
option(DISABLE_ALL_LOGS "Disable all application logging output" OFF)
if(DISABLE_ALL_LOGS)
    message(STATUS "日志输出已禁用: 使用 QT_NO_DEBUG_OUTPUT/INFO/WARNING 并定义 DISABLE_ALL_LOGS")
    target_compile_definitions(ScreenStreamApp PRIVATE DISABLE_ALL_LOGS QT_NO_DEBUG_OUTPUT QT_NO_INFO_OUTPUT QT_NO_WARNING_OUTPUT)
    target_compile_definitions(CaptureProcess PRIVATE DISABLE_ALL_LOGS QT_NO_DEBUG_OUTPUT QT_NO_INFO_OUTPUT QT_NO_WARNING_OUTPUT)
    target_compile_definitions(PlayerProcess PRIVATE DISABLE_ALL_LOGS QT_NO_DEBUG_OUTPUT QT_NO_INFO_OUTPUT QT_NO_WARNING_OUTPUT)
endif()

# 复制音频文件到构建目录
add_custom_command(TARGET ScreenStreamApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ScreenStreamApp>/audio
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/ling2.wav"
    "$<TARGET_FILE_DIR:ScreenStreamApp>/audio/ling2.wav"
    COMMENT "Copying audio assets..."
)

# 复制 maps 目录到构建目录
add_custom_command(TARGET ScreenStreamApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/src/maps"
    "$<TARGET_FILE_DIR:ScreenStreamApp>/maps"
    COMMENT "Copying maps assets..."
)

# 拷贝更新日志文件
add_custom_command(TARGET ScreenStreamApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/src/UpdateLog.html"
    "$<TARGET_FILE_DIR:ScreenStreamApp>/UpdateLog.html"
)


# 添加Windows特定库
if(WIN32)
    target_link_libraries(CaptureProcess PRIVATE d3d11 dxgi dbghelp)
    target_link_libraries(ScreenStreamApp PRIVATE d3d11 dxva2 ole32 dbghelp)
    target_link_libraries(PlayerProcess PRIVATE d3d11 dxva2 ole32 dbghelp)
    message(STATUS "检测到Windows平台，正在链接d3d11、dxgi、dxva2、ole32、dbghelp")
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

# 在Windows/MSVC下，构建前确保旧的可执行未在运行，避免 LNK1104
if(MSVC AND WIN32)
    add_custom_command(TARGET CaptureProcess PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Ensuring CaptureProcess.exe is not running..."
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='SilentlyContinue'; $p=Get-Process -Name 'CaptureProcess' -ErrorAction SilentlyContinue; if($p){ $p | Stop-Process -Force }; exit 0"
    )
    add_custom_command(TARGET PlayerProcess PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Ensuring PlayerProcess.exe is not running..."
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='SilentlyContinue'; $p=Get-Process -Name 'PlayerProcess' -ErrorAction SilentlyContinue; if($p){ $p | Stop-Process -Force }; exit 0"
    )
    add_custom_command(TARGET ScreenStreamApp PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Ensuring ScreenStreamApp.exe is not running..."
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='SilentlyContinue'; $p=Get-Process -Name 'ScreenStreamApp' -ErrorAction SilentlyContinue; if($p){ $p | Stop-Process -Force }; exit 0"
    )
endif()

# 为 Release/RelWithDebInfo 生成 PDB 并拷贝到输出目录（仅 MSVC 有效）
if(MSVC)
    # 确保为每个目标在 Release/RelWithDebInfo 生成 PDB
    target_compile_options(ScreenStreamApp PRIVATE $<$<CONFIG:Release>:/Zi> $<$<CONFIG:RelWithDebInfo>:/Zi>)
    target_link_options(ScreenStreamApp    PRIVATE $<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:RelWithDebInfo>:/DEBUG>)
    target_compile_options(CaptureProcess  PRIVATE $<$<CONFIG:Release>:/Zi> $<$<CONFIG:RelWithDebInfo>:/Zi>)
    target_link_options(CaptureProcess     PRIVATE $<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:RelWithDebInfo>:/DEBUG>)
    target_compile_options(PlayerProcess   PRIVATE $<$<CONFIG:Release>:/Zi> $<$<CONFIG:RelWithDebInfo>:/Zi>)
    target_link_options(PlayerProcess      PRIVATE $<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:RelWithDebInfo>:/DEBUG>)

    if(WIN32)
        add_custom_command(TARGET ScreenStreamApp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Copying PDB for ScreenStreamApp..."
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_PDB_FILE:ScreenStreamApp>
                $<TARGET_FILE_DIR:ScreenStreamApp>/
        )
        add_custom_command(TARGET CaptureProcess POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Copying PDB for CaptureProcess..."
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_PDB_FILE:CaptureProcess>
                $<TARGET_FILE_DIR:CaptureProcess>/
        )
        add_custom_command(TARGET PlayerProcess POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Copying PDB for PlayerProcess..."
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_PDB_FILE:PlayerProcess>
                $<TARGET_FILE_DIR:PlayerProcess>/
        )
    endif()
endif()

# 自动部署Qt DLL
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${CMAKE_PREFIX_PATH}/bin"
        PATHS "C:/Qt/6.8.3/msvc2022_64/bin"
    )

    if(WINDEPLOYQT_EXECUTABLE)
        get_filename_component(QT_BIN_DIR "${WINDEPLOYQT_EXECUTABLE}" DIRECTORY)
        if(EXISTS "${QT_BIN_DIR}/d3dcompiler_47.dll")
            foreach(tgt IN ITEMS ScreenStreamApp CaptureProcess PlayerProcess)
                add_custom_command(TARGET ${tgt} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${QT_BIN_DIR}/d3dcompiler_47.dll"
                        "$<TARGET_FILE_DIR:${tgt}>/"
                    COMMENT "Manually copying d3dcompiler_47.dll to ensure RHI works"
                )
            endforeach()
        endif()

        add_custom_command(TARGET ScreenStreamApp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Deploying Qt dependencies for ScreenStreamApp..."
            COMMAND ${WINDEPLOYQT_EXECUTABLE}
                --verbose 2
                $<IF:$<CONFIG:Debug>,--debug,--release>
                --compiler-runtime
                --no-system-d3d-compiler
                $<TARGET_FILE:ScreenStreamApp>
            COMMENT "Deploying Qt libraries and plugins for ScreenStreamApp"
        )
        add_custom_command(TARGET CaptureProcess POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Deploying Qt dependencies for CaptureProcess..."
            COMMAND ${WINDEPLOYQT_EXECUTABLE}
                --verbose 2
                $<IF:$<CONFIG:Debug>,--debug,--release>
                --compiler-runtime
                --no-system-d3d-compiler
                $<TARGET_FILE:CaptureProcess>
            COMMENT "Deploying Qt libraries and plugins for CaptureProcess"
        )
        add_custom_command(TARGET PlayerProcess POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Deploying Qt dependencies for PlayerProcess..."
            COMMAND ${WINDEPLOYQT_EXECUTABLE}
                --verbose 2
                $<IF:$<CONFIG:Debug>,--debug,--release>
                --compiler-runtime
                --no-system-d3d-compiler
                $<TARGET_FILE:PlayerProcess>
            COMMENT "Deploying Qt libraries and plugins for PlayerProcess"
        )
        message(STATUS "Found windeployqt: ${WINDEPLOYQT_EXECUTABLE}")
    else()
        message(WARNING "windeployqt not found! Qt dependencies will not be deployed automatically.")
    endif()

    add_custom_command(TARGET ScreenStreamApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying avatar files..."
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/src/maps
            $<TARGET_FILE_DIR:ScreenStreamApp>/maps
        COMMENT "Copying avatar files to output directory"
    )
endif()

# 显式复制 MSVC 运行库到输出目录，避免目标机器缺少 VC++ 运行库
if(MSVC AND WIN32)
    set(_vc_redist_candidates
        "$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT"
        "$ENV{VCToolsRedistDir}/x64/Microsoft.VC142.CRT"
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT"
        "C:/Program Files/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/*/x64/Microsoft.VC142.CRT"
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT"
        "C:/Program Files/Microsoft Visual Studio/2019/Professional/VC/Redist/MSVC/*/x64/Microsoft.VC142.CRT"
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT"
        "C:/Program Files/Microsoft Visual Studio/2019/Enterprise/VC/Redist/MSVC/*/x64/Microsoft.VC142.CRT"
        "C:/Program Files (x86)/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT"
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/*/x64/Microsoft.VC142.CRT"
    )
    unset(VCREDIST_DIR)
    foreach(dir IN LISTS _vc_redist_candidates)
        if(dir MATCHES "\\*")
            file(GLOB _vc_dirs "${dir}")
            if(_vc_dirs)
                list(GET _vc_dirs 0 VCREDIST_DIR)
                break()
            endif()
        elseif(EXISTS "${dir}")
            set(VCREDIST_DIR "${dir}")
            break()
        endif()
    endforeach()

    if(VCREDIST_DIR AND (EXISTS "${VCREDIST_DIR}/vcruntime140_1.dll" OR EXISTS "${VCREDIST_DIR}/vcruntime140.dll"))
        set(_vcr_dlls
            vcruntime140_1.dll
            vcruntime140.dll
            msvcp140.dll
            concrt140.dll
        )
        foreach(tgt IN ITEMS ScreenStreamApp CaptureProcess PlayerProcess)
            foreach(dll IN LISTS _vcr_dlls)
                add_custom_command(TARGET ${tgt} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${VCREDIST_DIR}/${dll}"
                        "$<TARGET_FILE_DIR:${tgt}>/"
                )
            endforeach()
        endforeach()
        message(STATUS "VC++ runtime detected: ${VCREDIST_DIR}")
    else()
        message(WARNING "VC++ runtime folder not found; ensure Visual Studio VC Redist is installed or rely on windeployqt --compiler-runtime.")
    endif()
endif()

# 可选：添加测试子目录（仅在需要时启用）
# 通过选项控制是否编译测试（默认关闭）

# 安装与打包配置
include(GNUInstallDirs)

# 将已部署的可执行文件与其依赖（Qt DLL、插件、资源）整体安装到根目录
set(INSTALL_BUNDLE_DIR ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)
install(DIRECTORY ${INSTALL_BUNDLE_DIR}/ DESTINATION .
        PATTERN "*.pdb" EXCLUDE
        PATTERN "config" EXCLUDE
        PATTERN "config/*" EXCLUDE)

# 安装静态资源（地图/头像等），以确保无构建工序也可包含
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/maps DESTINATION .)
# cpack -G NSIS -C Release
# CPack 打包元数据（默认生成 ZIP 包，免外部打包器依赖）
include(InstallRequiredSystemLibraries)
set(CPACK_PACKAGE_NAME "IrulerDeskPro")
set(CPACK_PACKAGE_VENDOR "lunzi")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "屏幕采集、VP9 传输、Qt 前端一体化客户端")
set(CPACK_PACKAGE_VERSION "1.0.1")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-win64")
set(CPACK_SYSTEM_NAME "Windows")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "IrulerDeskPro")
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES")
set(CPACK_NSIS_ENABLE_UNINSTALL ON)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL OFF)
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "IrulerDeskPro")
set(CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS "ExecWait $\"taskkill /F /IM irulerdesk.exe /T$\"\nExecWait $\"taskkill /F /IM CaptureProcess.exe /T$\"\nExecWait $\"taskkill /F /IM PlayerProcess.exe /T$\"\nStrCpy $R0 $\"$\"\nReadRegStr $R0 HKLM $\"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\IrulerDeskPro$\" $\"UninstallString$\"\nStrCmp $R0 $\"$\" checkHKCU haveUninst\ncheckHKCU:\nReadRegStr $R0 HKCU $\"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\IrulerDeskPro$\" $\"UninstallString$\"\nStrCmp $R0 $\"$\" doneCheck haveUninst\nhaveUninst:\nMessageBox MB_ICONQUESTION|MB_YESNO $\"检测到已安装旧版本，是否先卸载后继续安装？$\" IDYES doUninstall IDNO doneCheck\ndoUninstall:\nExecWait $\"$R0$\"\ndoneCheck:\n")
set(CPACK_NSIS_DISPLAY_NAME "IrulerDeskPro")
set(CPACK_NSIS_CONTACT "support@iruler.local")
set(CPACK_NSIS_HELP_LINK "http://www.iruler.cn/")
set(CPACK_NSIS_URL_INFO_ABOUT "http://www.iruler.cn/")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/installer/LICENSE_en.txt")
set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/src/maps/logo/iruler.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/src/maps/logo/iruler.ico")
set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
set(CPACK_NSIS_MENU_LINKS "irulerdesk.exe" "IrulerDeskPro")
set(CPACK_NSIS_MUI_FINISHPAGE_RUN "irulerdesk.exe")
set(CPACK_GENERATOR "NSIS")
set(CPACK_NSIS_EXECUTABLE "C:/Program Files (x86)/NSIS/makensis.exe")

# 手动管理快捷方式创建（确保使用 All Users 上下文，解决桌面和开机自启无效问题）
# SetShellVarContext all: 切换到"所有用户"上下文 (ProgramData/Public Desktop)
# 注意：使用 \\" 转义双引号，确保 NSIS 脚本格式正确
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
    SetShellVarContext all
    CreateShortCut \\\"$DESKTOP\\\\IrulerDeskPro.lnk\\\" \\\"$INSTDIR\\\\irulerdesk.exe\\\"
    CreateShortCut \\\"$SMSTARTUP\\\\IrulerDeskPro.lnk\\\" \\\"$INSTDIR\\\\irulerdesk.exe\\\"
")

set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
    SetShellVarContext all
    Delete \\\"$DESKTOP\\\\IrulerDeskPro.lnk\\\"
    Delete \\\"$SMSTARTUP\\\\IrulerDeskPro.lnk\\\"
")

include(CPack)

# 嵌入应用图标（使用 src/maps/logo/iruler.ico）
set(IRULER_ICON_PATH "${CMAKE_SOURCE_DIR}/src/maps/logo/iruler.ico")
if(EXISTS "${IRULER_ICON_PATH}")
    set(APP_RC "${CMAKE_BINARY_DIR}/app_icon.rc")
    file(WRITE "${APP_RC}" "IDI_ICON1 ICON \"${IRULER_ICON_PATH}\"\n")
    target_sources(ScreenStreamApp PRIVATE "${APP_RC}")
endif()

# 自定义目标：构建完成后生成带时间戳的ZIP包（仅限Release模式）
# 包名格式：ReleaseMMddHHmm.zip，位置在 build/bin/ 目录下
if(MSVC)
    add_custom_target(ZipRelease ALL
        DEPENDS ScreenStreamApp CaptureProcess PlayerProcess
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command 
        "if ('$<CONFIG>' -eq 'Release') { $t = Get-Date -Format 'MMddHHmm'; $src = '${CMAKE_BINARY_DIR}/bin/Release'; $dst = '${CMAKE_BINARY_DIR}/bin/Release' + $t + '.zip'; Write-Host 'Zipping Release to ' $dst; if (Test-Path $dst) { Remove-Item $dst -Force }; Get-ChildItem -Path $src -Exclude '*.pdb' | Where-Object { $_.Name -ne 'config' } | Compress-Archive -DestinationPath $dst -Update }"
        COMMENT "Generating Release timestamped ZIP..."
    )
endif()
