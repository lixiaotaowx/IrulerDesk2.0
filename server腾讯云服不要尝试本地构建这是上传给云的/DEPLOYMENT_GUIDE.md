# WebSocket登录系统部署指南

## 📋 概述

本指南说明如何使用增强后的脚本部署带有登录功能的WebSocket服务器。所有功能都集成在现有的三个脚本中：

- **`install-service.sh`** - 首次安装服务
- **`update-service.sh`** - 更新服务（支持自动编译）
- **`uninstall-service.sh`** - 卸载服务

## 🚀 部署步骤

### 1. 首次安装

```bash
# 上传文件到服务器
scp websocket_server_standalone.cpp user@server:/tmp/
scp CMakeLists.txt user@server:/tmp/
scp build.sh user@server:/tmp/
scp websocket-server.service user@server:/tmp/
scp install-service.sh user@server:/tmp/

# 在服务器上安装
ssh user@server
cd /tmp
sudo ./install-service.sh
```

**install-service.sh v2.0 新功能：**
- ✅ 自动创建安装目录 `/opt/websocket_server_standalone`
- ✅ 自动复制所有必要文件
- ✅ 自动编译项目
- ✅ 创建用户数据目录（支持登录系统）
- ✅ 安装并启动systemd服务
- ✅ 详细的安装报告和管理命令说明

### 2. 更新服务

```bash
# 上传新的源文件到服务器
scp websocket_server_standalone.cpp user@server:/tmp/

# 在服务器上更新
ssh user@server
cd /tmp
sudo ./update-service.sh
```

**update-service.sh v2.0 新功能：**
- ✅ 自动备份现有文件（带时间戳）
- ✅ 智能检测：有新源文件就编译，没有就使用现有文件
- ✅ 自动编译新版本
- ✅ 编译失败时自动恢复备份
- ✅ 创建用户数据目录（支持登录系统）
- ✅ 详细的更新报告和功能说明

### 3. 卸载服务

```bash
ssh user@server
cd /opt/websocket_server_standalone
sudo ./uninstall-service.sh
```

**uninstall-service.sh v2.0 新功能：**
- ✅ 交互式选择清理选项
- ✅ 可选择保留或删除安装目录
- ✅ 可选择保留或删除用户数据目录
- ✅ 可选择保留或删除服务用户
- ✅ 详细的卸载报告

## 📁 文件结构

### 服务器端文件结构
```
/opt/websocket_server_standalone/          # 安装目录
├── websocket_server_standalone.cpp        # 源文件
├── CMakeLists.txt                         # 编译配置
├── build.sh                              # 编译脚本
├── websocket-server.service              # 服务配置
├── install-service.sh                    # 安装脚本
├── update-service.sh                     # 更新脚本
├── uninstall-service.sh                  # 卸载脚本
└── build/                                # 编译输出
    └── bin/
        └── WebSocketServer               # 可执行文件

/home/www-data/.local/share/WebSocket Screen Stream Server/
└── users.json                           # 用户数据文件
```

## 🎯 使用场景

### 场景1：首次部署
```bash
# 准备文件
cd /path/to/server/directory
# 确保有以下文件：
# - websocket_server_standalone.cpp (带登录功能)
# - CMakeLists.txt
# - build.sh
# - websocket-server.service
# - install-service.sh

# 执行安装
sudo ./install-service.sh
```

### 场景2：更新登录功能
```bash
# 只需要新的源文件
cd /path/to/server/directory
# 放置新的 websocket_server_standalone.cpp

# 执行更新（自动编译）
sudo ./update-service.sh
```

### 场景3：重启服务
```bash
# 不需要重新编译，只是重启
sudo systemctl restart websocket-server
```

### 场景4：查看状态
```bash
# 查看服务状态
sudo systemctl status websocket-server

# 查看实时日志
sudo journalctl -u websocket-server -f

# 查看登录相关日志
sudo journalctl -u websocket-server | grep -i "登录\|用户\|login"
```

## 🔧 管理命令

### 服务管理
```bash
sudo systemctl start websocket-server      # 启动
sudo systemctl stop websocket-server       # 停止
sudo systemctl restart websocket-server    # 重启
sudo systemctl status websocket-server     # 状态
sudo systemctl enable websocket-server     # 开机启动
sudo systemctl disable websocket-server    # 禁用开机启动
```

### 日志查看
```bash
sudo journalctl -u websocket-server -f     # 实时日志
sudo journalctl -u websocket-server -n 50  # 最近50行
sudo journalctl -u websocket-server --since "1 hour ago"  # 最近1小时
```

### 手动测试
```bash
# 测试端口连接
telnet localhost 8765

# 手动启动（调试用）
cd /opt/websocket_server_standalone
sudo -u www-data ./build/bin/WebSocketServer --port 8765
```

## 🎉 功能特性

### 登录系统功能
- **自动登录**: 客户端启动时自动登录
- **实时用户列表**: 服务器实时广播在线用户
- **JSON存储**: 简单的用户数据存储
- **同端口**: 复用8765端口，无需额外配置

### 推流拉流功能
- **完全兼容**: 不影响现有推流拉流功能
- **高性能**: 支持多客户端同时推流拉流
- **稳定性**: systemd服务管理，自动重启

## 🔍 故障排除

### 常见问题

1. **编译失败**
   ```bash
   # 检查Qt依赖
   pkg-config --exists Qt6Core Qt6WebSockets
   
   # 安装依赖
   sudo apt install qt6-base-dev qt6-websockets-dev
   ```

2. **服务启动失败**
   ```bash
   # 查看详细错误
   sudo journalctl -u websocket-server -n 20
   
   # 检查端口占用
   sudo netstat -tlnp | grep 8765
   ```

3. **权限问题**
   ```bash
   # 修复权限
   sudo chown -R www-data:www-data /opt/websocket_server_standalone
   sudo chown -R www-data:www-data /home/www-data/.local
   ```

### 恢复备份
```bash
# 查看备份目录
ls -la /opt/websocket_backup_*

# 恢复备份（如果需要）
BACKUP_DIR="/opt/websocket_backup_20250123_143022"  # 替换为实际备份目录
sudo cp "$BACKUP_DIR/websocket_server_standalone.cpp" /opt/websocket_server_standalone/
sudo ./update-service.sh
```

## 📝 总结

使用增强后的脚本，你可以：

1. **一键安装**: `sudo ./install-service.sh` - 完整安装包括编译
2. **一键更新**: `sudo ./update-service.sh` - 智能更新包括自动编译和备份
3. **一键卸载**: `sudo ./uninstall-service.sh` - 交互式卸载，可选择保留数据

所有脚本都支持登录系统功能，无需额外配置。客户端连接后将自动显示在线用户列表。