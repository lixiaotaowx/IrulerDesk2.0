#!/bin/bash
# WebSocket服务器systemd服务安装脚本 (支持登录系统)

SERVICE_NAME="websocket-server"
SERVICE_USER="www-data"
INSTALL_DIR="/opt/websocket_server_standalone"

echo "========================================="
echo "    WebSocket服务器安装脚本 v2.0"
echo "    支持登录系统功能"
echo "========================================="

# 检查root权限
if [[ $EUID -ne 0 ]]; then
   echo "错误: 需要root权限运行"
   echo "请使用: sudo $0"
   exit 1
fi

echo "安装WebSocket服务器服务..."
echo "安装目录: $INSTALL_DIR"

# 创建安装目录
echo "创建安装目录..."
mkdir -p "$INSTALL_DIR"

# 复制文件到安装目录
echo "复制文件到安装目录..."
if [ -f "websocket_server_with_routing.cpp" ]; then
    cp websocket_server_with_routing.cpp "$INSTALL_DIR/"
    echo "已复制路由源文件"
else
    echo "错误: 未找到源文件 websocket_server_with_routing.cpp"
    echo "请确保在当前目录包含该文件后再运行安装脚本"
    exit 1
fi

if [ -f "CMakeLists.txt" ]; then
    cp CMakeLists.txt "$INSTALL_DIR/"
    echo "已复制CMakeLists.txt"
else
    echo "错误: 未找到 CMakeLists.txt"
    exit 1
fi

if [ -f "build.sh" ]; then
    cp build.sh "$INSTALL_DIR/"
    chmod +x "$INSTALL_DIR/build.sh"
    echo "已复制build.sh"
else
    echo "错误: 未找到 build.sh"
    exit 1
fi

if [ -f "websocket-server.service" ]; then
    cp websocket-server.service "$INSTALL_DIR/"
    echo "已复制服务配置文件"
else
    echo "错误: 未找到 websocket-server.service"
    exit 1
fi

# 进入安装目录
cd "$INSTALL_DIR"

# 强制确保CMakeLists.txt使用路由版本
sed -i -E "s@(add_executable\s*\(\s*WebSocketServer\s+)[^ )]+@\1websocket_server_with_routing.cpp@" CMakeLists.txt

# 编译项目
echo "编译项目..."
if [ -f "build.sh" ]; then
    ./build.sh
    if [ $? -ne 0 ]; then
        echo "错误: 编译失败"
        exit 1
    fi
    echo "✅ 编译成功"
else
    echo "错误: 未找到 build.sh 脚本"
    exit 1
fi

# 检查可执行文件
if [ ! -f "build/bin/WebSocketServer" ]; then
    echo "错误: 编译后未找到可执行文件"
    exit 1
fi

# 创建用户
if ! id "$SERVICE_USER" &>/dev/null; then
    useradd --system --no-create-home --shell /bin/false $SERVICE_USER
    echo "已创建用户: $SERVICE_USER"
else
    echo "用户已存在: $SERVICE_USER"
fi

# 设置目录权限
echo "设置权限..."
chown -R $SERVICE_USER:$SERVICE_USER $INSTALL_DIR
chmod +x $INSTALL_DIR/build/bin/WebSocketServer

# 创建用户数据目录 (支持登录系统)
echo "创建用户数据目录..."
USER_DATA_DIR="/home/$SERVICE_USER/.local/share/WebSocket Screen Stream Server"
mkdir -p "$USER_DATA_DIR"
chown -R $SERVICE_USER:$SERVICE_USER "/home/$SERVICE_USER/.local"
echo "用户数据目录: $USER_DATA_DIR"

# 安装服务
echo "安装systemd服务..."
if [ -f "websocket-server.service" ]; then
    cp websocket-server.service /etc/systemd/system/
    systemctl daemon-reload
    systemctl enable $SERVICE_NAME
    echo "服务已启用"
else
    echo "错误: 未找到服务配置文件"
    exit 1
fi

# 启动服务
echo "启动服务..."
systemctl start $SERVICE_NAME

# 等待服务启动
sleep 3

# 检查状态
if systemctl is-active --quiet $SERVICE_NAME; then
    echo "✅ 服务安装成功！"
    echo ""
    echo "========================================="
    echo "           安装完成！"
    echo "========================================="
    echo "服务状态:"
    systemctl status $SERVICE_NAME --no-pager -l
    echo ""
    echo "功能特性:"
    echo "  ✅ WebSocket推流拉流服务 (端口8765)"
    echo "  ✅ 用户登录系统"
    echo "  ✅ 实时在线用户列表"
    echo "  ✅ JSON数据存储"
    echo "  ✅ 客户端自动登录"
    echo ""
    echo "管理命令:"
    echo "  启动服务: sudo systemctl start $SERVICE_NAME"
    echo "  停止服务: sudo systemctl stop $SERVICE_NAME"
    echo "  重启服务: sudo systemctl restart $SERVICE_NAME"
    echo "  查看状态: sudo systemctl status $SERVICE_NAME"
    echo "  查看日志: sudo journalctl -u $SERVICE_NAME -f"
    echo ""
    echo "更新服务: sudo ./update-service.sh"
    echo "卸载服务: sudo ./uninstall-service.sh"
    echo ""
    echo "测试连接:"
    echo "  telnet localhost 8765"
    echo ""
    echo "安装目录: $INSTALL_DIR"
    echo "用户数据: $USER_DATA_DIR"
    echo "========================================="
else
    echo "❌ 服务启动失败"
    echo ""
    echo "查看错误日志:"
    journalctl -u $SERVICE_NAME --no-pager -n 20
    echo ""
    echo "尝试手动启动:"
    echo "  cd $INSTALL_DIR"
    echo "  sudo -u $SERVICE_USER ./build/bin/WebSocketServer --port 8765"
    exit 1
fi
