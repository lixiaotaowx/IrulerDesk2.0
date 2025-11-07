#include <QApplication>
#include "VP9Decoder.h"
#include "WebSocketReceiver.h"
#include "VideoRenderer.h"
#include "../video_components/VideoDisplayWidget.h"
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <iostream>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    // 在Windows上分配控制台以显示调试输出
    if (AllocConsole()) {
        freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
        freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);
        freopen_s((FILE**)stdin, "CONIN$", "r", stdin);
        std::cout.clear();
        std::cerr.clear();
        std::cin.clear();
    }
    // 设置控制台编码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    QApplication app(argc, argv); // 使用QApplication支持GUI窗口
    // 统一工具提示样式为黑底白字（播放器窗口也受益）
    app.setStyleSheet(
        "QToolTip {\n"
        "    color: #ffffff;\n"
        "    background-color: #000000;\n"
        "    border: 1px solid #4c4c4c;\n"
        "}"
    );
    
    
    
    // 检查命令行参数
    bool embeddedMode = false;
    QString serverUrl = "ws://123.207.222.92:8765";
    QString targetDeviceId; // 目标设备ID
    
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--embedded") {
            embeddedMode = true;
            
        } else if (arg.startsWith("--server=")) {
            serverUrl = arg.mid(9);
            
        } else if (arg.startsWith("--target=")) {
            targetDeviceId = arg.mid(9);
            
        } else {
            // 如果没有前缀，直接作为目标设备ID
            targetDeviceId = arg;
            
        }
    }
    
    // 检查是否提供了目标设备ID
    if (targetDeviceId.isEmpty()) {
        std::cout << "[PlayerProcess] missing --target device id, exiting" << std::endl;
        return -1;
    }
    
    if (embeddedMode) {
        // 嵌入模式：只提供功能，不显示窗口
        
        
        // 创建组件
        VP9Decoder decoder;
        WebSocketReceiver receiver;
        
        // 初始化解码器
        if (!decoder.initialize()) {
            return -1;
        }
        
        // 连接信号槽 - 解码后的帧数据写入共享内存或文件
        QObject::connect(&receiver, &WebSocketReceiver::frameReceived, [&decoder](const QByteArray &frameData) {
            decoder.decode(frameData);
        });
        
        // 解码完成后的统计
        static int decodedFrameCount = 0;
        QObject::connect(&decoder, &VP9Decoder::frameDecoded, [](const QByteArray &frameData, const QSize &frameSize) {
            decodedFrameCount++;
            // 每50帧输出一次日志，避免垃圾消息
            if (decodedFrameCount % 50 == 0) {
                // qDebug() << "[PlayerProcess] 已解码" << decodedFrameCount << "帧，最新帧尺寸:" << frameSize; // 已禁用以提升性能
            }
        });
        
        // 连接到WebSocket服务器
        std::cout << "[PlayerProcess] connecting: url=" << serverUrl.toStdString()
                  << ", target=" << targetDeviceId.toStdString() << std::endl;
        receiver.connectToServer(serverUrl);
        
        // 发送观看请求
        QString viewerId = QString("viewer_%1").arg(QDateTime::currentMSecsSinceEpoch());
        std::cout << "[PlayerProcess] sendWatchRequest: viewer=" << viewerId.toStdString()
                  << ", target=" << targetDeviceId.toStdString() << std::endl;
        receiver.sendWatchRequest(viewerId, targetDeviceId);
        
        
        
        return app.exec();
    } else {
        // 独立窗口模式：显示完整的视频播放窗口
        
        
        VideoDisplayWidget *videoWidget = new VideoDisplayWidget();
        videoWidget->setWindowTitle("屏幕流播放器");
        videoWidget->resize(800, 600);
        videoWidget->show();
        
        // 自动开始接收
        std::cout << "[PlayerProcess] UI-mode startReceiving: url=" << serverUrl.toStdString() << std::endl;
        videoWidget->startReceiving(serverUrl);
        
        // 发送观看请求（独立窗口模式也需要）
        QString viewerId = QString("viewer_%1").arg(QDateTime::currentMSecsSinceEpoch());
        // 延迟发送观看请求，确保连接已建立
        QTimer::singleShot(2000, [videoWidget, viewerId, targetDeviceId]() {
            std::cout << "[PlayerProcess] UI-mode sendWatchRequest after delay: viewer="
                      << viewerId.toStdString() << ", target=" << targetDeviceId.toStdString() << std::endl;
            videoWidget->sendWatchRequest(viewerId, targetDeviceId);
            
        });
        
        
        
        int result = app.exec();
        delete videoWidget;
        return result;
    }
}