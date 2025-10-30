#!/bin/bash
# WebSocket服务器更新脚本 (支持登录系统)

SERVICE_NAME="websocket-server"
SERVICE_USER="www-data"
INSTALL_DIR="/opt/websocket_server_standalone"
BACKUP_DIR="/opt/websocket_backup_$(date +%Y%m%d_%H%M%S)"

echo "========================================="
echo "    WebSocket服务器更新脚本 v2.0"
echo "    支持登录系统功能"
echo "========================================="

# 检查root权限
if [[ $EUID -ne 0 ]]; then
   echo "错误: 需要root权限运行"
   echo "请使用: sudo $0"
   exit 1
fi

# 检查安装目录
if [ ! -d "$INSTALL_DIR" ]; then
    echo "错误: 安装目录不存在: $INSTALL_DIR"
    echo "请先运行 ./install-service.sh 安装基础服务"
    exit 1
fi

echo "更新WebSocket服务器..."
echo "安装目录: $INSTALL_DIR"
echo "备份目录: $BACKUP_DIR"

# 创建备份目录
echo "创建备份..."
mkdir -p "$BACKUP_DIR"

# 备份现有文件
if [ -f "$INSTALL_DIR/websocket_server_standalone.cpp" ]; then
    cp "$INSTALL_DIR/websocket_server_standalone.cpp" "$BACKUP_DIR/"
    echo "已备份源文件"
fi

if [ -f "$INSTALL_DIR/build/bin/WebSocketServer" ]; then
    cp "$INSTALL_DIR/build/bin/WebSocketServer" "$BACKUP_DIR/"
    echo "已备份可执行文件"
fi

# 检查是否有新的源文件需要更新
if [ -f "websocket_server_standalone.cpp" ]; then
    echo "发现新的源文件，准备更新..."
    
    # 停止服务
    if systemctl is-active --quiet $SERVICE_NAME; then
        echo "停止服务..."
        systemctl stop $SERVICE_NAME
    fi
    
    # 复制新的源文件
    echo "复制新的源文件..."
    cp websocket_server_standalone.cpp "$INSTALL_DIR/"
    chown $SERVICE_USER:$SERVICE_USER "$INSTALL_DIR/websocket_server_standalone.cpp"
    
    # 进入安装目录进行编译
    cd "$INSTALL_DIR"
    
    # 清理旧的构建文件
    echo "清理旧的构建文件..."
    if [ -d "build" ]; then
        rm -rf build
    fi
    
    # 编译新版本
    echo "编译新版本..."
    if [ ! -f "build.sh" ]; then
        echo "错误: 未找到 build.sh 脚本"
        exit 1
    fi
    
    chmod +x build.sh
    ./build.sh
    
    if [ $? -ne 0 ]; then
        echo "错误: 编译失败"
        echo "正在恢复备份..."
        if [ -f "$BACKUP_DIR/websocket_server_standalone.cpp" ]; then
            cp "$BACKUP_DIR/websocket_server_standalone.cpp" "$INSTALL_DIR/"
        fi
        if [ -f "$BACKUP_DIR/WebSocketServer" ]; then
            mkdir -p "$INSTALL_DIR/build/bin"
            cp "$BACKUP_DIR/WebSocketServer" "$INSTALL_DIR/build/bin/"
        fi
        exit 1
    fi
    
    echo "✅ 编译成功"
    
else
    echo "未发现新的源文件，使用现有可执行文件..."
    
    # 检查现有可执行文件
    if [ ! -f "$INSTALL_DIR/build/bin/WebSocketServer" ]; then
        echo "错误: 未找到可执行文件"
        echo "请确保以下任一条件："
        echo "1. 在当前目录放置新的 websocket_server_standalone.cpp 文件"
        echo "2. 或者先运行 ./build.sh 编译现有代码"
        exit 1
    fi
    
    # 停止服务
    if systemctl is-active --quiet $SERVICE_NAME; then
        echo "停止服务..."
        systemctl stop $SERVICE_NAME
    fi
fi

# 设置权限
echo "设置权限..."
chown -R $SERVICE_USER:$SERVICE_USER $INSTALL_DIR
chmod +x $INSTALL_DIR/build/bin/WebSocketServer

# 创建用户数据目录 (支持登录系统)
echo "创建用户数据目录..."
USER_DATA_DIR="/home/$SERVICE_USER/.local/share/WebSocket Screen Stream Server"
mkdir -p "$USER_DATA_DIR"
chown -R $SERVICE_USER:$SERVICE_USER "/home/$SERVICE_USER/.local"
echo "用户数据目录: $USER_DATA_DIR"

# 更新服务配置
echo "更新服务配置..."
if [ -f "websocket-server.service" ]; then
    cp websocket-server.service /etc/systemd/system/
    systemctl daemon-reload
    echo "服务配置已更新"
else
    echo "使用现有服务配置"
fi

# 启动服务
echo "启动服务..."
systemctl start $SERVICE_NAME

# 等待服务启动
sleep 3

# 检查状态
if systemctl is-active --quiet $SERVICE_NAME; then
    echo "✅ 服务更新成功！"
    echo ""
    echo "========================================="
    echo "           更新完成！"
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
    echo "查看日志:"
    echo "  sudo journalctl -u $SERVICE_NAME -f"
    echo ""
    echo "测试连接:"
    echo "  telnet localhost 8765"
    echo ""
    echo "备份文件位置: $BACKUP_DIR"
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
    echo ""
    echo "恢复备份:"
    echo "  如需恢复，备份文件位于: $BACKUP_DIR"
    exit 1
fi