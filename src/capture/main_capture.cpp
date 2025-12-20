#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QTimer>
#include <QDateTime>
#include <QVector>
#include <QMap>
#include <QQueue>
#include <QMutex>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <iostream>
#include <cmath>
#include <chrono>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QPermissions>
#endif
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>
#include <QtMultimedia/QAudioFormat>
#include <QtMultimedia/QAudioSource>
#include <QtMultimedia/QMediaDevices>
#include <QtMultimedia/QAudioBuffer>
#include <QtMultimedia/QAudioDecoder>
#include <QtMultimedia/QAudioSink>
#include <QUrl>
#include <QUrl>
#include <opus/opus.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include "../common/ConsoleLogger.h"
#include "../common/CrashGuard.h"
#include "ScreenCapture.h"
#include "VP9Encoder.h"
#include "WebSocketSender.h"
#include "MouseCapture.h" // 新增：鼠标捕获头文件
// 性能监控禁用：避免统计带来的额外开销
#include "AnnotationOverlay.h"
#include "CursorOverlay.h"


// 新增：读取本地默认质量设置
QString getLocalQualityFromConfig()
{
    QStringList configPaths;
    // 覆盖多种位置，兼容现有读取策略
    configPaths << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/config/app_config.txt";
    configPaths << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/app_config.txt";
    configPaths << QDir::currentPath() + "/config/app_config.txt";
    configPaths << QCoreApplication::applicationDirPath() + "/config/app_config.txt";

    for (const QString& path : configPaths) {
        QFile configFile(path);
        if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&configFile);
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.startsWith("local_quality=")) {
                    QString q = line.mid(QString("local_quality=").length()).toLower();
                    configFile.close();
                    if (q == "low" || q == "medium" || q == "high" || q == "extreme") {
                        return q;
                    }
                }
            }
            configFile.close();
        }
    }
    return "medium";
}

QString getUserNameFromConfig()
{
    QStringList configPaths;
    configPaths << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/config/app_config.txt";
    configPaths << QDir::currentPath() + "/config/app_config.txt";
    configPaths << QCoreApplication::applicationDirPath() + "/config/app_config.txt";

    for (const QString& path : configPaths) {
        QFile configFile(path);
        if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&configFile);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            in.setEncoding(QStringConverter::Utf8);
#else
            in.setCodec("UTF-8");
#endif
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.startsWith("user_name=")) {
                    QString name = line.mid(10).trimmed();
                    configFile.close();
                    if (!name.isEmpty()) return name;
                }
            }
            configFile.close();
        }
    }
    return "";
}

bool getMicEnabledFromConfig()
{
    QStringList configPaths;
    configPaths << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/config/app_config.txt";
    configPaths << QDir::currentPath() + "/config/app_config.txt";
    configPaths << QCoreApplication::applicationDirPath() + "/config/app_config.txt";

    for (const QString& path : configPaths) {
        QFile configFile(path);
        if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&configFile);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.startsWith("mic_enabled=")) {
                    const QString v = line.mid(QString("mic_enabled=").length()).trimmed();
                    configFile.close();
                    return v.compare("true", Qt::CaseInsensitive) == 0;
                }
            }
            configFile.close();
        }
    }
    return false;
}

// 从配置文件读取设备ID
QString getDeviceIdFromConfig()
{
    // 尝试多个可能的配置文件路径
    QStringList possiblePaths;
    
    // 路径1：使用AppDataLocation
    QString configDir1 = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    possiblePaths << configDir1 + "/config/app_config.txt";
    
    // 路径2：使用当前工作目录
    possiblePaths << QDir::currentPath() + "/config/app_config.txt";
    
    // 路径3：使用应用程序目录
    QString appDir = QCoreApplication::applicationDirPath();
    possiblePaths << appDir + "/config/app_config.txt";
    
    // qDebug() << "[CaptureProcess] 尝试读取配置文件，可能的路径:";
    // for (const QString& path : possiblePaths) {
    //     qDebug() << "[CaptureProcess]   - " << path;
    // }
    
    for (const QString& configFilePath : possiblePaths) {
        QFile configFile(configFilePath);
        if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // qDebug() << "[CaptureProcess] 找到配置文件:" << configFilePath;
            QTextStream in(&configFile);
            QString line;
            while (!in.atEnd()) {
                line = in.readLine();
                if (line.startsWith("random_id=")) {
                    QString deviceId = line.mid(10); // 去掉"random_id="前缀
                    configFile.close();
                    // qDebug() << "[CaptureProcess] 从配置文件读取到设备ID:" << deviceId;
                    return deviceId;
                }
            }
            configFile.close();
        }
    }
    
    // 如果读取失败，返回默认值
    // qDebug() << "[CaptureProcess] 所有配置文件路径都无法读取，使用默认设备ID: 25561";
    return "25561";
}

QString getServerAddressFromConfig()
{
    // 尝试多个可能的配置文件路径
    QStringList possiblePaths;
    
    // 路径1：使用AppDataLocation
    QString configDir1 = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    possiblePaths << configDir1 + "/config/app_config.txt";
    
    // 路径2：使用当前工作目录
    possiblePaths << QDir::currentPath() + "/config/app_config.txt";
    
    // 路径3：使用应用程序目录
    QString appDir = QCoreApplication::applicationDirPath();
    possiblePaths << appDir + "/config/app_config.txt";
    
    // qDebug() << "[CaptureProcess] 尝试读取服务器地址配置，可能的路径:";
    // for (const QString& path : possiblePaths) {
    //     qDebug() << "[CaptureProcess]   - " << path;
    // }
    
    for (const QString& configFilePath : possiblePaths) {
        QFile configFile(configFilePath);
        if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // qDebug() << "[CaptureProcess] 找到配置文件:" << configFilePath;
            QTextStream in(&configFile);
            QString line;
            while (!in.atEnd()) {
                line = in.readLine();
                if (line.startsWith("server_address=")) {
                    QString serverAddress = line.mid(15); // 去掉"server_address="前缀
                    configFile.close();
                    // qDebug() << "[CaptureProcess] 从配置文件读取到服务器地址:" << serverAddress;
                    return serverAddress;
                }
            }
            configFile.close();
        }
    }
    
    // 如果读取失败，返回默认值（腾讯云）
    // qDebug() << "[CaptureProcess] 所有配置文件路径都无法读取服务器地址，使用默认 123.207.222.92:8765";
    return "123.207.222.92:8765";
}

// 新增：读取屏幕索引
int getScreenIndexFromConfig()
{
    QStringList configPaths;
    configPaths << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/app_config.txt";
    configPaths << QDir::currentPath() + "/config/app_config.txt";
    configPaths << QCoreApplication::applicationDirPath() + "/config/app_config.txt";

    for (const QString& path : configPaths) {
        QFile configFile(path);
        if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&configFile);
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.startsWith("screen_index=")) {
                    bool ok = false;
                    int idx = line.mid(QString("screen_index=").length()).toInt(&ok);
                    if (ok && idx >= 0) {
                        configFile.close();
                        return idx;
                    }
                }
            }
            configFile.close();
        }
    }
    return 0; // 默认主屏索引0
}

// 新增：保存屏幕索引到配置
static void saveScreenIndexToConfig(int screenIndex)
{
    QString configFilePath = QCoreApplication::applicationDirPath() + "/config/app_config.txt";
    QDir dir(QFileInfo(configFilePath).path());
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QStringList lines;
    QFile f(configFilePath);
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            lines << in.readLine();
        }
        f.close();
    }

    bool replaced = false;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].startsWith("screen_index=")) {
            lines[i] = QString("screen_index=%1").arg(screenIndex);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        lines << QString("screen_index=%1").arg(screenIndex);
    }

    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        for (const QString &line : lines) {
            out << line << "\n";
        }
        f.close();
    }
}

#include <QLocalSocket>

// -------------------------------------------------------------------------
// 看门狗客户端类
// -------------------------------------------------------------------------
class WatchdogClient : public QObject {
    Q_OBJECT
public:
    WatchdogClient(const QString& serverName, QObject* parent = nullptr) : QObject(parent) {
        m_socket = new QLocalSocket(this);
        m_timer = new QTimer(this);
        
        connect(m_timer, &QTimer::timeout, this, &WatchdogClient::sendHeartbeat);
        connect(m_socket, &QLocalSocket::readyRead, this, &WatchdogClient::onDataReceived);
        
        // 尝试连接
        m_socket->connectToServer(serverName);
        
        // 每1秒发送一次心跳
        m_timer->start(1000);
    }

    void notifyViewerExited(const QString &viewerId)
    {
        if (viewerId.isEmpty()) {
            return;
        }
        if (m_socket->state() == QLocalSocket::ConnectedState) {
            m_socket->write(QByteArray("EVT_VIEWER_EXIT:") + viewerId.toUtf8() + "\n");
            m_socket->flush();
        }
    }

    void notifyViewerMicState(const QString &viewerId, bool enabled)
    {
        if (viewerId.isEmpty()) {
            return;
        }
        if (m_socket->state() == QLocalSocket::ConnectedState) {
            m_socket->write(QByteArray("EVT_VIEWER_MIC:") + viewerId.toUtf8() + (enabled ? ":1\n" : ":0\n"));
            m_socket->flush();
        }
    }

signals:
    void approvalReceived();
    void rejectionReceived();
    void switchScreenRequested(int index);
    void audioToggleRequested(bool enabled);

private slots:
    void sendHeartbeat() {
        if (m_socket->state() == QLocalSocket::ConnectedState) {
            m_socket->write("1\n");
            m_socket->flush();
        } else if (m_socket->state() == QLocalSocket::UnconnectedState) {
            // 如果连接断开，尝试重连
            m_socket->connectToServer(m_socket->serverName());
        }
    }

    void onDataReceived() {
        QByteArray data = m_socket->readAll();
        QString str = QString::fromUtf8(data);
        if (str.contains("CMD_APPROVE")) {
            qDebug() << "[CaptureProcess] Received local approval command from MainWindow.";
            emit approvalReceived();
        } else if (str.contains("CMD_REJECT")) {
            qDebug() << "[CaptureProcess] Received local rejection command from MainWindow.";
            emit rejectionReceived();
        } else if (str.startsWith("CMD_SWITCH_SCREEN:")) {
            bool ok;
            int idx = str.mid(18).toInt(&ok);
            if (ok) {
                qDebug() << "[CaptureProcess] Received local switch screen command from MainWindow:" << idx;
                emit switchScreenRequested(idx);
            }
        } else if (str.contains("CMD_AUDIO_TOGGLE:")) {
            const QString prefix = QStringLiteral("CMD_AUDIO_TOGGLE:");
            const int p = str.lastIndexOf(prefix);
            if (p >= 0) {
                const QString tail = str.mid(p + prefix.size()).trimmed();
                bool ok = false;
                const int v = tail.toInt(&ok);
                if (ok) {
                    emit audioToggleRequested(v != 0);
                }
            }
        }
    }

private:
    QLocalSocket* m_socket;
    QTimer* m_timer;
};

int main(int argc, char *argv[])
{
    // 安装崩溃守护与控制台日志重定向
    CrashGuard::install();
    ConsoleLogger::attachToParentConsole();
    ConsoleLogger::installQtMessageHandler();

    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QCoreApplication::applicationDirPath() + "/maps/logo/iruler.ico"));
    
    // -------------------------------------------------------------------------
    // 启动看门狗客户端
    // -------------------------------------------------------------------------
    QString watchdogPipeName;
    QStringList args = app.arguments();
    for (int i = 0; i < args.size() - 1; ++i) {
        if (args[i] == "--watchdog") {
            watchdogPipeName = args[i + 1];
            break;
        }
    }
    
    WatchdogClient *watchdog = nullptr;
    if (!watchdogPipeName.isEmpty()) {
        watchdog = new WatchdogClient(watchdogPipeName, &app);
        // qDebug() << "[CaptureProcess] Watchdog client started, connecting to:" << watchdogPipeName;
    }
    // -------------------------------------------------------------------------
    
    // 尝试申请麦克风权限 (Qt 6.5+)
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    qDebug() << "[Audio] Checking microphone permission...";
    QMicrophonePermission micPermission;
    switch (app.checkPermission(micPermission)) {
    case Qt::PermissionStatus::Undetermined:
        qDebug() << "[Audio] Permission: Undetermined. Requesting...";
        app.requestPermission(micPermission, [](const QPermission &permission) {
            if (permission.status() == Qt::PermissionStatus::Granted) {
                qDebug() << "[Audio] Permission: Granted by user.";
            } else {
                qDebug() << "[Audio] Permission: Denied by user.";
            }
        });
        break;
    case Qt::PermissionStatus::Denied:
        qDebug() << "[Audio] Permission: Denied (System). Please check Privacy Settings!";
        break;
    case Qt::PermissionStatus::Granted:
        qDebug() << "[Audio] Permission: Granted (System).";
        break;
    }
#endif
    
    
    
    // 创建屏幕捕获对象
    ScreenCapture *capture = new ScreenCapture(&app);
    capture->setTargetScreenIndex(getScreenIndexFromConfig());
    if (!capture->initialize()) {
        return -1;
    }
    // qDebug() << "[CaptureProcess] 屏幕捕获模块初始化成功";
    
    // 创建VP9编码器
    QSize actualScreenSize = capture->getScreenSize();
    QSize initEncodeSize = actualScreenSize;

    // 限制最大宽度为 1920，超过则等比缩放
    if (initEncodeSize.width() > 1920) {
        double ratio = 1920.0 / initEncodeSize.width();
        initEncodeSize.setWidth(1920);
        initEncodeSize.setHeight(qRound(initEncodeSize.height() * ratio));
        // 确保宽高是偶数（某些编码器要求）
        if (initEncodeSize.width() % 2 != 0) initEncodeSize.setWidth(initEncodeSize.width() - 1);
        if (initEncodeSize.height() % 2 != 0) initEncodeSize.setHeight(initEncodeSize.height() - 1);
        qDebug() << "[CaptureProcess] Screen > 1920, scaling down to:" << initEncodeSize;
    }
    
    VP9Encoder *encoder = new VP9Encoder(&app);
    // 保持现有帧率设置以降低编码负载（避免强制60fps）
    if (!encoder->initialize(initEncodeSize.width(), initEncodeSize.height(), encoder->getFrameRate())) {
        return -1;
    }
    // qDebug() << "[CaptureProcess] VP9编码器初始化成功";
    
    // 配置VP9静态检测优化参数 - 极致流量控制
    // qDebug() << "[CaptureProcess] 配置VP9静态检测优化...";
    encoder->setEnableStaticDetection(true);        // 启用静态检测
    encoder->setStaticThreshold(0.005);             // 设置0.5%的变化阈值，非常敏感
    encoder->setStaticBitrateReduction(0.10);       // 静态内容码率减少到10%，极致压缩
    encoder->setSkipStaticFrames(true);             // 启用静态帧跳过，画面静止时不发送数据
    // qDebug() << "[CaptureProcess] VP9静态检测优化配置完成";
    //     qDebug() << "[CaptureProcess] - 静态检测阈值: 5%";
    //     qDebug() << "[CaptureProcess] - 码率减少: 50%";
    //     qDebug() << "[CaptureProcess] - 帧跳过: 禁用";
    
    // 在编码器准备好后再启动WebSocket发送器
    // qDebug() << "[CaptureProcess] 启动WebSocket发送器...";
    WebSocketSender *sender = new WebSocketSender(&app);
    
    static QVector<AnnotationOverlay*> s_overlays;
    static QVector<CursorOverlay*> s_cursorOverlays;
    s_overlays.clear();
    s_cursorOverlays.clear();
    {
        const auto screensInit = QApplication::screens();
        for (int i = 0; i < screensInit.size(); ++i) {
            AnnotationOverlay *ov = new AnnotationOverlay();
            ov->alignToScreen(screensInit[i]);
            ov->show();
            ov->raise();
            s_overlays.append(ov);
            CursorOverlay *cv = new CursorOverlay();
            cv->alignToScreen(screensInit[i]);
            cv->show();
            cv->raise();
            s_cursorOverlays.append(cv);
        }
    }
    
    // WebSocket连接逻辑已移动到main函数末尾，确保信号连接完成后再启动连接

    
    // 已禁用性能监控，减少CPU开销
    // 连接信号槽
    // qDebug() << "[CaptureProcess] 连接编码器和服务器信号槽...";
    QObject::connect(encoder, &VP9Encoder::frameEncodedWithInfo,
                     sender, &WebSocketSender::enqueueFrame);
    
    // 连接首帧关键帧策略信号槽
    QObject::connect(sender, &WebSocketSender::requestKeyFrame,
                     encoder, &VP9Encoder::forceKeyFrame);
    // qDebug() << "[CaptureProcess] [首帧策略] 已连接关键帧请求信号槽";
    
    // 创建鼠标捕获模块
    // qDebug() << "[CaptureProcess] 初始化鼠标捕获模块...";
    MouseCapture *mouseCapture = new MouseCapture(&app);
    
    // 设置鼠标坐标缩放和屏幕偏移
    // 计算当前屏幕的物理区域
    QRect currentScreenRect;
    int initialScreenIdx = getScreenIndexFromConfig();
    if (initialScreenIdx >= 0 && initialScreenIdx < QApplication::screens().size()) {
        QScreen *scr = QApplication::screens().at(initialScreenIdx);
        qreal dpr = scr->devicePixelRatio();
        QRect geo = scr->geometry();
        currentScreenRect = QRect(qRound(geo.x() * dpr), qRound(geo.y() * dpr), 
                                  qRound(geo.width() * dpr), qRound(geo.height() * dpr));
    } else {
        currentScreenRect = QRect(0, 0, actualScreenSize.width(), actualScreenSize.height());
    }
    
    mouseCapture->setScreenRect(currentScreenRect, initEncodeSize);
    
    // 鼠标坐标转换的连接将在下面（静态变量声明之后）设置
    // qDebug() << "[CaptureProcess] 鼠标捕获模块初始化成功，已连接到WebSocket发送器";
    
    // 创建定时器进行屏幕捕获 - 优化：减少lambda捕获开销
    // qDebug() << "[CaptureProcess] 创建屏幕捕获定时器(60fps)...";
    QTimer *captureTimer = new QTimer(&app);
    
    // 优化：使用静态变量避免lambda捕获开销
    static int frameCount = 0;
    static ScreenCapture *staticCapture = capture;
    static VP9Encoder *staticEncoder = encoder;
    static MouseCapture *staticMouseCapture = mouseCapture; // 新增：静态鼠标捕获指针
    static WebSocketSender *staticSender = sender; // 新增：静态WebSocket发送器指针
    static bool isCapturing = false; // 控制捕获状态
    static int currentScreenIndex = getScreenIndexFromConfig(); // 当前屏幕索引
    static QString currentUserName = getUserNameFromConfig(); // 当前用户名
    static bool isSwitching = false; // 屏幕热切换中标志（不断流）
    // 新增：质量控制相关静态状态
    static QString currentQuality = getLocalQualityFromConfig();
    static QSize targetEncodeSize = staticEncoder->getFrameSize();

    // -------------------------------------------------------------------------
    // 屏幕切换处理逻辑 (Screen Switch Logic)
    // -------------------------------------------------------------------------
    auto handleScreenSwitch = [&](int targetIndex = -1) {
        const auto screens = QApplication::screens();
        if (screens.isEmpty()) {
            return;
        }

        // 计算目标屏幕索引
        if (targetIndex >= 0 && targetIndex < screens.size()) {
            currentScreenIndex = targetIndex;
        } else {
            // 默认滚动到下一屏幕
            currentScreenIndex = (currentScreenIndex + 1) % screens.size();
        }
        saveScreenIndexToConfig(currentScreenIndex);
        
        // 标记正在切换以避免捕获循环继续抓帧（不断流，仅暂时不发帧）
        isSwitching = true;

        // 重新初始化屏幕捕获到新屏幕
        staticCapture->cleanup();
        staticCapture->setTargetScreenIndex(currentScreenIndex);
        if (!staticCapture->initialize()) {
            isSwitching = false;
            return;
        }

        QSize newSize = staticCapture->getScreenSize();
        QSize encodeSize = newSize;

        // 限制最大长边为 1920，超过则等比缩放 (适用于超宽屏或竖屏)
        if (encodeSize.width() > 1920 || encodeSize.height() > 1920) {
            double ratioW = 1920.0 / encodeSize.width();
            double ratioH = 1920.0 / encodeSize.height();
            double ratio = std::min(ratioW, ratioH);
            
            encodeSize.setWidth(qRound(encodeSize.width() * ratio));
            encodeSize.setHeight(qRound(encodeSize.height() * ratio));
            
            // 确保偶数宽高
            if (encodeSize.width() % 2 != 0) encodeSize.setWidth(encodeSize.width() - 1);
            if (encodeSize.height() % 2 != 0) encodeSize.setHeight(encodeSize.height() - 1);
        }

        // 重新初始化编码器以匹配新分辨率
        staticEncoder->cleanup();
        // 切屏后保持既有帧率，避免强制提升到60fps
        if (!staticEncoder->initialize(encodeSize.width(), encodeSize.height(), staticEncoder->getFrameRate())) {
            isSwitching = false;
            return;
        }
        
        // 更新目标编码尺寸和鼠标缩放
        targetEncodeSize = encodeSize;
        
        // 更新鼠标捕获的屏幕区域信息
        QRect newScreenRect;
        if (currentScreenIndex >= 0 && currentScreenIndex < screens.size()) {
            QScreen *scr = screens.at(currentScreenIndex);
            qreal dpr = scr->devicePixelRatio();
            QRect geo = scr->geometry();
            newScreenRect = QRect(qRound(geo.x() * dpr), qRound(geo.y() * dpr), 
                                  qRound(geo.width() * dpr), qRound(geo.height() * dpr));
        } else {
            newScreenRect = QRect(0, 0, newSize.width(), newSize.height());
        }
        
        staticMouseCapture->setScreenRect(newScreenRect, encodeSize);
        
        staticEncoder->forceKeyFrame();

        if (currentScreenIndex >= 0 && currentScreenIndex < s_overlays.size()) {
            s_overlays[currentScreenIndex]->raise();
        }
        if (currentScreenIndex >= 0 && currentScreenIndex < s_cursorOverlays.size()) {
            s_cursorOverlays[currentScreenIndex]->raise();
        }

        // 切换完成，恢复捕获循环发帧
        isSwitching = false;
    };

    // 新增：音频发送（改为麦克风采集，基于文本消息）
    QTimer *audioTimer = new QTimer(&app);
    audioTimer->setInterval(20); // 20ms 一帧
    static double audioPhase = 0.0;
    static int audioSampleRate = 16000; // 目标：16kHz 单声道 16bit PCM
    static const int audioChannels = 1;
    static const int audioBitsPerSample = 16;
    static bool remoteAudioEnabled = false; // 默认关闭音频发送
    static bool localMicEnabled = getMicEnabledFromConfig();
    static bool serverAudioWanted = false;
    static int audioFrameSendCount = 0; // 发送帧计数
    
    // 远程音频播放相关变量
    // 多人混音支持
    static QMap<QString, OpusDecoder*> peerDecoders;
    static QMap<QString, QQueue<QByteArray>> peerQueues;
    static QMap<QString, int> peerSilenceCounts;
    static QMap<QString, qint64> peerLastActiveTimes;
    static QMap<QString, bool> peerBuffering; // New: Buffering state for jitter control
    static QMap<QString, bool> peerMicOn;
    static QMutex mixMutex;
    static QByteArray mixBuffer;
    
    static QAudioSink *mixSink = nullptr;
    static QIODevice *mixIO = nullptr;
    static QTimer *mixTimer = nullptr;
    
    static bool remoteListenEnabled = true;

    static OpusEncoder *opusEnc = nullptr;
    static int opusSampleRate = audioSampleRate;
    static int opusFrameSize = 0; // samples per 20ms
    static QString testMp3Path;
    QByteArray envMp3 = qgetenv("IRULER_AUDIO_TEST_MP3");
    if (!envMp3.isEmpty()) {
        testMp3Path = QString::fromLocal8Bit(envMp3);
    } else {
        testMp3Path = QString::fromLocal8Bit("g:/c/2025/lunzi/IrulerDeskpro/src/audio/test.mp3");
    }
    static QAudioDecoder *mp3Decoder = nullptr;
    static QByteArray pcmAccum;

    // 麦克风采集初始化
    QAudioFormat micFormat;
    // Opus 支持的采样率列表 (优先高采样率)
    const QList<int> opusRates = {48000, 24000, 16000, 12000, 8000};
    bool formatFound = false;

    QAudioDevice inDev = QMediaDevices::defaultAudioInput();
    
    // 1. 尝试直接匹配 Opus 支持的采样率 (优先 48kHz)
    // 检查 Int16 和 Float 格式，因为有些设备（特别是 High Def Audio）在 Shared Mode 下更喜欢 Float
    // 如果能以 48kHz 采集，就能利用操作系统的高质量重采样（如果硬件本身不是48k），
    // 从而避免我们自己做低质量的线性插值重采样。
    for (int rate : opusRates) {
        // 先试 Int16
        QAudioFormat fmt;
        fmt.setSampleRate(rate);
        fmt.setChannelCount(1); 
        fmt.setSampleFormat(QAudioFormat::Int16);
        
        if (inDev.isFormatSupported(fmt)) {
            micFormat = fmt;
            formatFound = true;
            qDebug() << "[Audio] Found supported Opus format (Int16):" << rate << "Hz";
            break;
        }

        // 再试 Float
        fmt.setSampleFormat(QAudioFormat::Float);
        if (inDev.isFormatSupported(fmt)) {
            micFormat = fmt;
            formatFound = true;
            qDebug() << "[Audio] Found supported Opus format (Float):" << rate << "Hz";
            break;
        }
    }

    // 2. 如果标准检查失败，尝试“强行”请求 48kHz
    // 因为 isFormatSupported 有时在 Windows WASAPI 下不准确，或者过于保守。
    // 我们宁愿让 OS 帮我们重采样，也不愿自己做。
    if (!formatFound) {
        QAudioFormat tryFmt;
        tryFmt.setSampleRate(48000);
        tryFmt.setChannelCount(1);
        tryFmt.setSampleFormat(QAudioFormat::Int16);
        
        // 我们不检查 isFormatSupported，直接标记为找到，后续尝试打开。
        // 如果打开失败，我们还有后面的错误处理逻辑吗？
        // 这里需要谨慎。我们可以先尝试“信任”首选格式，但首选格式往往是 44100。
        // 让我们修改策略：如果没找到，我们先记录首选格式，但暂时把 micFormat 设为 48000 碰碰运气？
        // 不，如果不成功会很麻烦。
        
        // 还是保守一点：如果上面都没找到，就用首选格式。
        // 但是！我们可以尝试 Float 的首选格式是否是 48k？
        // 有些设备首选是 48k Float。
        
        qDebug() << "[Audio] No standard Opus format confirmed supported. Checking preferred...";
        QAudioFormat preferred = inDev.preferredFormat();
        qDebug() << "[Audio] Device Preferred format:" << preferred;
        
        // 如果首选格式本身就是 Opus 友好的 (48k/24k/16k...)，那最好
        if (opusRates.contains(preferred.sampleRate())) {
            micFormat = preferred;
             // 强制单声道以节省带宽（如果设备支持单声道切片）
            QAudioFormat tryMono = preferred;
            tryMono.setChannelCount(1);
            if (inDev.isFormatSupported(tryMono)) {
                micFormat = tryMono;
            }
        } else {
             // 首选格式不是 Opus 标准 (例如 44100)。
             // 这时我们再做最后一次努力：尝试构造一个 48000 的格式，看设备是否"可能"支持
             // 有些驱动不报告支持，但其实能跑。
             // 但为了稳定性，我们还是回退到 preferred，然后让后续逻辑处理重采样。
             // 这里的关键是：上面的循环已经检查了 Int16 和 Float 的 48k。如果都失败了，说明系统真的不想给 48k。
             micFormat = preferred;
        }
        
        // 确保 micFormat 有效
        if (micFormat.sampleRate() <= 0) micFormat.setSampleRate(48000);
        if (micFormat.channelCount() <= 0) micFormat.setChannelCount(1);
    }

    audioSampleRate = micFormat.sampleRate();
    qDebug() << "[Audio] Selected Device:" << inDev.description();
    qDebug() << "[Audio] Selected Mic Format:" << micFormat << " SampleRate:" << audioSampleRate;

    QAudioSource *audioSource = new QAudioSource(inDev, micFormat, &app);
    audioSource->setVolume(1.0); // 确保音量最大
    QIODevice *audioInput = nullptr;
    int currentMicGainPercent = 100;
    
    // 确定 Opus 编码器使用的采样率
    // 如果采集率在 Opus 列表中，直接使用；否则强制使用 48000 (需要重采样)
    if (opusRates.contains(audioSampleRate)) {
        opusSampleRate = audioSampleRate;
    } else {
        opusSampleRate = 48000; // 默认重采样目标
        qDebug() << "[Audio] Mismatch sample rate. Will resample" << audioSampleRate << "->" << opusSampleRate;
    }
    opusFrameSize = opusSampleRate / 50; // 20ms

    {
        int srcBytesPerSample = 2;
        switch (micFormat.sampleFormat()) {
            case QAudioFormat::UInt8: srcBytesPerSample = 1; break;
            case QAudioFormat::Int16: srcBytesPerSample = 2; break;
            case QAudioFormat::Int32: srcBytesPerSample = 4; break;
            case QAudioFormat::Float: srcBytesPerSample = 4; break;
            default: srcBytesPerSample = 2; break;
        }
        audioSource->setBufferSize(opusFrameSize * srcBytesPerSample * micFormat.channelCount() * 5);
    }

    QObject::connect(audioTimer, &QTimer::timeout, [&]() {
        if (!audioInput) return;

        // 源格式字节宽度
        int srcBytesPerSample = 2; // 默认 Int16
        switch (micFormat.sampleFormat()) {
            case QAudioFormat::UInt8: srcBytesPerSample = 1; break;
            case QAudioFormat::Int16: srcBytesPerSample = 2; break;
            case QAudioFormat::Int32: srcBytesPerSample = 4; break;
            case QAudioFormat::Float: srcBytesPerSample = 4; break;
            default: srcBytesPerSample = 2; break;
        }

        const int srcChannels = micFormat.channelCount();
        // 计算当前需要读取多少源数据才能凑够一个 Opus 帧 (20ms)
        const int srcFrameSamples = micFormat.sampleRate() / 50; 
        const int needBytes = srcFrameSamples * srcChannels * srcBytesPerSample;
        
        static int noDataCount = 0;
        
        // 检查音频源状态
        // 允许 ActiveState 和 IdleState (Idle 可能只是暂时没数据)
        bool isRunning = (audioSource && (audioSource->state() == QAudio::ActiveState || audioSource->state() == QAudio::IdleState));
        
        if (!isRunning) {
             static int errorLogCount = 0;
             if (++errorLogCount % 100 == 0) {
                 qDebug() << "[Audio] Audio source not running. State:" << (audioSource ? audioSource->state() : -1) 
                          << " Error:" << (audioSource ? audioSource->error() : -1);
                 if (audioSource && audioSource->error() == QAudio::OpenError) {
                     qDebug() << "[Audio] CRITICAL: Failed to open microphone! Possible permission denied or device occupied.";
                 }
             }
             // 尝试恢复
             if (audioSource && audioSource->state() == QAudio::StoppedState) {
                 audioInput = audioSource->start();
             }
             // 不要直接返回，允许进入下方的测试音注入逻辑
        }

        // 读取所有可用数据到累积缓冲区
        QByteArray incoming;
        if (isRunning && audioInput) {
            incoming = audioInput->readAll();
        }

        if (!incoming.isEmpty()) {
            // 调试日志：验证麦克风是否真的采集到了数据
            static int micDataLogCount = 0;
            if (++micDataLogCount % 20 == 0) {
                 qDebug() << "[Audio] Microphone captured bytes:" << incoming.size() 
                          << "Accumulated:" << pcmAccum.size();
            }
            
            pcmAccum.append(incoming);
            noDataCount = 0;
        } else {
            noDataCount++;
            if (noDataCount % 100 == 0) {
                 qDebug() << "[Audio] No data available for" << (noDataCount * 20 / 1000) << "seconds. State:" << (audioSource ? audioSource->state() : -1);
            }

            // 自动重启逻辑: 如果超过 10 秒无数据，尝试重启音频源
            if (noDataCount > 500) {
                 static int restartCount = 0;
                 if (++restartCount % 50 == 0) {
                     qDebug() << "[Audio] No data for 10s. Restarting audio source...";
                     if (audioSource) {
                         audioSource->stop();
                         audioInput = audioSource->start();
                     }
                 }
            }

            // 尝试 13: 注入测试音 (当麦克风无数据超过 5 秒)
            if (noDataCount > 250) { 
                static int tonePhase = 0;
                static int toneLogCount = 0;
                if (++toneLogCount % 50 == 0) {
                    qDebug() << "[Audio] Injecting 440Hz test tone (Mic dead/muted)...";
                }
                
                // 生成 20ms 的 440Hz 正弦波
                int samples = micFormat.sampleRate() / 50; 
                QByteArray toneBytes;
                toneBytes.resize(samples * srcChannels * srcBytesPerSample);
                
                // 简单的 PCM 生成 (仅支持 Int16 和 Float，其他格式忽略)
                if (micFormat.sampleFormat() == QAudioFormat::Int16) {
                    int16_t *ptr = reinterpret_cast<int16_t*>(toneBytes.data());
                    for (int i = 0; i < samples; ++i) {
                        double t = static_cast<double>(tonePhase++) / micFormat.sampleRate();
                        int16_t val = static_cast<int16_t>(10000.0 * std::sin(2.0 * 3.1415926535 * 440.0 * t)); // 音量调小一点
                        for (int c = 0; c < srcChannels; ++c) {
                            ptr[i * srcChannels + c] = val;
                        }
                    }
                    pcmAccum.append(toneBytes);
                } else if (micFormat.sampleFormat() == QAudioFormat::Float) {
                    float *ptr = reinterpret_cast<float*>(toneBytes.data());
                    for (int i = 0; i < samples; ++i) {
                        double t = static_cast<double>(tonePhase++) / micFormat.sampleRate();
                        float val = static_cast<float>(0.3 * std::sin(2.0 * 3.1415926535 * 440.0 * t));
                        for (int c = 0; c < srcChannels; ++c) {
                            ptr[i * srcChannels + c] = val;
                        }
                    }
                    pcmAccum.append(toneBytes);
                }
            }
        }

        // 处理累积缓冲区中的完整帧
        while (pcmAccum.size() >= needBytes) {
            // 日志输出读取到的字节数 (每100次输出一次)
            static int readLogCount = 0;
            if (++readLogCount % 100 == 0) {
                 qDebug() << "[Audio] Buffer size:" << pcmAccum.size() << " Need:" << needBytes;
            }

            // 提取一帧数据
            QByteArray pcmSrc = pcmAccum.left(needBytes);
            pcmAccum.remove(0, needBytes);

            // 第一步：统一转换为 Int16 单声道
            // 此时采样率仍为 micFormat.sampleRate()
            QVector<int16_t> tempPcm(srcFrameSamples);
            int16_t *dst = tempPcm.data();

            if (micFormat.sampleFormat() == QAudioFormat::Int16) {
                // 源就是 Int16，按通道取第一个声道
                const int16_t *src = reinterpret_cast<const int16_t*>(pcmSrc.constData());
                for (int i = 0; i < srcFrameSamples; ++i) {
                    dst[i] = src[i * srcChannels];
                }
            } else if (micFormat.sampleFormat() == QAudioFormat::Float) {
                const float *src = reinterpret_cast<const float*>(pcmSrc.constData());
                for (int i = 0; i < srcFrameSamples; ++i) {
                    float s = src[i * srcChannels];
                    if (s > 1.0f) s = 1.0f; if (s < -1.0f) s = -1.0f;
                    dst[i] = static_cast<int16_t>(s * 32767.0f);
                }
            } else if (micFormat.sampleFormat() == QAudioFormat::UInt8) {
                const uint8_t *src = reinterpret_cast<const uint8_t*>(pcmSrc.constData());
                for (int i = 0; i < srcFrameSamples; ++i) {
                    int v = static_cast<int>(src[i * srcChannels]) - 128;
                    dst[i] = static_cast<int16_t>(v << 8);
                }
            } else if (micFormat.sampleFormat() == QAudioFormat::Int32) {
                const int32_t *src = reinterpret_cast<const int32_t*>(pcmSrc.constData());
                for (int i = 0; i < srcFrameSamples; ++i) {
                    dst[i] = static_cast<int16_t>(src[i * srcChannels] >> 16);
                }
            } else {
                // 未知格式，直接丢弃本次
                continue;
            }

            // 第二步：重采样 (如果需要)
            // 从 srcFrameSamples (audioSampleRate) -> opusFrameSize (opusSampleRate)
            QByteArray finalPcm;
            finalPcm.resize(opusFrameSize * sizeof(int16_t));
            int16_t *finalDst = reinterpret_cast<int16_t*>(finalPcm.data());

            if (audioSampleRate == opusSampleRate) {
                // 无需重采样
                memcpy(finalDst, dst, opusFrameSize * sizeof(int16_t));
            } else {
                // 简单线性插值重采样
                double ratio = static_cast<double>(srcFrameSamples) / static_cast<double>(opusFrameSize);
                for (int i = 0; i < opusFrameSize; ++i) {
                    double pos = i * ratio;
                    int idx = static_cast<int>(pos);
                    if (idx >= srcFrameSamples - 1) {
                        finalDst[i] = dst[srcFrameSamples - 1];
                    } else {
                        double frac = pos - idx;
                        finalDst[i] = static_cast<int16_t>(dst[idx] * (1.0 - frac) + dst[idx + 1] * frac);
                    }
                }
            }

            auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();

            if (!isCapturing) {
                continue; // 未开始推流时不发送音频
            }

            if (!opusEnc) {
                continue; // 编码器未初始化
            }

            // Opus 编码（20ms 帧）
            QByteArray opusOut;
            opusOut.resize(4096); // 足够容纳单帧 Opus 数据
            int nbytes = opus_encode(opusEnc,
                                     reinterpret_cast<const opus_int16*>(finalDst),
                                     opusFrameSize,
                                     reinterpret_cast<unsigned char*>(opusOut.data()),
                                     opusOut.size());
            if (nbytes < 0) {
                // 编码失败，跳过本帧
                qDebug() << "[Audio] Opus encode failed:" << nbytes;
                continue;
            }
            opusOut.resize(nbytes);

            QJsonObject msg;
            msg["type"] = "audio_opus";
            msg["sample_rate"] = opusSampleRate;
            msg["channels"] = 1; // 单声道
            msg["timestamp"] = static_cast<qint64>(timestamp);
            msg["frame_samples"] = opusFrameSize; // 每帧采样数（20ms）
            static quint32 audioSeq = 0;
            msg["seq"] = static_cast<qint64>(audioSeq++);
            msg["data_base64"] = QString::fromUtf8(opusOut.toBase64());
            
            QJsonDocument doc(msg);
            staticSender->sendTextMessage(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
            audioFrameSendCount++;
            if (audioFrameSendCount % 100 == 0) {
                qDebug() << "[Audio] Sent Opus frame #" << audioFrameSendCount << " Bytes:" << nbytes;
            }
        }
    });

    // 新增：质量应用方法
    auto applyQualitySetting = [&](const QString &qualityRaw) {
        QString q = qualityRaw.toLower();
        if (q != "low" && q != "medium" && q != "high" && q != "extreme") {
            q = "medium";
        }
        currentQuality = q;

        // 目标编码分辨率计算（低质720p，保持纵横比，不上采样）
        QSize orig = staticCapture->getScreenSize();
        QSize desired = orig;
        if (q == "low") {
            double fw = 1280.0 / orig.width();
            double fh = 720.0 / orig.height();
            double factor = std::min(fw, fh);
            if (factor < 1.0) {
                desired = QSize(qRound(orig.width() * factor), qRound(orig.height() * factor));
            } else {
                desired = orig;
            }
        } else {
            desired = orig;
        }

        // 全局限制：无论什么画质，最大长边不超过 1920
        if (desired.width() > 1920 || desired.height() > 1920) {
            double ratioW = 1920.0 / desired.width();
            double ratioH = 1920.0 / desired.height();
            double ratio = std::min(ratioW, ratioH);
            
            desired.setWidth(qRound(desired.width() * ratio));
            desired.setHeight(qRound(desired.height() * ratio));
        }
        
        // 确保偶数宽高
        if (desired.width() % 2 != 0) desired.setWidth(desired.width() - 1);
        if (desired.height() % 2 != 0) desired.setHeight(desired.height() - 1);

        staticEncoder->setQualityPreset(q);
        staticEncoder->cleanup();
        if (!staticEncoder->initialize(desired.width(), desired.height(), staticEncoder->getFrameRate())) {
            return; // 保持旧状态以避免崩溃
        }
        targetEncodeSize = staticEncoder->getFrameSize();
        
        // 更新鼠标缩放比例（同时更新屏幕区域）
        QRect qScreenRect;
        int qScreenIdx = currentScreenIndex;
        if (qScreenIdx >= 0 && qScreenIdx < QApplication::screens().size()) {
            QScreen *scr = QApplication::screens().at(qScreenIdx);
            qreal dpr = scr->devicePixelRatio();
            QRect geo = scr->geometry();
            qScreenRect = QRect(qRound(geo.x() * dpr), qRound(geo.y() * dpr), 
                                qRound(geo.width() * dpr), qRound(geo.height() * dpr));
        } else {
            qScreenRect = QRect(0, 0, orig.width(), orig.height());
        }
        
        staticMouseCapture->setScreenRect(qScreenRect, targetEncodeSize);

        // 按质量调整码率与静态内容降码策略
        // 全局启用静态帧跳过以实现最低流量
        staticEncoder->setSkipStaticFrames(true);

        if (q == "low") {
            staticEncoder->setBitrate(200000);
            staticEncoder->setEnableStaticDetection(true);
            staticEncoder->setStaticBitrateReduction(0.05);
            staticEncoder->setStaticThreshold(0.0001); // 降低阈值以检测微小变化
        } else if (q == "medium") {
            staticEncoder->setBitrate(400000);
            staticEncoder->setEnableStaticDetection(true);
            staticEncoder->setStaticBitrateReduction(0.10);
            staticEncoder->setStaticThreshold(0.0001); // 降低阈值以检测微小变化
        } else if (q == "high") {
            staticEncoder->setBitrate(500000);
            staticEncoder->setEnableStaticDetection(true);
            staticEncoder->setStaticBitrateReduction(0.15);
            staticEncoder->setStaticThreshold(0.0001); // 降低阈值以检测微小变化
        } else if (q == "extreme") {
            staticEncoder->setBitrate(3000000);
            staticEncoder->setEnableStaticDetection(true);
            staticEncoder->setStaticBitrateReduction(0.20);
            staticEncoder->setStaticThreshold(0.0001); // 降低阈值以检测微小变化
        }

        // 强制关键帧以快速稳定画面
        staticEncoder->forceKeyFrame();

        
    };

    // 启动时应用默认质量（来自配置）
    applyQualitySetting(currentQuality);
    
    // 安全启动定时器：如果连接成功后3秒仍未收到start_streaming信号，则强制启动
    // [Privacy Change] Disabled safety auto-start.
    // The process must remain IDLE until explicitly requested by a viewer.
    // This prevents "Ghost Streaming" when the process restarts automatically.
    QTimer *safetyStartTimer = new QTimer(&app);
    safetyStartTimer->setInterval(3000);
    safetyStartTimer->setSingleShot(true);
    
    QObject::connect(sender, &WebSocketSender::connected, [safetyStartTimer]() {
        // qDebug() << "[CaptureProcess] Connected to server. Starting safety timer (3s)...";
        // safetyStartTimer->start(); 
        qDebug() << "[CaptureProcess] Connected to server. Standing by for viewer request.";
    });
    
    QObject::connect(safetyStartTimer, &QTimer::timeout, [&, sender]() {
        // Disabled logic
    });



    // 定义音频启动/停止函数
    auto startAudio = [&]() {
        // 使用实际协商的采样率，而不是硬编码 16000
        // opusSampleRate 在 main 中已根据 micFormat 初始化
        qDebug() << "[Audio] startAudio called. SampleRate:" << opusSampleRate << " FrameSize:" << opusFrameSize;
        
        if (!opusEnc) {
            int err = OPUS_OK;
            // Opus 仅支持 8000, 12000, 16000, 24000, 48000
            opusEnc = opus_encoder_create(opusSampleRate, 1, OPUS_APPLICATION_VOIP, &err);
            if (err == OPUS_OK && opusEnc) {
                opus_encoder_ctl(opusEnc, OPUS_SET_BITRATE(24000));
                opus_encoder_ctl(opusEnc, OPUS_SET_VBR(1));
                opus_encoder_ctl(opusEnc, OPUS_SET_COMPLEXITY(5));
                opus_encoder_ctl(opusEnc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                opus_encoder_ctl(opusEnc, OPUS_SET_INBAND_FEC(1));
                qDebug() << "[Audio] Opus encoder created successfully.";
            } else {
                qDebug() << "[Audio] Opus encoder creation failed. Error:" << err;
            }
        }
        // 回退到麦克风采集
        if (audioSource) {
            // 安全重置 audioInput
            if (audioInput) {
                 audioSource->stop();
                 audioInput = nullptr; 
            }
            audioInput = audioSource->start();
            if (!audioInput) {
                qDebug() << "[Audio] Failed to start audio source! Error:" << audioSource->error();
            } else {
                audioSource->setVolume(currentMicGainPercent / 100.0);
                qDebug() << "[Audio] Microphone started. AudioInput:" << audioInput << " State:" << audioSource->state();
            }
        } else {
            qDebug() << "[Audio] AudioSource is null!";
        }
        if (!audioTimer->isActive()) {
            audioTimer->start();
            qDebug() << "[Audio] Audio timer started.";
        }
    };

    auto stopAudio = [&]() {
        audioTimer->stop();
        qDebug() << "[Audio] Audio timer stopped.";
        if (audioSource) { 
            audioSource->stop(); 
            audioInput = nullptr; 
            qDebug() << "[Audio] Audio source stopped.";
        }
        if (mp3Decoder) { mp3Decoder->stop(); }
        if (opusEnc) { opus_encoder_destroy(opusEnc); opusEnc = nullptr; }
        // 注意：不要停止远程接收（remoteSink），因为这会导致消费者说话生产者听不到
        // if (remoteSink) { remoteSink->stop(); delete remoteSink; remoteSink = nullptr; remoteOutIO = nullptr; }
        // if (remoteOpusDec) { opus_decoder_destroy(remoteOpusDec); remoteOpusDec = nullptr; }
    };

    // 状态监控定时器：每3秒输出一次音频发送状态
    QTimer *statusTimer = new QTimer(&app);
    QObject::connect(statusTimer, &QTimer::timeout, [&]() {
        if (isCapturing) {
            qDebug() << "[CaptureProcess] Status - Capturing: Yes | Audio Frames Sent:" << audioFrameSendCount 
                     << "| Audio Source State:" << (audioSource ? audioSource->state() : -1)
                     << "| Opus Enc:" << (opusEnc ? "Ready" : "Null")
                     << "| Audio Buffer:" << pcmAccum.size()
                     << "| Audio Input:" << (audioInput ? "Valid" : "Null")
                     << "| Bytes Avail:" << (audioInput ? audioInput->bytesAvailable() : -1)
                     << "| Sender Connected:" << sender->isConnected();
                     
            // 自动故障恢复：如果音频源停止了，或者长时间没有发送数据，尝试重启
            bool needsRestart = false;
            if (audioSource && audioSource->state() == QAudio::StoppedState) {
                 qDebug() << "[CaptureProcess] Audio source stopped unexpectedly.";
                 needsRestart = true;
            }
            if (!opusEnc) {
                 qDebug() << "[CaptureProcess] Opus encoder missing.";
                 needsRestart = true;
            }
            
            // 检查是否长时间没有发送音频帧 (例如超过5秒)
            static int lastFrameCount = 0;
            static int noFrameTimer = 0;
            if (audioFrameSendCount == lastFrameCount) {
                noFrameTimer++;
                if (noFrameTimer >= 2) { // 2 * 3s = 6秒无数据
                    qDebug() << "[CaptureProcess] No audio frames sent for 6 seconds. Audio stuck?";
                    // 尝试重启音频
                    needsRestart = true;
                }
            } else {
                noFrameTimer = 0;
                lastFrameCount = audioFrameSendCount;
            }

            if (needsRestart && remoteAudioEnabled) {
                 qDebug() << "[CaptureProcess] Attempting audio restart...";
                 stopAudio();
                 QTimer::singleShot(100, [&](){ startAudio(); });
                 // 重置计数器防止循环重启
                 noFrameTimer = 0; 
            }
        } else {
            qDebug() << "[CaptureProcess] Status - Capturing: No | Connected:" << sender->isConnected()
                     << "| Waiting for start_streaming signal...";
        }
        
        // [Fix] Keep-Alive UI Overlay
        // Every 3 seconds, if we are capturing, force the overlay windows to stay on top.
        // This combats the "drawing not visible" issue after long sessions.
        if (isCapturing) {
            for (auto *ov : s_overlays) {
                 if (ov) {
                     ov->raise();
                     // Check if window handle is still valid (primitive check)
                     if (!ov->isVisible()) ov->show();
                 }
            }
        }
        
        // [Fix] Zombie Process Self-Termination
        // If capture is active but we haven't sent any video frames for 60 seconds (frameCount stuck),
        // we assume the capture loop is dead or graphics driver is hung.
        // We exit, letting the Watchdog restart us.
        static int lastVideoFrameCount = 0;
        static int stuckVideoCounter = 0;
        if (isCapturing) {
            if (frameCount == lastVideoFrameCount) {
                stuckVideoCounter++;
                if (stuckVideoCounter >= 20) { // 20 * 3s = 60s
                    qDebug() << "[CaptureProcess] CRITICAL: Video stuck for 60s! Self-terminating to force restart.";
                    app.quit();
                }
            } else {
                stuckVideoCounter = 0;
                lastVideoFrameCount = frameCount;
            }
        }
    });
    statusTimer->start(3000); // 3秒一次

    QObject::connect(sender, &WebSocketSender::annotationEventReceived,
                     [&](const QString &phase, int x, int y, const QString &viewerId, int colorId) {
        // [Debug] Log annotation events to diagnose "cannot draw" issues
        if (phase != "move") { 
             qDebug() << "[CaptureProcess] Annotation received:" << phase << "from" << viewerId << "at" << x << "," << y;
        }

        int idx = currentScreenIndex;
        if (phase == QStringLiteral("clear")) {
            for (auto *ov : s_overlays) { if (ov) ov->clear(); }
            for (auto *cv : s_cursorOverlays) { if (cv) cv->clear(); }
            return;
        }
        if (idx >= 0 && idx < s_overlays.size()) {
            // 直接使用 Overlay Widget 的逻辑尺寸进行映射
            // 这避免了物理分辨率/DPR换算的复杂性和潜在错误 (如 4K 屏双倍映射问题)
            QSize logicalSize = s_overlays[idx]->size();
            QSize enc = targetEncodeSize;
            
            int sx = enc.width() > 0 ? qRound(double(x) * double(logicalSize.width()) / double(enc.width())) : x;
            int sy = enc.height() > 0 ? qRound(double(y) * double(logicalSize.height()) / double(enc.height())) : y;
            
            s_overlays[idx]->onAnnotationEvent(phase, sx, sy, viewerId, colorId);
            
            // [Fix] Force overlay to top when drawing happens
            s_overlays[idx]->raise();
            s_overlays[idx]->show(); 
        }
    });
    QObject::connect(sender, &WebSocketSender::textAnnotationReceived,
                     [&](const QString &text, int x, int y, const QString &viewerId, int colorId, int fontSize) {
        int idx = currentScreenIndex;
        if (idx >= 0 && idx < s_overlays.size()) {
            QSize logicalSize = s_overlays[idx]->size();
            QSize enc = targetEncodeSize;
            
            int sx = enc.width() > 0 ? qRound(double(x) * double(logicalSize.width()) / double(enc.width())) : x;
            int sy = enc.height() > 0 ? qRound(double(y) * double(logicalSize.height()) / double(enc.height())) : y;
            
            s_overlays[idx]->onTextAnnotation(text, sx, sy, viewerId, colorId, fontSize);
        }
    });
    QObject::connect(sender, &WebSocketSender::likeRequested,
                     [&](const QString &viewerId) {
        int idx = currentScreenIndex;
        if (idx >= 0 && idx < s_overlays.size()) {
            s_overlays[idx]->onLikeRequested(viewerId);
        }
    });

    
    const auto screens = QApplication::screens();
    int screenIndex = getScreenIndexFromConfig();
    QScreen *targetScreen = nullptr;
    if (screenIndex >= 0 && screenIndex < screens.size()) {
        targetScreen = screens[screenIndex];
    } else {
        targetScreen = QApplication::primaryScreen();
    }

    // 列出所有可用音频输入设备
    const auto availableDevices = QMediaDevices::audioInputs();
    qDebug() << "[Audio] Available input devices:";
    for (const auto &dev : availableDevices) {
        qDebug() << "  -" << dev.description();
    }

    // 连接推流控制信号
    QObject::connect(sender, &WebSocketSender::streamingStarted, [&, startAudio, applyQualitySetting]() {
        qDebug() << "[CaptureProcess] Streaming started signal received. isCapturing:" << isCapturing;
        if (!isCapturing) {
            // [Fix] 重新应用画质设置，确保编码器分辨率与 targetEncodeSize 一致
            // 避免 streamingStopped 重置为全分辨率后导致的鼠标映射错误
            applyQualitySetting(currentQuality);
            qDebug() << "[CaptureProcess] Quality applied on start. TargetEncodeSize:" << targetEncodeSize;

            isCapturing = true;
            // 降低帧率以应对多人观看：从33ms(30fps)调整为66ms(15fps)
            // 这是一个非常稳妥的折中值，既能保证流畅度，又能将流量和CPU减半
            captureTimer->start(66); 
            staticMouseCapture->startCapture(); // 开始鼠标捕获
            if (currentScreenIndex >= 0 && currentScreenIndex < s_overlays.size()) {
                s_overlays[currentScreenIndex]->raise();
                s_cursorOverlays[currentScreenIndex]->raise();
            }
        }
        
        // 无论是否已经在捕获，只要收到推流信号，都强制检查并启动音频
        // 这是为了防止 isCapturing 为 true 但音频未启动的情况 (例如之前的故障恢复)
        if (remoteAudioEnabled) {
            qDebug() << "[CaptureProcess] Enforcing audio start on streamingStarted...";
            startAudio();
        }
    });
    
    QObject::connect(sender, &WebSocketSender::streamingStopped, [captureTimer, audioTimer, audioSource, &audioInput]() {
        if (isCapturing) {
            isCapturing = false;
            captureTimer->stop();
            staticMouseCapture->stopCapture(); // 停止鼠标捕获
            // qDebug() << "[CaptureProcess] 停止屏幕捕获和鼠标捕获";
            audioTimer->stop();
            if (audioSource) {
                audioSource->stop();
                audioInput = nullptr;
            }
            if (opusEnc) { opus_encoder_destroy(opusEnc); opusEnc = nullptr; }
            if (mp3Decoder) { mp3Decoder->stop(); }
            
            // 重置捕获和编码器状态，防止下次连接时出现残留问题（如黑屏）
            if (staticCapture) {
                staticCapture->cleanup();
                staticCapture->initialize();
            }
            if (staticEncoder) {
                // 编码器也重置一下比较安全
                staticEncoder->cleanup();
                QSize capSize = staticCapture->getScreenSize();
                staticEncoder->initialize(capSize.width(), capSize.height(), staticEncoder->getFrameRate());
            }
        }
    });

    // 响应关键帧请求（带2秒冷却保护）
    QObject::connect(sender, &WebSocketSender::requestKeyFrame, []() {
        static qint64 lastKeyFrameRequestTime = 0;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        // 2秒内忽略重复请求，防止多人进场导致的关键帧风暴
        if (now - lastKeyFrameRequestTime < 2000) {
            return;
        }
        lastKeyFrameRequestTime = now;
        
        if (staticEncoder) {
            staticEncoder->forceKeyFrame();
        }
    });

    // 监听观众数量变化，动态调整帧率 (1人:20fps, 2人:15fps, 3人+:10fps)
    static int currentViewerCount = 0;
    // 需要WebSocketSender暴露观众进出信号
    // 由于WebSocketSender目前没有直接暴露count，我们通过事件来估算或修改sender
    // 这里我们先假设通过viewerCursor/audio等事件能感知活跃用户，或者直接修改WebSocketSender增加viewerCount信号
    
    // 简单实现：每次有新观众请求关键帧或进入时，我们暂时无法获得确切总人数
    // 但为了响应您的需求，我们可以做一个保守的动态策略：
    // 默认启动时设为 20 FPS (50ms)
    // 后面根据网络拥堵情况调整可能更准确，但按人数调整最直观。
    
    // 既然目前无法直接获取准确人数，我们先实现 "默认15fps + 关键帧限流" 这一最稳妥方案。
    // 动态调整需要服务端配合下发人数通知。
    
    // 将系统全局鼠标坐标转换为当前捕获屏幕的局部坐标后发送到观看端
    QObject::connect(sender, &WebSocketSender::viewerNameChanged, [&](const QString &){ });
    QObject::connect(sender, &WebSocketSender::viewerCursorReceived,
                     [&](const QString &viewerId, int x, int y, const QString &viewerName) {
        int idx = currentScreenIndex;
        if (idx >= 0 && idx < s_cursorOverlays.size()) {
            // 直接使用 Overlay Widget 的逻辑尺寸进行映射
            // 避免了依赖 QApplication::screens() 索引一致性和 Fallback 到物理尺寸的风险
            QSize logicalSize = s_cursorOverlays[idx]->size();
            QSize enc = targetEncodeSize;

            int sx = enc.width() > 0 ? qRound(double(x) * double(logicalSize.width()) / double(enc.width())) : x;
            int sy = enc.height() > 0 ? qRound(double(y) * double(logicalSize.height()) / double(enc.height())) : y;
            
            // static int logCount = 0;
            // if (++logCount % 60 == 0) {
            //      qDebug() << "[CursorMap] In(" << x << "," << y << ") "
            //               << "LogSize:" << logicalSize << " EncSize:" << enc 
            //               << " -> Out(" << sx << "," << sy << ")";
            // }

            s_cursorOverlays[idx]->onViewerCursor(viewerId, sx, sy, viewerName);
        }
    });
    QObject::connect(sender, &WebSocketSender::viewerNameUpdateReceived,
                     [&](const QString &viewerId, const QString &viewerName) {
        int idx = currentScreenIndex;
        if (idx >= 0 && idx < s_cursorOverlays.size()) {
            s_cursorOverlays[idx]->onViewerNameUpdate(viewerId, viewerName);
        }
    });

    QObject::connect(sender, &WebSocketSender::watchRequestReceived,
                     [&](const QString &viewerId, const QString &viewerName, const QString &targetId, int iconId) {
        
        // 检查配置是否启用手动审批
        auto isManualApprovalEnabledFromConfig = []() -> bool {
            QStringList paths;
            paths << QCoreApplication::applicationDirPath() + "/config/app_config.txt";
            paths << QDir::currentPath() + "/config/app_config.txt";
            for (const QString &p : paths) {
                QFile f(p);
                if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QTextStream in(&f);
                    while (!in.atEnd()) {
                        QString line = in.readLine();
                        if (line.startsWith("manual_approval_enabled=")) {
                            QString v = line.mid(QString("manual_approval_enabled=").length()).trimmed();
                            f.close();
                            return v.compare("true", Qt::CaseInsensitive) == 0 || v == "1";
                        }
                    }
                    f.close();
                }
            }
            // 默认开启手动同意，与MainWindow保持一致
            return true;
        };

        if (isManualApprovalEnabledFromConfig()) {
            // 手动模式：主程序 MainWindow 负责弹出对话框并发送 accepted 消息
            // CaptureProcess 等待收到 watch_request_accepted 消息后再开始推流
            qDebug() << "[CaptureProcess] Manual approval mode. Waiting for watch_request_accepted signal.";
            // 移除自动开始推流，等待明确的同意指令
        } else {
            // 自动模式：直接通过
            qDebug() << "[CaptureProcess] Auto-approving watch request (Auto mode)";
            sender->approveWatchRequest();
        }
    });

    QObject::connect(sender, &WebSocketSender::viewerExited,
                     [&](const QString &viewerId) {
        for (auto *cv : s_cursorOverlays) {
            if (cv) cv->onViewerExited(viewerId);
        }
        for (auto *ov : s_overlays) {
            if (ov) ov->onViewerExited(viewerId);
        }

        if (watchdog) {
            watchdog->notifyViewerExited(viewerId);
        }
        
        // [Security] User disconnected, stop streaming immediately to prevent zombie streaming
        // This assumes 1-on-1 session or that we want to stop when ANY viewer exits (conservative)
        // Ideally we should check if viewerCount == 0, but since we don't have count sync,
        // stopping here is the safest bet to prevent "secret recording".
        // The next viewer will trigger start_streaming again.
        qDebug() << "[CaptureProcess] Viewer exited:" << viewerId << "-> Stopping stream for privacy/performance.";
        
        // Trigger stop logic
        if (isCapturing) {
            isCapturing = false;
            captureTimer->stop();
            staticMouseCapture->stopCapture();
            audioTimer->stop();
            if (audioSource) {
                audioSource->stop();
                audioInput = nullptr;
            }
            // Reset encoders to free resources
            if (staticCapture) staticCapture->cleanup();
            if (staticEncoder) staticEncoder->cleanup();
            
            // Notify server we stopped (optional, but good practice)
            // sender->sendStreamingStopped(); 
        }
    });

    QObject::connect(staticMouseCapture, &MouseCapture::mousePositionChanged, sender,
                     [sender](const QPoint &globalPos) {
        const auto screens = QApplication::screens();
        if (screens.isEmpty()) {
            return;
        }
        int idx = currentScreenIndex;
        if (idx < 0 || idx >= screens.size()) {
            idx = 0;
        }
        QScreen *screen = screens[idx];
        QRect geom = screen->geometry();
        QPoint local(globalPos.x() - geom.x(), globalPos.y() - geom.y());
        if (local.x() < 0 || local.y() < 0 || local.x() >= geom.width() || local.y() >= geom.height()) {
            // 不在当前屏幕范围内则忽略，避免错误叠加
            return;
        }

        QJsonObject messageObj;
        messageObj["type"] = "mouse_position";
        messageObj["x"] = local.x();
        messageObj["y"] = local.y();
        if (!currentUserName.isEmpty()) {
            messageObj["name"] = currentUserName;
        }
        // 使用微秒时间戳，与采集端原实现一致
        auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        messageObj["timestamp"] = static_cast<qint64>(timestamp);
        QJsonDocument doc(messageObj);
        sender->sendTextMessage(doc.toJson(QJsonDocument::Compact));
    });

    

    // 处理观看端切换屏幕请求：滚动切换到下一屏幕
    QObject::connect(sender, &WebSocketSender::switchScreenRequested, 
                     [&](const QString &direction, int targetIndex) {
        int finalIndex = -1;
        if (direction == "index") {
            finalIndex = targetIndex;
        }
        handleScreenSwitch(finalIndex);
    });

    // 处理本地切换屏幕请求
    if (watchdog) {
        QObject::connect(watchdog, &WatchdogClient::switchScreenRequested, 
                         [&](int index) {
            handleScreenSwitch(index);
        });
    }

    // 本地麦克风开关（由主进程通过 Watchdog 下发）
    if (watchdog) {
        QObject::connect(watchdog, &WatchdogClient::audioToggleRequested, [&, startAudio, stopAudio](bool enabled) {
            localMicEnabled = enabled;
            const bool finalEnabled = serverAudioWanted && localMicEnabled;
            remoteAudioEnabled = finalEnabled;
            if (finalEnabled) {
                if (!sender->isStreaming()) return;
                startAudio();
            } else {
                stopAudio();
            }
        });
    }

    // 新增：处理质量变更请求
    QObject::connect(sender, &WebSocketSender::qualityChangeRequested, [&](const QString &quality) {
        applyQualitySetting(quality);
    });

    // 新增：处理音频开关请求（麦克风采集）
    // 变量已移至上方作为静态变量声明
    QObject::connect(sender, &WebSocketSender::audioToggleRequested, [&, startAudio, stopAudio](bool enabled) {
        serverAudioWanted = enabled;
        const bool finalEnabled = enabled && localMicEnabled;
        remoteAudioEnabled = finalEnabled;
        if (finalEnabled) {
            if (!sender->isStreaming()) return;
            startAudio();
        } else {
            stopAudio();
        }
    });

    QObject::connect(sender, &WebSocketSender::audioGainRequested, [&, audioSource](int percent) {
        int p = percent;
        if (p < 0) p = 0;
        if (p > 100) p = 100;
        currentMicGainPercent = p;
        if (audioSource) {
            audioSource->setVolume(p / 100.0);
        }
    });

    // --- Audio Mixing Initialization ---
    QAudioFormat mixFmt;
    mixFmt.setSampleRate(48000); // Fixed mixing rate
    mixFmt.setChannelCount(1);
    mixFmt.setSampleFormat(QAudioFormat::Int16);
    
    mixSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), mixFmt, &app);
    mixSink->setBufferSize(16000);
    mixIO = mixSink->start();
    
    mixTimer = new QTimer(&app);
    mixTimer->setInterval(20); // 20ms mixing cycle
    
    QObject::connect(mixTimer, &QTimer::timeout, [&]() {
        QMutexLocker locker(&mixMutex);
        
        // Cleanup zombies (>30s inactive)
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        auto it = peerDecoders.begin();
        while (it != peerDecoders.end()) {
            QString vid = it.key();
            if (now - peerLastActiveTimes.value(vid, 0) > 30000) {
                if (peerMicOn.value(vid, false) && watchdog) {
                    watchdog->notifyViewerMicState(vid, false);
                }
                if (it.value()) opus_decoder_destroy(it.value());
                peerQueues.remove(vid);
                peerSilenceCounts.remove(vid);
                peerLastActiveTimes.remove(vid);
                peerBuffering.remove(vid);
                peerMicOn.remove(vid);
                it = peerDecoders.erase(it);
                qDebug() << "[AudioMixer] Removed zombie peer:" << vid;
            } else {
                ++it;
            }
        }

        const auto micKeys = peerMicOn.keys();
        for (const QString &vid : micKeys) {
            if (!peerMicOn.value(vid, false)) continue;
            qint64 last = peerLastActiveTimes.value(vid, 0);
            if (last <= 0 || (now - last) > 800) {
                peerMicOn[vid] = false;
                if (watchdog) {
                    watchdog->notifyViewerMicState(vid, false);
                }
            }
        }

        if (!remoteListenEnabled) return;
        
        if (peerDecoders.isEmpty()) return;

        // Prepare mix buffer
        int frameSamples = 48000 * 20 / 1000; // 960 samples for 20ms
        int mixSize = frameSamples * sizeof(opus_int16);
        if (mixBuffer.size() != mixSize) {
            mixBuffer.resize(mixSize);
        }
        std::memset(mixBuffer.data(), 0, mixSize);
        opus_int16 *mixPtr = reinterpret_cast<opus_int16*>(mixBuffer.data());
        
        bool anyAudio = false;
        
        for (auto it = peerDecoders.begin(); it != peerDecoders.end(); ++it) {
            QString vid = it.key();
            OpusDecoder *dec = it.value();
            QQueue<QByteArray> &q = peerQueues[vid];
            
            QByteArray opusData;
            bool isSilence = false;
            
            // Peer Anti-Jitter Logic
            bool isBuffering = peerBuffering.value(vid, true);
            if (isBuffering) {
                if (q.size() >= 12) { 
                    isBuffering = false;
                    peerBuffering[vid] = false;
                    qDebug() << "[AudioMixer] Peer" << vid << "buffering done. Queue:" << q.size();
                } else {
                    isSilence = true;
                }
            }

            if (!isSilence) {
                if (!q.isEmpty()) {
                    opusData = q.dequeue();
                    peerSilenceCounts[vid] = 0;
                } else {
                    // Underrun: Start buffering again
                    peerBuffering[vid] = true;
                    isSilence = true;
                    peerSilenceCounts[vid]++;
                    // qDebug() << "[AudioMixer] Peer" << vid << "underrun. Buffering...";
                }
            }
            
            if (isSilence) continue;
            
            QByteArray pcm(frameSamples * sizeof(opus_int16), 0);
            int decoded = opus_decode(dec, 
                opusData.isEmpty() ? nullptr : reinterpret_cast<const unsigned char*>(opusData.constData()),
                opusData.isEmpty() ? 0 : opusData.size(),
                reinterpret_cast<opus_int16*>(pcm.data()),
                frameSamples, 
                0); // FEC disabled for now
                
            if (decoded > 0) {
                const opus_int16 *src = reinterpret_cast<const opus_int16*>(pcm.constData());
                for (int i = 0; i < decoded; ++i) {
                    int s = mixPtr[i] + src[i];
                    if (s > 32767) s = 32767;
                    if (s < -32768) s = -32768;
                    mixPtr[i] = static_cast<opus_int16>(s);
                }
                anyAudio = true;
            }
        }
        
        if (anyAudio && mixIO) {
             if (mixSink->state() != QAudio::ActiveState) {
                 if (mixSink->state() == QAudio::StoppedState) mixIO = mixSink->start();
                 if (mixSink->state() == QAudio::SuspendedState) mixSink->resume();
             }
             mixIO->write(mixBuffer);
        }
    });
    
    mixTimer->start();

    QObject::connect(sender, &WebSocketSender::viewerAudioOpusReceived, [&](const QString &viewerId, const QByteArray &opus, int sr, int ch, int frameSamples, qint64 /*ts*/) {
        QMutexLocker locker(&mixMutex);
        QString vid = viewerId;
        if (vid.isEmpty()) vid = "unknown";
        
        if (!peerDecoders.contains(vid)) {
            int err;
            OpusDecoder *dec = opus_decoder_create(48000, 1, &err);
            if (err == OPUS_OK) {
                peerDecoders[vid] = dec;
                peerBuffering[vid] = true; // Initialize buffering
                qDebug() << "[AudioMixer] Added new peer:" << vid;
            } else {
                return;
            }
        }
        
        // 简单缓冲，如果堆积过多则丢弃旧帧 (Latency control)
        const int MAX_PEER_QUEUE = 24; 
        if (peerQueues[vid].size() >= MAX_PEER_QUEUE) {
             while (peerQueues[vid].size() >= MAX_PEER_QUEUE - 9) {
                 peerQueues[vid].dequeue(); 
             }
             qDebug() << "[AudioMixer] Peer" << vid << "latency high. Dropped frames. Queue:" << peerQueues[vid].size();
        }
        peerQueues[vid].enqueue(opus);
        peerLastActiveTimes[vid] = QDateTime::currentMSecsSinceEpoch();
        if (!peerMicOn.value(vid, false)) {
            peerMicOn[vid] = true;
            if (watchdog) {
                watchdog->notifyViewerMicState(vid, true);
            }
        }
    });

    QObject::connect(sender, &WebSocketSender::viewerListenMuteRequested, [&](bool mute) {
        remoteListenEnabled = !mute;
        if (mute) {
             QMutexLocker locker(&mixMutex);
             const auto keys = peerMicOn.keys();
             for (const QString &vid : keys) {
                 if (peerMicOn.value(vid, false) && watchdog) {
                     watchdog->notifyViewerMicState(vid, false);
                 }
             }
             for(auto dec : peerDecoders) opus_decoder_destroy(dec);
             peerDecoders.clear();
             peerQueues.clear();
             peerSilenceCounts.clear();
             peerLastActiveTimes.clear();
             peerMicOn.clear();
        }
    });
    
    QObject::connect(captureTimer, &QTimer::timeout, []() {
        if (!isCapturing) return; // 只有在推流状态下才捕获
        if (isSwitching) return;   // 热切换过程中不断流，但暂时不抓帧
        
        // 关键帧保活机制：每隔8秒强制发送一个关键帧
        // 这可以解决网络抖动导致的关键帧丢失，或观看端重连时的黑屏问题
        static qint64 lastKeyFrameTime = 0;
        const qint64 keyFrameInterval = 8000; // 8秒，避免流量过大
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        
        if (staticEncoder && (currentTime - lastKeyFrameTime > keyFrameInterval)) {
            staticEncoder->forceKeyFrame();
            lastKeyFrameTime = currentTime;
            // qDebug() << "[CaptureProcess] 触发保活关键帧";
        }

        auto captureStartTime = std::chrono::high_resolution_clock::now();
        QByteArray frameData = staticCapture->captureScreen();
        if (!frameData.isEmpty()) {
            auto captureEndTime = std::chrono::high_resolution_clock::now();
            auto captureLatency = std::chrono::duration_cast<std::chrono::microseconds>(captureEndTime - captureStartTime).count();
            frameCount++;
            // 只在延迟过高时输出警告
            if (captureLatency > 20000) { // 超过20ms时输出警告
            }
            
            // 如果编码目标分辨率与屏幕尺寸不同（例如低质720p），VP9Encoder会自动使用libyuv进行高效缩放
            // 移除了此前导致4K卡顿和异常的QImage::scaled操作
            const QSize capSize = staticCapture->getScreenSize();
            
            // 继续正常的VP9编码流程，传入实际捕获的尺寸
            // encode内部会根据初始化尺寸和输入尺寸自动判断是否需要缩放
            staticEncoder->encode(frameData, capSize.width(), capSize.height());
        }
    });
    
    
    
    
    // 连接到WebSocket服务器 - 使用推流URL格式
    // 移至此处以确保所有信号槽（特别是streamingStarted）都已连接
    QString deviceId = getDeviceIdFromConfig(); // 从配置文件读取设备ID
    QString serverAddress = getServerAddressFromConfig(); // 从配置文件读取服务器地址
    QString serverUrl = QString("ws://%1/publish/%2").arg(serverAddress, deviceId);
    
    // 显示设备ID
    qDebug() << "[CaptureProcess] Current Device ID:" << deviceId;
    qDebug() << "[CaptureProcess] Target Server URL:" << serverUrl;
    
    if (!sender->connectToServer(serverUrl)) {
        return -1;
    }
    // qDebug() << "[CaptureProcess] 正在连接到WebSocket服务器:" << serverUrl;

    // -------------------------------------------------------------------------
    // 本地审批逻辑 (Local Approval Logic)
    // -------------------------------------------------------------------------
    if (watchdog) {
        QObject::connect(watchdog, &WatchdogClient::approvalReceived, [&]() {
            qDebug() << "[CaptureProcess] Processing local approval...";
            // 本地收到同意指令，效果等同于收到服务器的 watch_request_accepted
            if (sender) {
                 if (sender->isStreaming()) {
                     sender->stopStreaming();
                 }
                 sender->startStreaming();
                 sender->forceKeyFrame();
                 qDebug() << "[CaptureProcess] Local approval executed: Streaming started.";
            }
        });
        
        QObject::connect(watchdog, &WatchdogClient::rejectionReceived, [&]() {
            qDebug() << "[CaptureProcess] Processing local rejection...";
            if (sender) {
                sender->rejectWatchRequest();
                qDebug() << "[CaptureProcess] Local rejection executed: Request reset.";
            }
        });
    }

    return app.exec();
}

#include "main_capture.moc"
