#!/bin/bash

# WebSocket路由服务器编译脚本
# 支持URL路径路由的版本

echo "开始编译WebSocket路由服务器..."

# 创建构建目录
mkdir -p build_routing
cd build_routing

# 配置CMake
echo "配置CMake..."
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/local/Qt6 ../CMakeLists_routing.txt

# 编译
echo "开始编译..."
make -j$(nproc)

if [ $? -eq 0 ]; then
    echo "编译成功！"
    echo "可执行文件位置: $(pwd)/bin/websocket_server_routing"
    
    # 显示文件信息
    ls -la bin/websocket_server_routing
    
    echo ""
    echo "使用方法:"
    echo "  ./bin/websocket_server_routing --port 8765"
    echo "  ./bin/websocket_server_routing --port 8765 --daemon"
    echo ""
    echo "支持的URL格式:"
    echo "  推流端: ws://your-server:8765/publish/{device_id}"
    echo "  拉流端: ws://your-server:8765/subscribe/{device_id}"
else
    echo "编译失败！"
    exit 1
fi