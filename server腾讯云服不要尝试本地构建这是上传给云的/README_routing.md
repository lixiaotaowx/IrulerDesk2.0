# WebSocket路由服务器

支持URL路径路由的WebSocket服务器，用于屏幕流传输的多设备管理。

## 功能特性

### 🎯 **核心功能**
- **路径路由**: 支持 `/publish/{device_id}` 和 `/subscribe/{device_id}` 格式
- **房间管理**: 自动创建和管理设备房间
- **一对多广播**: 一个推流端可以对应多个拉流端
- **多设备支持**: 支持多个设备同时推流
- **自动清理**: 自动清理空房间和断开的连接

### 📊 **监控统计**
- 实时连接统计
- 房间状态监控
- 流量统计
- 消息转发计数

## URL格式

### 推流端 (Publisher)
```
ws://your-server:8765/publish/{device_id}
```

### 拉流端 (Subscriber)  
```
ws://your-server:8765/subscribe/{device_id}
```

### 示例
```
设备25561推流: ws://123.207.222.92:8765/publish/25561
观看设备25561: ws://123.207.222.92:8765/subscribe/25561

设备67890推流: ws://123.207.222.92:8765/publish/67890
观看设备67890: ws://123.207.222.92:8765/subscribe/67890
```

## 编译和部署

### Linux环境

1. **安装依赖**
```bash
# Ubuntu/Debian
sudo apt-get install qt6-base-dev qt6-websockets-dev cmake build-essential

# CentOS/RHEL
sudo yum install qt6-qtbase-devel qt6-qtwebsockets-devel cmake gcc-c++
```

2. **编译**
```bash
chmod +x build_routing.sh
./build_routing.sh
```

3. **运行**
```bash
# 前台运行
./build_routing/bin/websocket_server_routing --port 8765

# 后台运行
./build_routing/bin/websocket_server_routing --port 8765 --daemon

# 自定义端口
./build_routing/bin/websocket_server_routing --port 9999
```

### 服务化部署

1. **创建systemd服务文件**
```bash
sudo nano /etc/systemd/system/websocket-routing.service
```

2. **服务文件内容**
```ini
[Unit]
Description=WebSocket Routing Server for Screen Stream
After=network.target

[Service]
Type=simple
User=your-username
WorkingDirectory=/path/to/your/server
ExecStart=/path/to/websocket_server_routing --port 8765 --daemon
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

3. **启动服务**
```bash
sudo systemctl daemon-reload
sudo systemctl enable websocket-routing
sudo systemctl start websocket-routing
sudo systemctl status websocket-routing
```

## 日志监控

服务器会输出详细的日志信息：

```
2025-01-XX 10:30:15 WebSocket路由服务器启动成功，监听端口: 8765
2025-01-XX 10:30:20 新客户端连接: 192.168.1.100:54321 路径: /publish/25561
2025-01-XX 10:30:20 创建新房间: 25561
2025-01-XX 10:30:20 房间25561设置推流端
2025-01-XX 10:30:25 新客户端连接: 192.168.1.101:54322 路径: /subscribe/25561
2025-01-XX 10:30:25 房间25561新增订阅者，当前订阅者数量: 1
```

## 性能优化

### 系统参数调优
```bash
# 增加文件描述符限制
echo "* soft nofile 65536" >> /etc/security/limits.conf
echo "* hard nofile 65536" >> /etc/security/limits.conf

# 网络参数优化
echo "net.core.rmem_max = 16777216" >> /etc/sysctl.conf
echo "net.core.wmem_max = 16777216" >> /etc/sysctl.conf
sysctl -p
```

### 防火墙配置
```bash
# 开放端口
sudo ufw allow 8765/tcp
# 或者使用iptables
sudo iptables -A INPUT -p tcp --dport 8765 -j ACCEPT
```

## 故障排除

### 常见问题

1. **端口被占用**
```bash
# 检查端口占用
netstat -tlnp | grep 8765
# 或使用ss
ss -tlnp | grep 8765
```

2. **权限问题**
```bash
# 确保用户有执行权限
chmod +x websocket_server_routing
```

3. **Qt库找不到**
```bash
# 设置Qt路径
export LD_LIBRARY_PATH=/usr/local/Qt6/lib:$LD_LIBRARY_PATH
export PATH=/usr/local/Qt6/bin:$PATH
```

## 与原版本的区别

| 功能 | 原版本 | 路由版本 |
|------|--------|----------|
| URL格式 | `ws://server:8765` | `ws://server:8765/publish/{id}` |
| 设备管理 | 不支持 | 支持多设备 |
| 房间概念 | 无 | 按设备ID分房间 |
| 一对多 | 简单转发 | 房间内广播 |
| 监控 | 基础统计 | 详细房间统计 |

## 升级建议

1. **渐进式升级**: 先在测试环境验证
2. **保留原版本**: 作为备用方案
3. **监控日志**: 观察新版本运行状态
4. **性能测试**: 验证多设备场景下的性能