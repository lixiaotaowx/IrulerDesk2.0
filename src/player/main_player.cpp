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
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "Winmm.lib")
#endif
#include "../common/ConsoleLogger.h"
#include "../common/CrashGuard.h"
#include "../common/AppConfig.h"

int main(int argc, char *argv[])
{
    #ifdef _WIN32
    timeBeginPeriod(1);
    #endif
    // 安装崩溃守护与控制台日志重定向
    CrashGuard::install();
    ConsoleLogger::attachToParentConsole();
    ConsoleLogger::installQtMessageHandler();

    QApplication app(argc, argv); // 使用QApplication支持GUI窗口
    AppConfig::applyApplicationInfo(app);
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    app.setWindowIcon(QIcon(QCoreApplication::applicationDirPath() + "/maps/logo/iruler.ico"));
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
    QString serverUrl = AppConfig::wsBaseUrl();
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

    serverUrl = AppConfig::normalizeWsBaseUrl(serverUrl);
    
    // 检查是否提供了目标设备ID
    if (targetDeviceId.isEmpty()) {
        return -1;
    }

    QString viewerId = AppConfig::readConfigValue(QStringLiteral("random_id")).trimmed();
    if (viewerId.isEmpty()) {
        viewerId = QStringLiteral("viewer_%1").arg(QDateTime::currentMSecsSinceEpoch());
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
        
        // 连接到WebSocket服务器 - 使用订阅URL
        QString subscribeUrl = QString("%1/subscribe/%2").arg(serverUrl).arg(targetDeviceId);
        // 如果serverUrl不包含ws://，添加它 (这里假设serverUrl是完整的ws://...，如果不是需要处理)
        // 现有的serverUrl处理逻辑比较简单，这里假设serverUrl是 "ws://ip:port"
        
        receiver.setSessionInfo(viewerId, targetDeviceId);
        receiver.connectToServer(subscribeUrl);

        
        int r = app.exec();
        #ifdef _WIN32
        timeEndPeriod(1);
        #endif
        return r;
    } else {
        // 独立窗口模式：显示完整的视频播放窗口
        
        
        VideoDisplayWidget *videoWidget = new VideoDisplayWidget();
        videoWidget->setWindowIcon(QIcon(QCoreApplication::applicationDirPath() + "/maps/logo/iruler.ico"));
        videoWidget->setWindowTitle("屏幕流播放器");
        videoWidget->resize(800, 600);
        videoWidget->show();
        
        // 自动开始接收 - 使用订阅URL
        QString subscribeUrl = QString("%1/subscribe/%2").arg(serverUrl).arg(targetDeviceId);

        // 先设置会话信息，确保连接建立时能携带正确的 viewer_id
        qInfo().noquote() << "[PlayerProcess] Initializing session info: viewerId=" << viewerId << " targetId=" << targetDeviceId;
        videoWidget->setSessionInfo(viewerId, targetDeviceId);
        
        videoWidget->startReceiving(subscribeUrl);
        
        
        int result = app.exec();
        #ifdef _WIN32
        timeEndPeriod(1);
        #endif
        delete videoWidget;
        return result;
    }
}
