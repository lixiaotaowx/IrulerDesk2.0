# PlayerProcess API 文档

## 概述

PlayerProcess 是一个独立的视频播放进程，专门用于接收和解码VP9视频流。它通过WebSocket客户端连接到视频源，接收VP9编码的帧数据，解码后显示在窗口中。

## 启动方式

```bash
./PlayerProcess.exe [WebSocket_URL]
```

### 启动参数

- `WebSocket_URL` (可选): WebSocket服务器地址，默认为 `ws://localhost:8765`

### 启动示例

```bash
# 使用默认地址
./PlayerProcess.exe

# 指定服务器地址
./PlayerProcess.exe ws://192.168.1.100:8765
```

## 功能特性

- **VP9解码**: 使用libvpx库进行VP9视频解码，支持DXVA硬件加速
- **硬件解码**: 优先使用DXVA VP9硬件解码，失败时自动回退到软件解码
- **实时播放**: 低延迟的实时视频播放，优化的YUV到RGB转换
- **WebSocket客户端**: 连接到WebSocket视频服务器
- **自动重连**: 连接断开时自动尝试重连
- **全屏显示**: 支持全屏模式播放
- **性能统计**: 内置解码和渲染性能统计
- **多线程解码**: 4线程并行解码优化
- **统计信息**: 显示接收、解码、显示帧数统计

## 技术实现

### VP9解码
- **DXVA硬件解码**: 优先使用DirectX Video Acceleration进行硬件解码
  - 支持VP9硬件解码加速
  - 硬件初始化失败时自动回退到软件解码
- **软件解码**: libvpx (vpx_codec_vp9_dx)作为备用方案
- 解码配置:
  - 线程数: 4 (多线程并行解码)
  - 后处理: 禁用
  - 错误隐藏: 启用
  - 解码模式: 实时优化

### 视频渲染
- 渲染引擎: Qt QLabel
- 颜色空间转换: 优化的YUV420 -> RGB32转换
- 显示模式: 自适应缩放
- 性能统计: 解码和转换时间统计

## WebSocket 客户端配置

### 连接参数

- **协议**: WebSocket (ws://)
- **默认地址**: ws://localhost:8765
- **重连间隔**: 3秒
- **超时时间**: 5秒

### 数据处理

#### 接收的数据格式

- **数据类型**: 二进制数据 (QByteArray)
- **内容**: VP9编码的视频帧
- **处理频率**: 实时处理

#### 发送的数据

当前版本不向服务器发送数据。

## 集成示例

### 作为子进程启动

```cpp
#include <QProcess>

class VideoPlayer {
public:
    void startPlayer(const QString& serverUrl = "ws://localhost:8765") {
        m_playerProcess = new QProcess(this);
        
        // 设置启动参数
        QStringList arguments;
        if (!serverUrl.isEmpty()) {
            arguments << serverUrl;
        }
        
        // 启动播放器进程
        m_playerProcess->start("./PlayerProcess.exe", arguments);
        
        if (!m_playerProcess->waitForStarted(3000)) {
            qCritical() << "播放器进程启动失败";
            return;
        }
        
        qDebug() << "播放器进程启动成功，PID:" << m_playerProcess->processId();
    }
    
    void stopPlayer() {
        if (m_playerProcess) {
            m_playerProcess->terminate();
            if (!m_playerProcess->waitForFinished(3000)) {
                m_playerProcess->kill();
            }
            m_playerProcess->deleteLater();
            m_playerProcess = nullptr;
        }
    }
    
private:
    QProcess *m_playerProcess = nullptr;
};
```

### Python 集成

```python
import subprocess
import time

class VideoPlayerManager:
    def __init__(self):
        self.process = None
    
    def start_player(self, server_url="ws://localhost:8765"):
        """启动播放器进程"""
        try:
            args = ['./PlayerProcess.exe']
            if server_url:
                args.append(server_url)
            
            self.process = subprocess.Popen(args)
            print(f"播放器进程启动成功，PID: {self.process.pid}")
            return True
        except Exception as e:
            print(f"启动播放器失败: {e}")
            return False
    
    def stop_player(self):
        """停止播放器进程"""
        if self.process:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
            self.process = None
    
    def is_running(self):
        """检查播放器是否在运行"""
        return self.process is not None and self.process.poll() is None
```

## 用户界面

### 窗口组件

- **视频显示区域**: 显示解码后的视频帧
- **连接状态**: 显示WebSocket连接状态
- **统计信息**: 显示帧数统计
  - 接收帧数
  - 解码帧数  
  - 显示帧数
- **控制按钮**: 开始/停止接收

### 快捷键

当前版本不支持快捷键操作。

## 性能指标

### 优化后性能 (DXVA硬件解码)
- **解码延迟**: < 20ms
- **YUV转RGB**: < 8ms
- **渲染延迟**: < 16ms
- **总延迟**: < 45ms
- **内存使用**: ~40MB
- **CPU使用率**: 5-15% (硬件加速)

### 回退性能 (软件解码)
- **解码延迟**: 1-5ms (实测)
- **YUV转RGB**: 6-8ms (实测)
- **渲染延迟**: < 16ms
- **总延迟**: < 30ms
- **CPU使用率**: 15-25%

### 实际测试结果
- **接收帧率**: 与捕获端同步 (15.4 FPS)
- **解码成功率**: > 99%
- **显示流畅度**: 优秀
- **支持分辨率**: 最高4K (取决于硬件性能)

## 错误处理

### 常见错误

1. **连接失败**: 无法连接到WebSocket服务器
2. **解码失败**: VP9解码器初始化或解码失败
3. **显示错误**: 视频帧显示异常

### 错误恢复

- **自动重连**: 连接断开时自动尝试重连
- **解码器重置**: 解码失败时重新初始化解码器
- **错误日志**: 详细的错误信息输出

## 日志输出

### 日志级别

- **Debug**: 详细的调试信息
- **Info**: 一般信息
- **Warning**: 警告信息
- **Critical**: 严重错误

### 日志内容

- WebSocket连接状态变化
- VP9解码器状态
- 帧接收和处理统计
- 错误和异常信息

## 配置选项

当前版本的配置都是硬编码的，未来版本可能支持配置文件。

### 默认配置

```cpp
// WebSocket配置
static const QString DEFAULT_SERVER_URL = "ws://localhost:8765";
static const int RECONNECT_INTERVAL = 3000; // 3秒
static const int CONNECTION_TIMEOUT = 5000; // 5秒

// 显示配置
static const QSize MIN_WINDOW_SIZE = QSize(640, 480);
static const bool AUTO_RESIZE = true;
static const bool KEEP_ASPECT_RATIO = true;

// 统计更新间隔
static const int STATS_UPDATE_INTERVAL = 1000; // 1秒
```

## 依赖项

- Qt 6.8.3 (Core, WebSockets, GUI, Widgets)
- libvpx (VP9解码)
- Windows API

## 系统要求

- **操作系统**: Windows 10/11
- **架构**: x64
- **内存**: 最少512MB可用内存
- **显卡**: 支持硬件加速的显卡 (可选)

## 注意事项

1. **单连接**: 每个进程实例只能连接一个视频源
2. **资源管理**: 进程退出时自动清理所有资源
3. **线程安全**: 内部使用Qt的信号槽机制保证线程安全
4. **内存优化**: 自动管理解码缓冲区，避免内存泄漏

## 版本信息

- **当前版本**: 1.0
- **兼容性**: Windows 10/11
- **架构**: x64
- **更新日期**: 2025年1月

## 故障排除

### 常见问题

**Q: 播放器启动后没有画面？**
A: 检查WebSocket服务器是否正在运行，确认服务器地址是否正确。

**Q: 画面卡顿或延迟？**
A: 检查网络连接质量，确认系统资源是否充足。

**Q: 进程意外退出？**
A: 查看日志输出，通常是解码器初始化失败或内存不足导致。

**Q: 连接频繁断开？**
A: 检查网络稳定性，确认服务器端是否正常运行。