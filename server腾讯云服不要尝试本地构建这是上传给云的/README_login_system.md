# WebSocket登录系统说明

## 系统概述

本登录系统是在现有WebSocket推流拉流服务器基础上添加的用户管理功能，实现了：
- 客户端自动登录
- 实时在线用户列表
- 简单的JSON数据存储
- 与推流拉流功能完全兼容

## 技术架构

### 服务器端 (websocket_server_standalone.cpp)
- **消息类型区分**: 通过JSON消息的`type`字段区分登录消息和推流数据
- **用户管理**: 使用`QMap<QString, UserInfo>`存储在线用户信息
- **数据持久化**: 用户数据保存到`~/.local/share/WebSocket Screen Stream Server/users.json`
- **实时广播**: 用户上线/下线时自动广播更新给所有客户端

### 客户端 (MainWindow.cpp)
- **自动连接**: 启动后3秒自动连接到WebSocket服务器
- **自动登录**: 连接成功后立即发送登录请求
- **用户列表**: 右侧QListWidget实时显示在线用户
- **状态管理**: 显示连接状态和登录进度

## 消息协议

### 1. 登录请求 (客户端 → 服务器)
```json
{
  "type": "login",
  "data": {
    "id": "12345",
    "name": "用户12345"
  }
}
```

### 2. 登录响应 (服务器 → 客户端)
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

### 3. 在线用户列表广播 (服务器 → 所有客户端)
```json
{
  "type": "online_users_update",
  "data": [
    {"id": "12345", "name": "用户12345"},
    {"id": "67890", "name": "用户67890"}
  ]
}
```

### 4. 获取在线用户列表 (客户端 → 服务器)
```json
{
  "type": "get_online_users"
}
```

### 5. 登出请求 (客户端 → 服务器)
```json
{
  "type": "logout"
}
```

## 数据结构

### UserInfo 结构体
```cpp
struct UserInfo {
    QString id;           // 用户ID
    QString name;         // 用户名
    QWebSocket* socket;   // WebSocket连接
    QDateTime loginTime;  // 登录时间
};
```

### JSON存储格式
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

## 关键功能实现

### 服务器端关键方法

1. **handleUserLogin()**: 处理用户登录请求
   - 验证用户ID和名称
   - 处理重复登录（踢掉旧连接）
   - 更新在线用户列表
   - 广播用户列表更新

2. **handleUserLogout()**: 处理用户登出
   - 从在线用户列表移除
   - 广播用户列表更新
   - 保存数据到文件

3. **broadcastOnlineUsersList()**: 广播在线用户列表
   - 构造JSON消息
   - 发送给所有连接的客户端

4. **saveUserData()**: 保存用户数据到JSON文件
   - 只保存在线用户信息
   - 自动创建数据目录

### 客户端关键方法

1. **initializeLoginSystem()**: 初始化登录系统
   - 创建WebSocket连接
   - 设置信号槽连接
   - 延迟连接服务器

2. **sendLoginRequest()**: 发送登录请求
   - 使用设备ID作为用户ID
   - 自动生成用户名

3. **updateUserList()**: 更新用户列表显示
   - 清空现有列表
   - 添加在线用户
   - 格式化显示文本

4. **onLoginWebSocketTextMessageReceived()**: 处理服务器消息
   - 解析JSON消息
   - 根据消息类型处理
   - 更新UI状态

## 兼容性设计

### 与推流拉流功能的兼容
- **消息类型区分**: 登录消息使用JSON文本格式，推流数据使用二进制格式
- **连接复用**: 同一个WebSocket连接既可以发送登录消息，也可以发送推流数据
- **端口复用**: 继续使用8765端口，无需额外端口
- **向后兼容**: 不影响现有的推流拉流客户端

### 错误处理
- **连接断开**: 自动重连机制
- **登录失败**: 显示错误信息并重试
- **服务器不可用**: 显示连接状态并定期重试
- **JSON解析错误**: 忽略无效消息，不影响其他功能

## 部署流程

### 1. 准备文件
- `websocket_server_standalone.cpp` (更新后的服务器代码)
- `quick_deploy.sh` (快速部署脚本)
- `deploy_login_system.md` (部署指南)

### 2. 服务器部署
```bash
# 上传文件到服务器
scp websocket_server_standalone.cpp user@server:/opt/websocket_server_standalone/
scp quick_deploy.sh user@server:/opt/websocket_server_standalone/

# 在服务器上执行部署
ssh user@server
cd /opt/websocket_server_standalone/
sudo ./quick_deploy.sh
```

### 3. 客户端更新
- 重新编译包含登录功能的客户端
- 分发给用户使用

## 测试验证

### 功能测试清单
- [ ] 服务器正常启动并监听8765端口
- [ ] 客户端能够自动连接和登录
- [ ] 用户列表实时更新
- [ ] 多个客户端同时在线
- [ ] 客户端断开连接后从列表移除
- [ ] 推流拉流功能不受影响
- [ ] 服务重启后功能正常
- [ ] 用户数据文件正确创建和更新

### 性能测试
- 支持并发用户数: 预期100+用户同时在线
- 消息延迟: 用户列表更新延迟<1秒
- 内存使用: 每个在线用户约占用几KB内存
- CPU使用: 登录系统CPU开销<5%

## 维护说明

### 日志监控
```bash
# 查看登录相关日志
sudo journalctl -u websocket-server | grep -E "(登录|用户|login)"

# 实时监控
sudo journalctl -u websocket-server -f
```

### 数据文件管理
- 位置: `~/.local/share/WebSocket Screen Stream Server/users.json`
- 大小: 通常<1MB
- 备份: 建议定期备份
- 清理: 服务重启时自动清理离线用户

### 故障排除
1. **用户列表不更新**: 检查WebSocket连接状态
2. **登录失败**: 检查服务器日志和网络连接
3. **数据文件权限错误**: 确保www-data用户有写权限
4. **编译失败**: 检查Qt依赖和CMake配置

## 扩展计划

### 可能的功能扩展
- 用户认证和权限管理
- 用户头像和状态信息
- 私聊功能
- 房间/群组管理
- 用户统计和分析
- Web管理界面

### 技术改进
- 数据库存储替代JSON文件
- Redis缓存提升性能
- 负载均衡支持
- SSL/TLS加密
- API接口标准化

## 总结

本登录系统实现了最简单有效的用户管理功能，满足了基本的在线用户显示需求。系统设计简洁，部署方便，与现有功能完全兼容，为后续功能扩展奠定了基础。