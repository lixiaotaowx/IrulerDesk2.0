# WebSocket服务器部署指南

## 1. 域名 vs IP 选择

**推荐使用域名**，原因如下：
- 更稳定：IP可能变化，域名不会
- 支持HTTPS/WSS：域名可以申请SSL证书
- 更专业：便于记忆和管理
- 负载均衡：可以配置多个服务器

## 2. 服务器部署方案

### 方案A：Linux服务器部署（推荐）

#### 2.1 环境准备
```bash
# Ubuntu/Debian
sudo apt update
sudo apt install build-essential cmake git

# 安装Qt6
sudo apt install qt6-base-dev qt6-websockets-dev


#### 2.2 创建独立的服务器项目
创建 `websocket_server_standalone` 目录：
#### 2.2.1 切换到ubuntu用户，给权限
sudo chown -R ubuntu:ubuntu /opt/websocket_server_standalone


#### 2.3 编译和部署

**首次安装：**
```bash
# 编译
./build.sh

# 安装服务
sudo ./install-service.sh
```

**更新服务：**
```bash
# 重新编译
./build.sh

# 更新服务（自动停止、更新、重启）
sudo ./update-service.sh
```

**手动管理：**
```bash
# 查看服务状态
sudo systemctl status websocket-server

# 查看日志
sudo journalctl -u websocket-server -f

# 重启服务
sudo systemctl restart websocket-server

# 卸载服务
sudo ./uninstall-service.sh
```

**常见问题解决：**

如果遇到 `Failed to set up mount namespacing: /opt/websocket-server/logs: No such file or directory` 错误：
1. 使用更新脚本：`sudo ./update-service.sh`
2. 或手动创建目录：`sudo mkdir -p /opt/websocket-server/logs`


## 3. 客户端配置修改

### 3.1 修改WebSocketSender.cpp
```cpp
// 在startServer方法中添加域名支持
const QString SERVER_URL = "ws://your-domain.com:8765"; // 使用你的域名
// 或者
const QString SERVER_URL = "ws://123.456.789.123:8765"; // 使用IP
```

### 3.2 修改WebSocketReceiver.cpp
```cpp
// 同样修改连接地址
m_webSocket->open(QUrl("ws://your-domain.com:8765"));
```

## 4. 防火墙和安全配置

### 4.1 开放端口
```bash
# Ubuntu/Debian (ufw)
sudo ufw allow 8765


```

### 4.2 腾讯云安全组
- 登录腾讯云控制台
- 进入云服务器 → 安全组
- 添加入站规则：协议TCP，端口8765，来源0.0.0.0/0

## 5. 域名配置

### 5.1 DNS解析
- 在域名管理面板添加A记录
- 主机记录：ws（或其他子域名）
- 记录值：服务器公网IP
- TTL：600

### 5.2 SSL证书（可选，用于WSS）
```bash
# 使用Let's Encrypt
sudo apt install certbot
sudo certbot certonly --standalone -d ws.your-domain.com
```

## 6. 性能优化建议

### 6.1 服务器配置
- CPU：2核心以上
- 内存：4GB以上
- 带宽：根据并发用户数计算（每用户约5-10Mbps）

### 6.2 系统优化
```bash
# 增加文件描述符限制
echo "* soft nofile 65536" >> /etc/security/limits.conf
echo "* hard nofile 65536" >> /etc/security/limits.conf

# 优化网络参数
echo "net.core.rmem_max = 16777216" >> /etc/sysctl.conf
echo "net.core.wmem_max = 16777216" >> /etc/sysctl.conf
sudo sysctl -p
```

## 7. 监控和日志

### 7.1 日志配置
```bash
# 查看服务日志
sudo journalctl -u websocket-server -f

# 设置日志轮转
sudo nano /etc/logrotate.d/websocket-server
```

### 7.2 监控脚本
```bash
#!/bin/bash
# check_server.sh
if ! pgrep -f "WebSocketServer" > /dev/null; then
    echo "服务器已停止，正在重启..."
    sudo systemctl restart websocket-server
fi
```

## 8. 测试验证

```bash
# 测试WebSocket连接
wscat -c ws://your-domain.com:8765

# 或使用curl测试HTTP升级
curl -i -N -H "Connection: Upgrade" \
     -H "Upgrade: websocket" \
     -H "Sec-WebSocket-Version: 13" \
     -H "Sec-WebSocket-Key: test" \
     http://your-domain.com:8765/
```

推荐使用**域名 + Docker部署**的方案，最简单且易于维护。