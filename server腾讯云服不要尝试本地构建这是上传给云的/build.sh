#!/bin/bash
# Linux/macOS 编译脚本

echo "开始编译WebSocket服务器..."

# 检查依赖
echo "检查依赖..."
if ! command -v cmake &> /dev/null; then
    echo "错误: 未找到cmake，请先安装cmake"
    exit 1
fi

# 检查Qt
if ! pkg-config --exists Qt6Core; then
    if ! pkg-config --exists Qt5Core; then
        echo "错误: 未找到Qt开发库，请安装Qt6或Qt5开发包"
        echo "Ubuntu/Debian: sudo apt install qt6-base-dev qt6-websockets-dev"
        echo "CentOS/RHEL: sudo yum install qt6-qtbase-devel qt6-qtwebsockets-devel"
        exit 1
    else
        echo "找到Qt5"
    fi
else
    echo "找到Qt6"
fi

# 创建构建目录
if [ -d "build" ]; then
    echo "清理旧的构建目录..."
    rm -rf build
fi

mkdir build
cd build

# 配置和编译
echo "配置项目..."
cmake ..
if [ $? -ne 0 ]; then
    echo "错误: cmake配置失败"
    exit 1
fi

echo "开始编译..."
make -j$(nproc)
if [ $? -ne 0 ]; then
    echo "错误: 编译失败"
    exit 1
fi

echo "编译成功！"
echo "可执行文件位置: $(pwd)/bin/WebSocketServer"
echo ""
echo "使用方法:"
echo "  ./bin/WebSocketServer --port 8765"
echo "  ./bin/WebSocketServer --help"
echo ""
echo "要安装到系统目录，请运行:"
echo "  sudo make install"