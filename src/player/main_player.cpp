#include <QApplication>
#include "VP9Decoder.h"
#include "WebSocketReceiver.h"
#include "VideoRenderer.h"
#include "../video_components/VideoDisplayWidget.h"
#include <QDebug>
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
    
    qDebug() << "[PlayerProcess] ========== 启动播放进程 ==========";
    
    // 检查命令行参数
    bool embeddedMode = false;
    QString serverUrl = "ws://123.207.222.92:8765";
    QString targetDeviceId; // 目标设备ID
    
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--embedded") {
            embeddedMode = true;
            qDebug() << "[PlayerProcess] 使用嵌入模式";
        } else if (arg.startsWith("--server=")) {
            serverUrl = arg.mid(9);
            qDebug() << "[PlayerProcess] 使用服务器:" << serverUrl;
        } else if (arg.startsWith("--target=")) {
            targetDeviceId = arg.mid(9);
            qDebug() << "[PlayerProcess] 目标设备ID:" << targetDeviceId;
        } else {
            // 如果没有前缀，直接作为目标设备ID
            targetDeviceId = arg;
            qDebug() << "[PlayerProcess] 目标设备ID:" << targetDeviceId;
        }
    }
    
    // 检查是否提供了目标设备ID
    if (targetDeviceId.isEmpty()) {
        qCritical() << "[PlayerProcess] 错误：未提供目标设备ID";
        qDebug() << "[PlayerProcess] 用法: PlayerProcess.exe <target_device_id> [--server=ws://server:port]";
        return -1;
    }
    
    if (embeddedMode) {
        // 嵌入模式：只提供功能，不显示窗口
        qDebug() << "[PlayerProcess] 嵌入模式：创建后台服务...";
        
        // 创建组件
        VP9Decoder decoder;
        WebSocketReceiver receiver;
        
        // 初始化解码器
        if (!decoder.initialize()) {
            qCritical() << "[PlayerProcess] VP9解码器初始化失败";
            return -1;
        }
        qDebug() << "[PlayerProcess] VP9解码器初始化成功";
        
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
        receiver.connectToServer(serverUrl);
        
        // 发送观看请求
        QString viewerId = QString("viewer_%1").arg(QDateTime::currentMSecsSinceEpoch());
        receiver.sendWatchRequest(viewerId, targetDeviceId);
        
        qDebug() << "[PlayerProcess] 嵌入模式启动完成，已发送观看请求...";
        
        return app.exec();
    } else {
        // 独立窗口模式：显示完整的视频播放窗口
        qDebug() << "[PlayerProcess] 独立窗口模式：创建视频显示窗口...";
        
        VideoDisplayWidget *videoWidget = new VideoDisplayWidget();
        videoWidget->setWindowTitle("屏幕流播放器");
        videoWidget->resize(800, 600);
        videoWidget->show();
        
        // 自动开始接收
        videoWidget->startReceiving(serverUrl);
        
        // 发送观看请求（独立窗口模式也需要）
        QString viewerId = QString("viewer_%1").arg(QDateTime::currentMSecsSinceEpoch());
        // 延迟发送观看请求，确保连接已建立
        QTimer::singleShot(2000, [videoWidget, viewerId, targetDeviceId]() {
            videoWidget->sendWatchRequest(viewerId, targetDeviceId);
            qDebug() << "[PlayerProcess] 已发送观看请求，观看者ID:" << viewerId << "目标设备ID:" << targetDeviceId;
        });
        
        qDebug() << "[PlayerProcess] 独立窗口模式启动完成";
        
        int result = app.exec();
        delete videoWidget;
        return result;
    }
}