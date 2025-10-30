@echo off
chcp 65001 >nul
echo 开始编译WebSocket服务器...

REM 检查cmake
cmake --version >nul 2>&1
if %errorlevel% neq 0 (
    echo 错误: 未找到cmake，请先安装cmake
    echo 下载地址: https://cmake.org/download/
    pause
    exit /b 1
)

REM 检查Qt
if not defined CMAKE_PREFIX_PATH (
    echo 警告: 未设置CMAKE_PREFIX_PATH环境变量
    echo 请设置Qt安装路径，例如: C:\Qt\6.8.3\msvc2022_64
    set /p qt_path=请输入Qt安装路径: 
    set CMAKE_PREFIX_PATH=%qt_path%
)

echo 使用Qt路径: %CMAKE_PREFIX_PATH%

REM 清理旧的构建目录
if exist build (
    echo 清理旧的构建目录...
    rmdir /s /q build
)

mkdir build
cd build

REM 配置项目
echo 配置项目...
cmake -G "Visual Studio 17 2022" -A x64 ..
if %errorlevel% neq 0 (
    echo 错误: cmake配置失败
    echo 请检查Qt路径是否正确
    pause
    exit /b 1
)

REM 编译
echo 开始编译...
cmake --build . --config Release
if %errorlevel% neq 0 (
    echo 错误: 编译失败
    pause
    exit /b 1
)

echo.
echo 编译成功！
echo 可执行文件位置: %cd%\bin\Release\WebSocketServer.exe
echo.
echo 使用方法:
echo   .\bin\Release\WebSocketServer.exe --port 8765
echo   .\bin\Release\WebSocketServer.exe --help
echo.
pause