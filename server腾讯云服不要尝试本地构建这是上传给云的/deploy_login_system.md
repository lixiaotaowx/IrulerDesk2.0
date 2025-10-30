# 登录系统部署指南

## 概述
本指南说明如何在服务器上部署带有登录功能的WebSocket服务器。登录系统已集成到现有的推流拉流服务器中，无需额外端口。

## 功能特性
- **自动登录**: 客户端启动时自动使用设备ID和用户名登录
- **实时用户列表**: 服务器实时广播在线用户列表给所有客户端
- **JSON存储**: 用户数据存储在简单的JSON文件中
- **兼容性**: 不影响现有的推流拉流功能
- **同端口**: 复用8765端口，通过消息类型区分登录和推流数据

## 部署步骤

### 1. 上传更新的代码
将修改后的 `websocket_server_standalone.cpp` 文件上传到服务器的 `/opt/websocket_server_standalone/` 目录。

### 2. 编译新版本
```bash
cd /opt/websocket_server_standalone/
./build.sh
```

### 3. 更新服务
```bash
sudo ./update-service.sh
```

### 4. 验证部署
```bash
# 检查服务状态
sudo systemctl status websocket-server

# 查看服务日志
sudo journalctl -u websocket-server -f
```

## 登录系统API

### 客户端登录请求
```json
{
  "type": "login",
  "data": {
    "id": "12345",
    "name": "用户12345"
  }
}
```

### 服务器登录响应
```json
{
  "type": "login_response",
  "success": true,
  "message": "登录成功",
  "data": {
    "id": "12345",
    "name": "用户12345",
    "streamUrl": "ws://server:8765/stream/12345"
  }
}
```

### 在线用户列表广播
```json
{
  "type": "online_users_update",
  "data": [
    {"id": "12345", "name": "用户12345"},
    {"id": "67890", "name": "用户67890"}
  ]
}
```

## 数据存储

### 用户数据文件位置
- 文件路径: `~/.local/share/WebSocket Screen Stream Server/users.json`
- 格式: JSON
- 权限: 服务用户(www-data)可读写

### 数据结构示例
```json
{
  "users": {
    "12345": {
      "id": "12345",
      "name": "用户12345",
      "online": true,
      "lastLogin": "2025-01-23T10:30:00Z"
    }
  }
}
```

## 客户端集成

### 自动登录流程
1. 客户端启动后3秒自动连接到WebSocket服务器
2. 连接成功后立即发送登录请求
3. 使用设备配置文件中的随机ID作为用户ID
4. 用户名格式: "用户{ID}"

### 用户列表显示
- 右侧列表控件实时显示在线用户
- 格式: "用户名 (ID)"
- 自动更新，无需手动刷新

## 故障排除

### 常见问题

1. **编译失败**
   ```bash
   # 检查Qt依赖
   pkg-config --exists Qt6Core Qt6WebSockets
   
   # 安装缺失的依赖
   sudo apt install qt6-base-dev qt6-websockets-dev
   ```

2. **服务启动失败**
   ```bash
   # 检查端口占用
   sudo netstat -tlnp | grep 8765
   
   # 检查权限
   sudo chown -R www-data:www-data /opt/websocket_server_standalone
   ```

3. **用户数据文件权限问题**
   ```bash
   # 创建数据目录
   sudo mkdir -p /home/www-data/.local/share/WebSocket\ Screen\ Stream\ Server/
   sudo chown -R www-data:www-data /home/www-data/.local/
   ```

### 日志分析
```bash
# 查看登录相关日志
sudo journalctl -u websocket-server | grep -i login

# 查看用户管理日志
sudo journalctl -u websocket-server | grep -i "用户"

# 实时监控
sudo journalctl -u websocket-server -f
```

## 测试验证

### 1. 服务器端测试
```bash
# 检查WebSocket服务器是否正常监听
telnet localhost 8765

# 查看在线用户统计
sudo journalctl -u websocket-server | tail -20
```

### 2. 客户端测试
1. 启动客户端应用
2. 观察右侧用户列表是否显示"已连接服务器，正在登录..."
3. 确认登录成功后显示在线用户列表
4. 启动多个客户端验证用户列表实时更新

### 3. 功能验证
- ✅ 推流拉流功能正常工作
- ✅ 登录系统不影响视频传输
- ✅ 用户列表实时更新
- ✅ 客户端断开连接后自动从列表移除
- ✅ 重复登录处理正确

## 维护说明

### 定期维护
- 用户数据文件会自动管理，无需手动清理
- 服务重启时会清空在线用户列表
- 建议定期检查日志文件大小

### 备份建议
- 用户数据文件较小，可定期备份
- 主要备份服务配置和可执行文件

## 更新说明

本次更新内容:
- ✅ 服务器端添加用户登录管理功能
- ✅ 客户端添加自动登录和用户列表显示
- ✅ JSON数据存储实现
- ✅ 实时用户列表广播
- ✅ 兼容现有推流拉流功能
- ✅ 使用现有update-service.sh脚本部署

部署完成后，客户端将自动显示在线用户列表，实现简单的用户管理功能。