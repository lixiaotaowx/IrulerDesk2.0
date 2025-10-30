#!/bin/bash
# WebSocket服务器systemd服务卸载脚本 (支持登录系统)

SERVICE_NAME="websocket-server"
SERVICE_USER="www-data"
INSTALL_DIR="/opt/websocket_server_standalone"
USER_DATA_DIR="/home/$SERVICE_USER/.local/share/WebSocket Screen Stream Server"

echo "========================================="
echo "    WebSocket服务器卸载脚本 v2.0"
echo "    支持登录系统功能"
echo "========================================="

# 检查root权限
if [[ $EUID -ne 0 ]]; then
   echo "错误: 需要root权限运行"
   echo "请使用: sudo $0"
   exit 1
fi

echo "卸载WebSocket服务器服务..."

# 停止并禁用服务
if systemctl is-active --quiet $SERVICE_NAME; then
    echo "停止服务..."
    systemctl stop $SERVICE_NAME
    echo "已停止服务"
else
    echo "服务未运行"
fi

if systemctl is-enabled --quiet $SERVICE_NAME; then
    echo "禁用服务..."
    systemctl disable $SERVICE_NAME
    echo "已禁用服务"
fi

# 删除服务文件
if [ -f "/etc/systemd/system/$SERVICE_NAME.service" ]; then
    echo "删除服务文件..."
    rm -f "/etc/systemd/system/$SERVICE_NAME.service"
    systemctl daemon-reload
    echo "已删除服务文件"
fi

# 询问是否删除安装目录
echo ""
echo "========================================="
echo "           清理选项"
echo "========================================="
read -p "是否删除安装目录 $INSTALL_DIR? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if [ -d "$INSTALL_DIR" ]; then
        rm -rf "$INSTALL_DIR"
        echo "✅ 已删除安装目录: $INSTALL_DIR"
    fi
else
    echo "保留安装目录: $INSTALL_DIR"
fi

# 询问是否删除用户数据目录
read -p "是否删除用户数据目录 $USER_DATA_DIR? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if [ -d "$USER_DATA_DIR" ]; then
        rm -rf "$USER_DATA_DIR"
        echo "✅ 已删除用户数据目录: $USER_DATA_DIR"
    fi
    
    # 如果.local目录为空，也删除它
    if [ -d "/home/$SERVICE_USER/.local" ] && [ -z "$(ls -A /home/$SERVICE_USER/.local)" ]; then
        rm -rf "/home/$SERVICE_USER/.local"
        echo "✅ 已删除空的.local目录"
    fi
else
    echo "保留用户数据目录: $USER_DATA_DIR"
    echo "注意: 用户登录数据和配置文件将被保留"
fi

# 询问是否删除用户
read -p "是否删除服务用户 $SERVICE_USER? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if id "$SERVICE_USER" &>/dev/null; then
        # 删除用户的home目录（如果存在）
        if [ -d "/home/$SERVICE_USER" ]; then
            rm -rf "/home/$SERVICE_USER"
            echo "✅ 已删除用户home目录"
        fi
        
        userdel "$SERVICE_USER"
        echo "✅ 已删除用户: $SERVICE_USER"
    fi
else
    echo "保留服务用户: $SERVICE_USER"
fi

echo ""
echo "========================================="
echo "           卸载完成！"
echo "========================================="
echo "已完成的操作:"
echo "  ✅ 停止并禁用systemd服务"
echo "  ✅ 删除服务配置文件"

if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "  ✅ 清理了选择的文件和用户"
else
    echo "  ℹ️  保留了安装文件和用户数据"
fi

echo ""
echo "如需重新安装，请运行:"
echo "  sudo ./install-service.sh"
echo ""
echo "如需查看剩余文件:"
echo "  ls -la $INSTALL_DIR"
echo "  ls -la $USER_DATA_DIR"
echo "========================================="