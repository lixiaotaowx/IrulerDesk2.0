# WebSocket服务器独立部署包

这个目录包含了独立的WebSocket服务器，可以部署到任何支持Qt的服务器上，用于屏幕流传输的中继服务。

## 文件说明

- `websocket_server_standalone.cpp` - 独立的WebSocket服务器源代码
- `CMakeLists.txt` - CMake构建配置文件
- `build.sh` - Linux/macOS编译脚本
- `build.bat` - Windows编译脚本
- `server_deployment_guide.md` - 详细的部署指南
- `README.md` - 本说明文件

## 快速开始

### Windows平台

1. 确保已安装Qt6和CMake
2. 双击运行 `build.bat`
3. 编译完成后，可执行文件位于 `build/bin/Release/WebSocketServer.exe`

### Linux/macOS平台

1. 安装依赖：
   ```bash
   # Ubuntu/Debian
   sudo apt install build-essential cmake qt6-base-dev qt6-websockets-dev
   
   # CentOS/RHEL
   sudo yum groupinstall "Development Tools"
   sudo yum install cmake qt6-qtbase-devel qt6-qtwebsockets-devel
   
   # macOS (使用Homebrew)
   brew install cmake qt6
   ```

2. 运行编译脚本：
   ```bash
   chmod +x build.sh
   ./build.sh
   ```

3. 编译完成后，可执行文件位于 `build/bin/WebSocketServer`

## 使用方法

### 基本用法
```bash
# 使用默认端口8765启动
./WebSocketServer

# 指定端口启动
./WebSocketServer --port 9000

# 查看帮助
./WebSocketServer --help
```

### 服务器部署

1. **上传文件到服务器**
   ```bash
   scp -r server/ root@your-server:/opt/websocket-server/
   ```

2. **在服务器上编译**
   ```bash
   cd /opt/websocket-server
   ./build.sh
   ```

3. **启动服务**
   ```bash
   ./build/bin/WebSocketServer --port 8765
   ```

4. **配置为系统服务**（可选）
   ```bash
   # 创建systemd服务文件
   sudo nano /etc/systemd/system/websocket-server.service
   ```
   
   服务文件内容：
   ```ini
   [Unit]
   Description=WebSocket Screen Stream Server
   After=network.target
   
   [Service]
   Type=simple
   User=www-data
   WorkingDirectory=/opt/websocket-server
   ExecStart=/opt/websocket-server/build/bin/WebSocketServer --port 8765
   Restart=always
   RestartSec=10
   
   [Install]
   WantedBy=multi-user.target
   ```
   
   启动服务：
   ```bash
   sudo systemctl daemon-reload
   sudo systemctl enable websocket-server
   sudo systemctl start websocket-server
   ```

## 客户端配置

部署服务器后，需要修改客户端代码中的连接地址：

1. 修改 `src/capture/WebSocketSender.cpp`
2. 修改 `src/player/WebSocketReceiver.cpp`

将连接地址从 `localhost:8765` 改为：
- 使用域名：`ws://your-domain.com:8765`
- 使用IP：`ws://123.456.789.123:8765`

## 防火墙配置

确保服务器防火墙开放相应端口：

```bash
# Ubuntu/Debian (ufw)
sudo ufw allow 8765

# CentOS/RHEL (firewalld)
sudo firewall-cmd --permanent --add-port=8765/tcp
sudo firewall-cmd --reload
```

## 监控和日志

服务器会输出详细的连接和统计日志：
- 客户端连接/断开信息
- 消息转发统计
- 每30秒输出一次服务器统计信息

查看服务日志：
```bash
# 如果使用systemd
sudo journalctl -u websocket-server -f

# 直接运行时的日志会输出到控制台
```

## 性能建议

- **服务器配置**：推荐2核4GB内存起步
- **带宽要求**：每个用户约需5-10Mbps带宽
- **并发连接**：单服务器可支持数百个并发连接
- **负载均衡**：高并发场景建议使用多服务器+负载均衡

## 故障排除

1. **编译失败**
   - 检查Qt版本是否支持（Qt5.12+或Qt6.0+）
   - 确认已安装WebSockets模块
   - 检查CMake版本（3.16+）

2. **服务器启动失败**
   - 检查端口是否被占用：`netstat -an | grep 8765`
   - 确认防火墙设置
   - 查看错误日志

3. **客户端连接失败**
   - 确认服务器地址和端口正确
   - 检查网络连通性：`telnet server-ip 8765`
   - 确认防火墙和安全组设置

更多详细信息请参考 `server_deployment_guide.md`。