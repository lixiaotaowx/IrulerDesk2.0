#include <QtWidgets/QApplication>
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

int main(int argc, char *argv[])
{
    // 安装崩溃守护与控制台日志重定向
    CrashGuard::install();
    ConsoleLogger::attachToParentConsole();
    ConsoleLogger::installQtMessageHandler();

    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QCoreApplication::applicationDirPath() + "/maps/logo/iruler.ico"));
    
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
    
    VP9Encoder *encoder = new VP9Encoder(&app);
    // 保持现有帧率设置以降低编码负载（避免强制60fps）
    if (!encoder->initialize(actualScreenSize.width(), actualScreenSize.height(), encoder->getFrameRate())) {
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
    QObject::connect(encoder, &VP9Encoder::frameEncoded,
                     sender, &WebSocketSender::sendFrame);
    
    // 连接首帧关键帧策略信号槽
    QObject::connect(sender, &WebSocketSender::requestKeyFrame,
                     encoder, &VP9Encoder::forceKeyFrame);
    // qDebug() << "[CaptureProcess] [首帧策略] 已连接关键帧请求信号槽";
    
    // 创建鼠标捕获模块
    // qDebug() << "[CaptureProcess] 初始化鼠标捕获模块...";
    MouseCapture *mouseCapture = new MouseCapture(&app);
    
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
    static bool isSwitching = false; // 屏幕热切换中标志（不断流）
    // 新增：质量控制相关静态状态
    static QString currentQuality = getLocalQualityFromConfig();
    static QSize targetEncodeSize = staticEncoder->getFrameSize();

    // 新增：音频发送（改为麦克风采集，基于文本消息）
    QTimer *audioTimer = new QTimer(&app);
    audioTimer->setInterval(20); // 20ms 一帧
    static double audioPhase = 0.0;
    static int audioSampleRate = 16000; // 目标：16kHz 单声道 16bit PCM
    static const int audioChannels = 1;
    static const int audioBitsPerSample = 16;
    static bool remoteAudioEnabled = true; // 默认开启音频发送，不依赖观看端麦克风状态
    static int audioFrameSendCount = 0; // 发送帧计数
    
    // 远程音频播放相关变量
    // 多人混音支持
    static QMap<QString, OpusDecoder*> peerDecoders;
    static QMap<QString, QQueue<QByteArray>> peerQueues;
    static QMap<QString, int> peerSilenceCounts;
    static QMap<QString, qint64> peerLastActiveTimes;
    static QMap<QString, bool> peerBuffering; // New: Buffering state for jitter control
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

        staticEncoder->setQualityPreset(q);
        staticEncoder->cleanup();
        if (!staticEncoder->initialize(desired.width(), desired.height(), staticEncoder->getFrameRate())) {
            return; // 保持旧状态以避免崩溃
        }
        targetEncodeSize = staticEncoder->getFrameSize();

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
    // 这可以解决服务器未发送指令或指令丢失导致的问题
    QTimer *safetyStartTimer = new QTimer(&app);
    safetyStartTimer->setInterval(3000);
    safetyStartTimer->setSingleShot(true);
    
    QObject::connect(sender, &WebSocketSender::connected, [safetyStartTimer]() {
        qDebug() << "[CaptureProcess] Connected to server. Starting safety timer (3s)...";
        safetyStartTimer->start();
    });
    
    QObject::connect(safetyStartTimer, &QTimer::timeout, [&, sender]() {
        if (!isCapturing && sender->isConnected()) {
             qDebug() << "[CaptureProcess] Safety Timer Triggered: No start_streaming signal received in 3s. Force starting...";
             sender->startStreaming(); // 这将触发 streamingStarted 信号，进而调用 startAudio
        }
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
    });
    statusTimer->start(3000); // 3秒一次

    QObject::connect(sender, &WebSocketSender::annotationEventReceived,
                     [&](const QString &phase, int x, int y, const QString &viewerId, int colorId) {
        int idx = currentScreenIndex;
        if (phase == QStringLiteral("clear")) {
            for (auto *ov : s_overlays) { if (ov) ov->clear(); }
            for (auto *cv : s_cursorOverlays) { if (cv) cv->clear(); }
            return;
        }
        if (idx >= 0 && idx < s_overlays.size()) {
            QSize orig = staticCapture->getScreenSize();
            QSize enc = targetEncodeSize;
            int sx = enc.width() > 0 ? qRound(double(x) * double(orig.width()) / double(enc.width())) : x;
            int sy = enc.height() > 0 ? qRound(double(y) * double(orig.height()) / double(enc.height())) : y;
            s_overlays[idx]->onAnnotationEvent(phase, sx, sy, viewerId, colorId);
        }
    });
    QObject::connect(sender, &WebSocketSender::textAnnotationReceived,
                     [&](const QString &text, int x, int y, const QString &viewerId, int colorId, int fontSize) {
        int idx = currentScreenIndex;
        if (idx >= 0 && idx < s_overlays.size()) {
            QSize orig = staticCapture->getScreenSize();
            QSize enc = targetEncodeSize;
            int sx = enc.width() > 0 ? qRound(double(x) * double(orig.width()) / double(enc.width())) : x;
            int sy = enc.height() > 0 ? qRound(double(y) * double(orig.height()) / double(enc.height())) : y;
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
    QObject::connect(sender, &WebSocketSender::streamingStarted, [&, startAudio]() {
        qDebug() << "[CaptureProcess] Streaming started signal received. isCapturing:" << isCapturing;
        if (!isCapturing) {
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
            QSize orig = staticCapture->getScreenSize();
            QSize enc = targetEncodeSize;
            int sx = enc.width() > 0 ? qRound(double(x) * double(orig.width()) / double(enc.width())) : x;
            int sy = enc.height() > 0 ? qRound(double(y) * double(orig.height()) / double(enc.height())) : y;
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

    QObject::connect(sender, &WebSocketSender::viewerExited,
                     [&](const QString &viewerId) {
        for (auto *cv : s_cursorOverlays) {
            if (cv) cv->onViewerExited(viewerId);
        }
        for (auto *ov : s_overlays) {
            if (ov) ov->onViewerExited(viewerId);
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
        // 使用微秒时间戳，与采集端原实现一致
        auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        messageObj["timestamp"] = static_cast<qint64>(timestamp);
        QJsonDocument doc(messageObj);
        sender->sendTextMessage(doc.toJson(QJsonDocument::Compact));
    });

    

    // 处理观看端切换屏幕请求：滚动切换到下一屏幕
    QObject::connect(sender, &WebSocketSender::switchScreenRequested, [](const QString &direction, int targetIndex) {
        const auto screens = QApplication::screens();
        if (screens.isEmpty()) {
            return;
        }

        // 计算目标屏幕索引
        if (direction == "index" && targetIndex >= 0 && targetIndex < screens.size()) {
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

        // 重新初始化编码器以匹配新分辨率
        staticEncoder->cleanup();
        // 切屏后保持既有帧率，避免强制提升到60fps
        if (!staticEncoder->initialize(newSize.width(), newSize.height(), staticEncoder->getFrameRate())) {
            isSwitching = false;
            return;
        }
        staticEncoder->forceKeyFrame();

        
        if (currentScreenIndex >= 0 && currentScreenIndex < s_overlays.size()) {
            s_overlays[currentScreenIndex]->raise();
        }
        if (currentScreenIndex >= 0 && currentScreenIndex < s_cursorOverlays.size()) {
            s_cursorOverlays[currentScreenIndex]->raise();
        }

        // 切换完成，恢复捕获循环发帧
        isSwitching = false;
    });

    // 新增：处理质量变更请求
    QObject::connect(sender, &WebSocketSender::qualityChangeRequested, [&](const QString &quality) {
        applyQualitySetting(quality);
    });

    // 新增：处理音频开关请求（麦克风采集）
    // 变量已移至上方作为静态变量声明
    QObject::connect(sender, &WebSocketSender::audioToggleRequested, [&, startAudio, stopAudio](bool enabled) {
        remoteAudioEnabled = enabled;
        if (enabled) {
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
    mixSink->setBufferSize(4096); // ~85ms (reduced from 170ms)
    mixIO = mixSink->start();
    
    mixTimer = new QTimer(&app);
    mixTimer->setInterval(20); // 20ms mixing cycle
    
    QObject::connect(mixTimer, &QTimer::timeout, [&]() {
        if (!remoteListenEnabled) return;
        
        QMutexLocker locker(&mixMutex);
        
        // Cleanup zombies (>30s inactive)
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        auto it = peerDecoders.begin();
        while (it != peerDecoders.end()) {
            QString vid = it.key();
            if (now - peerLastActiveTimes.value(vid, 0) > 30000) {
                if (it.value()) opus_decoder_destroy(it.value());
                peerQueues.remove(vid);
                peerSilenceCounts.remove(vid);
                peerLastActiveTimes.remove(vid);
                peerBuffering.remove(vid);
                it = peerDecoders.erase(it);
                qDebug() << "[AudioMixer] Removed zombie peer:" << vid;
            } else {
                ++it;
            }
        }
        
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
            // Threshold reduced to 6 frames (120ms) for low latency (was 20 frames / 400ms)
            if (isBuffering) {
                if (q.size() >= 6) { 
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
        if (!remoteListenEnabled) return;
        
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
        // Previous 200 (4s) was too high, causing ~2s latency.
        // Adjusted to 25 frames (500ms) to balance jitter resistance and latency.
        const int MAX_PEER_QUEUE = 25; 
        if (peerQueues[vid].size() >= MAX_PEER_QUEUE) {
             // Drop oldest frames to catch up, but leave enough to avoid immediate underrun
             while (peerQueues[vid].size() >= MAX_PEER_QUEUE - 10) {
                 peerQueues[vid].dequeue(); 
             }
             qDebug() << "[AudioMixer] Peer" << vid << "latency high. Dropped 10 frames. Queue:" << peerQueues[vid].size();
        }
        peerQueues[vid].enqueue(opus);
        peerLastActiveTimes[vid] = QDateTime::currentMSecsSinceEpoch();
    });

    QObject::connect(sender, &WebSocketSender::viewerListenMuteRequested, [&](bool mute) {
        remoteListenEnabled = !mute;
        if (mute) {
             QMutexLocker locker(&mixMutex);
             for(auto dec : peerDecoders) opus_decoder_destroy(dec);
             peerDecoders.clear();
             peerQueues.clear();
             peerSilenceCounts.clear();
             peerLastActiveTimes.clear();
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
            
            // 如果编码目标分辨率与屏幕尺寸不同（例如低质720p），进行缩放
            const QSize encSize = staticEncoder->getFrameSize();
            const QSize capSize = staticCapture->getScreenSize();
            if (encSize != capSize) {
                QImage src(reinterpret_cast<const uchar*>(frameData.constData()),
                           capSize.width(), capSize.height(), QImage::Format_ARGB32);
                QImage scaled = src.scaled(encSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                QByteArray scaledData;
                scaledData.resize(scaled.sizeInBytes());
                memcpy(scaledData.data(), scaled.constBits(), scaled.sizeInBytes());
                frameData = scaledData;
            }
            
            // 继续正常的VP9编码流程
            staticEncoder->encode(frameData);
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

    return app.exec();
}
