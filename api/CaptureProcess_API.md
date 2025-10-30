# CaptureProcess API 文档

## 概述

CaptureProcess 是一个独立的屏幕捕获进程，集成了VP9编码器和WebSocket服务器功能。它可以实时捕获屏幕内容，进行VP9编码，并通过WebSocket协议向客户端发送编码后的视频流。

## 启动方式

```bash
./CaptureProcess.exe
```

### 启动参数

当前版本不需要命令行参数，所有配置都是硬编码的。

## 功能特性

- **屏幕捕获**: 实时捕获整个屏幕内容，支持D3D11硬件加速
- **VP9编码**: 使用libvpx库进行高效的VP9视频编码，支持多线程编码
- **WebSocket服务器**: 内置WebSocket服务器，端口8765
- **高帧率捕获**: 支持高达40fps的捕获频率
- **硬件加速**: 优先使用D3D11硬件加速捕获，失败时自动回退到Qt软件捕获
- **实时编码**: 优化的实时编码参数，低延迟高质量
- **自动重连**: 支持客户端断开重连
- **性能统计**: 内置帧率和编码性能统计

## WebSocket API

### 服务器信息

- **协议**: WebSocket (ws://)
- **地址**: localhost:8765
- **端口**: 8765
- **路径**: /

### 连接示例

```javascript
const ws = new WebSocket('ws://localhost:8765');

ws.onopen = function() {
    console.log('连接到屏幕捕获服务器');
};

ws.onmessage = function(event) {
    // event.data 包含VP9编码的视频帧数据
    const frameData = event.data;
    // 处理VP9帧数据
};

ws.onclose = function() {
    console.log('与服务器断开连接');
};
```

### 数据格式

#### 发送的数据 (服务器 → 客户端)

- **数据类型**: 二进制数据 (ArrayBuffer)
- **内容**: VP9编码的视频帧
- **频率**: 约30fps
- **大小**: 变长，通常几KB到几十KB

#### 接收的数据 (客户端 → 服务器)

当前版本不处理客户端发送的数据。

## 集成示例

### C++ Qt 集成

```cpp
#include <QWebSocket>
#include <QProcess>

class ScreenCaptureClient : public QObject {
    Q_OBJECT
    
public:
    void startCapture() {
        // 启动捕获进程
        m_captureProcess = new QProcess(this);
        m_captureProcess->start("./CaptureProcess.exe");
        
        // 等待服务器启动
        QTimer::singleShot(2000, [this]() {
            // 连接WebSocket
            m_webSocket = new QWebSocket();
            connect(m_webSocket, &QWebSocket::binaryMessageReceived,
                    this, &ScreenCaptureClient::onFrameReceived);
            m_webSocket->open(QUrl("ws://localhost:8765"));
        });
    }
    
private slots:
    void onFrameReceived(const QByteArray &data) {
        // 处理VP9编码的帧数据
        // 需要使用VP9解码器解码
    }
    
private:
    QProcess *m_captureProcess;
    QWebSocket *m_webSocket;
};
```

### Python 集成

```python
import asyncio
import websockets
import subprocess

class ScreenCaptureClient:
    def __init__(self):
        self.process = None
        self.websocket = None
    
    async def start_capture(self):
        # 启动捕获进程
        self.process = subprocess.Popen(['./CaptureProcess.exe'])
        
        # 等待服务器启动
        await asyncio.sleep(2)
        
        # 连接WebSocket
        self.websocket = await websockets.connect('ws://localhost:8765')
        
        # 接收数据
        async for message in self.websocket:
            # message 包含VP9编码的帧数据
            await self.process_frame(message)
    
    async def process_frame(self, frame_data):
        # 处理VP9帧数据
        # 需要使用VP9解码器解码
        pass
```

## 技术实现

### 屏幕捕获
- **D3D11硬件加速**: 优先使用Direct3D 11进行高性能屏幕捕获
  - 使用DXGI Desktop Duplication API
  - 支持WAIT_TIMEOUT处理，避免无新帧时的阻塞
  - 硬件加速失败时自动回退到Qt软件捕获
- **Qt软件捕获**: 使用QScreen::grabWindow()作为备用方案
- 捕获频率: 最高40fps，实际达到15-20fps
- 图像格式: RGBA32

### VP9编码
- 编码器: libvpx (vpx_codec_vp9_cx)
- 编码配置:
  - 目标比特率: 2Mbps
  - 关键帧间隔: 50帧 (优化后)
  - 线程数: 4 (多线程编码)
  - 实时模式: 启用
  - 错误恢复: 启用
  - 编码速度: 实时优化参数
  - 缓冲区: 优化的缓冲区管理

## 性能指标

### 优化后性能 (D3D11硬件加速)
- **捕获帧率**: 15.4 FPS (实测)
- **捕获延迟**: < 25ms
- **编码延迟**: < 30ms
- **总延迟**: < 60ms
- **内存使用**: ~60MB
- **CPU使用率**: 10-20% (硬件加速)

### 回退性能 (Qt软件捕获)
- **捕获帧率**: 9-10 FPS
- **捕获延迟**: < 33ms
- **编码延迟**: < 50ms
- **总延迟**: < 100ms
- **CPU使用率**: 20-30%

### 网络性能
- **网络带宽**: 1-10 Mbps (取决于屏幕内容变化)

## 错误处理

### 常见错误

1. **端口被占用**: 如果8765端口被占用，进程会启动失败
2. **编码器初始化失败**: VP9编码器初始化失败
3. **屏幕捕获失败**: 无法访问屏幕内容

### 日志输出

进程会输出详细的调试日志，包括：
- 启动状态
- WebSocket连接状态
- 编码统计信息
- 错误信息

## 依赖项

- Qt 6.8.3 (Core, WebSockets, GUI)
- libvpx (VP9编码)
- Windows API (屏幕捕获)

## 注意事项

1. **单实例运行**: 同时只能运行一个CaptureProcess实例
2. **权限要求**: 需要屏幕捕获权限
3. **资源清理**: 进程退出时会自动清理资源
4. **网络安全**: 当前版本不支持加密，仅适用于本地网络

## 版本信息

- **当前版本**: 1.0
- **兼容性**: Windows 10/11
- **架构**: x64