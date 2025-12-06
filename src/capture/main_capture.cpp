#include <QtWidgets/QApplication>
#include <QTimer>
#include <QVector>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <iostream>
#include <chrono>
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
    
    // 配置VP9静态检测优化参数
    // qDebug() << "[CaptureProcess] 配置VP9静态检测优化...";
    encoder->setEnableStaticDetection(true);        // 启用静态检测
    encoder->setStaticThreshold(5.0);               // 设置5%的变化阈值，降低敏感度
    encoder->setStaticBitrateReduction(0.5);        // 静态内容码率减少50%，不要太激进
    encoder->setSkipStaticFrames(false);            // 禁用静态帧跳过，避免视频流中断
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
    
    // 连接到WebSocket服务器 - 使用推流URL格式
    // 从配置文件读取设备ID和服务器地址，如果没有则使用默认值
    QString deviceId = getDeviceIdFromConfig(); // 从配置文件读取设备ID
    QString serverAddress = getServerAddressFromConfig(); // 从配置文件读取服务器地址
    // qDebug() << "[CaptureProcess] 使用设备ID:" << deviceId;
    // qDebug() << "[CaptureProcess] 使用服务器地址:" << serverAddress;
    QString serverUrl = QString("ws://%1/publish/%2").arg(serverAddress, deviceId);
    if (!sender->connectToServer(serverUrl)) {
        return -1;
    }
    // qDebug() << "[CaptureProcess] 正在连接到WebSocket服务器:" << serverUrl;
    
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
    micFormat.setSampleRate(audioSampleRate);
    micFormat.setChannelCount(audioChannels);
    micFormat.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice inDev = QMediaDevices::defaultAudioInput();
    if (!inDev.isFormatSupported(micFormat)) {
        QAudioFormat preferred = inDev.preferredFormat();
        if (preferred.sampleFormat() == QAudioFormat::Int16) {
            micFormat = preferred;
            audioSampleRate = micFormat.sampleRate();
        } else {
            // 如果不支持 Int16，则仍使用首选格式，但可能与播放器不匹配
            micFormat = preferred;
            audioSampleRate = micFormat.sampleRate();
        }
    }

    QAudioSource *audioSource = new QAudioSource(inDev, micFormat, &app);
    QIODevice *audioInput = nullptr;
    int currentMicGainPercent = 100;
    // 同步 Opus 编码器所用的采样率与帧长（20ms）
    opusSampleRate = micFormat.sampleRate();
    opusFrameSize = opusSampleRate / 50;

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
        const int frameSamples = micFormat.sampleRate() / 50; // 20ms 帧
        const int needBytes = frameSamples * srcChannels * srcBytesPerSample;
        QByteArray pcmSrc = audioInput->read(needBytes);
        if (pcmSrc.size() < needBytes) {
            return; // 数据不足，下一次再试
        }

        // 统一转换为 Int16 单声道（取第一个声道），保证观看端一致播放/编码
        QByteArray pcm;
        pcm.resize(frameSamples * sizeof(int16_t));
        int16_t *dst = reinterpret_cast<int16_t*>(pcm.data());

        if (micFormat.sampleFormat() == QAudioFormat::Int16) {
            // 源就是 Int16，按通道取第一个声道
            const int16_t *src = reinterpret_cast<const int16_t*>(pcmSrc.constData());
            for (int i = 0; i < frameSamples; ++i) {
                dst[i] = src[i * srcChannels];
            }
        } else if (micFormat.sampleFormat() == QAudioFormat::Float) {
            const float *src = reinterpret_cast<const float*>(pcmSrc.constData());
            for (int i = 0; i < frameSamples; ++i) {
                float s = src[i * srcChannels];
                if (s > 1.0f) s = 1.0f; if (s < -1.0f) s = -1.0f;
                dst[i] = static_cast<int16_t>(s * 32767.0f);
            }
        } else if (micFormat.sampleFormat() == QAudioFormat::UInt8) {
            const uint8_t *src = reinterpret_cast<const uint8_t*>(pcmSrc.constData());
            for (int i = 0; i < frameSamples; ++i) {
                // UInt8 无符号，中心点 128
                int v = static_cast<int>(src[i * srcChannels]) - 128;
                dst[i] = static_cast<int16_t>(v << 8);
            }
        } else if (micFormat.sampleFormat() == QAudioFormat::Int32) {
            const int32_t *src = reinterpret_cast<const int32_t*>(pcmSrc.constData());
            for (int i = 0; i < frameSamples; ++i) {
                dst[i] = static_cast<int16_t>(src[i * srcChannels] >> 16);
            }
        } else {
            // 未知格式，直接丢弃本次
            return;
        }

        auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();

        if (!opusEnc) {
            return; // 编码器未初始化（音频未启用）
        }

        // Opus 编码（20ms 帧）
        QByteArray opusOut;
        opusOut.resize(4096); // 足够容纳单帧 Opus 数据
        int nbytes = opus_encode(opusEnc,
                                 reinterpret_cast<const opus_int16*>(dst),
                                 opusFrameSize,
                                 reinterpret_cast<unsigned char*>(opusOut.data()),
                                 opusOut.size());
        if (nbytes < 0) {
            // 编码失败，跳过本帧
            return;
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
        staticSender->sendTextMessage(doc.toJson(QJsonDocument::Compact));
        // 日志清理：移除音频发送打印
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
        if (q == "low") {
            staticEncoder->setBitrate(200000);
            staticEncoder->setEnableStaticDetection(true);
            staticEncoder->setStaticBitrateReduction(0.20);
            staticEncoder->setStaticThreshold(0.012);
        } else if (q == "medium") {
            staticEncoder->setBitrate(400000);
            staticEncoder->setEnableStaticDetection(true);
            staticEncoder->setStaticBitrateReduction(0.55);
            staticEncoder->setStaticThreshold(0.015);
        } else if (q == "high") {
            staticEncoder->setBitrate(500000);
            staticEncoder->setEnableStaticDetection(true);
            staticEncoder->setStaticBitrateReduction(0.50);
            staticEncoder->setStaticThreshold(0.015);
        } else if (q == "extreme") {
            staticEncoder->setBitrate(3000000);
            staticEncoder->setEnableStaticDetection(true);
            staticEncoder->setStaticBitrateReduction(0.95);
            staticEncoder->setStaticThreshold(0.015);
        }

        // 强制关键帧以快速稳定画面
        staticEncoder->forceKeyFrame();

        
    };

    // 启动时应用默认质量（来自配置）
    applyQualitySetting(currentQuality);
    
    
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

    // 连接推流控制信号
    QObject::connect(sender, &WebSocketSender::streamingStarted, [captureTimer]() {
        if (!isCapturing) {
            isCapturing = true;
            captureTimer->start(33); // 30fps
            staticMouseCapture->startCapture(); // 开始鼠标捕获
            if (currentScreenIndex >= 0 && currentScreenIndex < s_overlays.size()) {
                s_overlays[currentScreenIndex]->raise();
                s_cursorOverlays[currentScreenIndex]->raise();
            }
            // qDebug() << "[CaptureProcess] 开始屏幕捕获和鼠标捕获";
            // 注意：音频测试发送不再在推流开始时自动启动，改为由音频开关控制
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
        }
    });

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
    QAudioSink *remoteSink = nullptr;
    QIODevice *remoteOutIO = nullptr;
    QAudioFormat remoteFmt;
    OpusDecoder *remoteOpusDec = nullptr;
    int remoteSampleRate = 16000;
    int remoteChannels = 1;
    bool remoteListenEnabled = true;
    QObject::connect(sender, &WebSocketSender::audioToggleRequested, [&, audioTimer, sender, audioSource](bool enabled) {
        if (enabled) {
            // std::cout << "[Sender] audio_toggle enabled" << std::endl;
            if (!sender->isStreaming()) return;
            // 懒加载创建 Opus 编码器
            opusSampleRate = 16000;
            opusFrameSize = opusSampleRate / 50;
            if (!opusEnc) {
                int err = OPUS_OK;
                opusEnc = opus_encoder_create(opusSampleRate, 1, OPUS_APPLICATION_VOIP, &err);
                if (err == OPUS_OK && opusEnc) {
                    opus_encoder_ctl(opusEnc, OPUS_SET_BITRATE(24000));
                    opus_encoder_ctl(opusEnc, OPUS_SET_VBR(1));
                    opus_encoder_ctl(opusEnc, OPUS_SET_COMPLEXITY(5));
                    opus_encoder_ctl(opusEnc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                    opus_encoder_ctl(opusEnc, OPUS_SET_INBAND_FEC(1));
                    // std::cout << "[Sender] opus encoder init ok sr=16000" << std::endl;
                } else {
                    // std::cout << "[Sender] opus encoder init failed" << std::endl;
                }
            }
            if (false && QFile::exists(testMp3Path)) {
                // std::cout << "[Sender] using mp3 file: " << testMp3Path.toStdString() << std::endl;
                if (!mp3Decoder) {
                    mp3Decoder = new QAudioDecoder(&app);
                    QObject::connect(mp3Decoder, &QAudioDecoder::formatChanged, [&](const QAudioFormat &fmt){
                        // std::cout << "[Sender] mp3 decoder format sr=" << fmt.sampleRate() << " ch=" << fmt.channelCount() << std::endl;
                    });
                    QObject::connect(mp3Decoder, &QAudioDecoder::bufferReady, [&]() {
                        if (!opusEnc) return;
                        QAudioBuffer buffer = mp3Decoder->read();
                        if (!buffer.isValid()) return;
                        const QAudioFormat &fmt = buffer.format();
                        int sr = fmt.sampleRate();
                        int ch = fmt.channelCount();
                        if (sr <= 0 || ch <= 0) return;
                        const int frames = buffer.frameCount();
                        // std::cout << "[Sender] mp3 buffer sr=" << sr << " ch=" << ch << " frames=" << frames << std::endl;
                        QByteArray mono;
                        mono.resize(frames * sizeof(int16_t));
                        int16_t *dst = reinterpret_cast<int16_t*>(mono.data());
                        if (fmt.sampleFormat() == QAudioFormat::Int16) {
                            const int16_t *src = buffer.data<const int16_t>();
                            for (int i = 0; i < frames; ++i) dst[i] = src[i * ch];
                        } else if (fmt.sampleFormat() == QAudioFormat::Float) {
                            const float *src = buffer.data<const float>();
                            for (int i = 0; i < frames; ++i) {
                                float s = src[i * ch];
                                if (s > 1.0f) s = 1.0f; if (s < -1.0f) s = -1.0f;
                                dst[i] = static_cast<int16_t>(s * 32767.0f);
                            }
                        } else if (fmt.sampleFormat() == QAudioFormat::Int32) {
                            const int32_t *src = buffer.data<const int32_t>();
                            for (int i = 0; i < frames; ++i) dst[i] = static_cast<int16_t>(src[i * ch] >> 16);
                        } else if (fmt.sampleFormat() == QAudioFormat::UInt8) {
                            const uint8_t *src = buffer.data<const uint8_t>();
                            for (int i = 0; i < frames; ++i) { int v = static_cast<int>(src[i * ch]) - 128; dst[i] = static_cast<int16_t>(v << 8); }
                        } else {
                            return;
                        }
                        QByteArray monoUse = mono;
                        if (sr != opusSampleRate) {
                            int outFrames = (frames * opusSampleRate) / sr;
                            QByteArray res;
                            res.resize(outFrames * sizeof(int16_t));
                            const int16_t *msrc = reinterpret_cast<const int16_t*>(monoUse.constData());
                            int16_t *mdst = reinterpret_cast<int16_t*>(res.data());
                            for (int i = 0; i < outFrames; ++i) {
                                int idx = (i * sr) / opusSampleRate;
                                if (idx < frames) mdst[i] = msrc[idx]; else mdst[i] = 0;
                            }
                            monoUse = res;
                            // std::cout << "[Sender] resampled to 16k frames=" << outFrames << std::endl;
                        }
                        pcmAccum.append(monoUse);
                        // std::cout << "[Sender] pcmAccum bytes=" << pcmAccum.size() << std::endl;
                        while (pcmAccum.size() >= opusFrameSize * sizeof(int16_t)) {
                            QByteArray one = pcmAccum.left(opusFrameSize * sizeof(int16_t));
                            pcmAccum.remove(0, opusFrameSize * sizeof(int16_t));
                            QByteArray opusOut;
                            opusOut.resize(4096);
                            int nbytes = opus_encode(opusEnc,
                                                     reinterpret_cast<const opus_int16*>(one.constData()),
                                                     opusFrameSize,
                                                     reinterpret_cast<unsigned char*>(opusOut.data()),
                                                     opusOut.size());
                            if (nbytes < 0) continue;
                            opusOut.resize(nbytes);
                            auto ts = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                            QJsonObject msg;
                            msg["type"] = "audio_opus";
                            msg["sample_rate"] = opusSampleRate;
                            msg["channels"] = 1;
                            msg["timestamp"] = static_cast<qint64>(ts);
                            msg["frame_samples"] = opusFrameSize;
                            static quint32 audioSeq = 0;
                            msg["seq"] = static_cast<qint64>(audioSeq++);
                            msg["data_base64"] = QString::fromUtf8(opusOut.toBase64());
                            QJsonDocument doc(msg);
                            staticSender->sendTextMessage(doc.toJson(QJsonDocument::Compact));
                            // std::cout << "[Sender] opus frame sent bytes=" << nbytes << " seq=" << (audioSeq-1) << std::endl;
                        }
                    });
                    QObject::connect(mp3Decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), [&](QAudioDecoder::Error){
                        // std::cout << "[Sender] mp3 decode error" << std::endl;
                    });
                    QObject::connect(mp3Decoder, &QAudioDecoder::finished, [&]() {
                        mp3Decoder->stop();
                        mp3Decoder->setSource(QUrl::fromLocalFile(testMp3Path));
                        mp3Decoder->start();
                        // std::cout << "[Sender] mp3 finished, restart" << std::endl;
                    });
                }
                mp3Decoder->setSource(QUrl::fromLocalFile(testMp3Path));
                // std::cout << "[Sender] mp3 decoder start" << std::endl;
                mp3Decoder->start();
                audioTimer->stop();
            } else {
                // std::cout << "[Sender] mp3 file not found, fallback mic" << std::endl;
                // 回退到麦克风采集
                if (audioSource) {
                    audioInput = audioSource->start();
                    audioSource->setVolume(currentMicGainPercent / 100.0);
                }
                audioTimer->start();
            }
        } else {
            // std::cout << "[Sender] audio_toggle disabled" << std::endl;
            audioTimer->stop();
            if (audioSource) { audioSource->stop(); audioInput = nullptr; }
            if (mp3Decoder) { mp3Decoder->stop(); }
            if (opusEnc) { opus_encoder_destroy(opusEnc); opusEnc = nullptr; }
            if (remoteSink) { remoteSink->stop(); delete remoteSink; remoteSink = nullptr; remoteOutIO = nullptr; }
            if (remoteOpusDec) { opus_decoder_destroy(remoteOpusDec); remoteOpusDec = nullptr; }
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

    QObject::connect(sender, &WebSocketSender::viewerAudioOpusReceived, [&](const QByteArray &opus, int sr, int ch, int frameSamples, qint64 /*ts*/) {
        if (!remoteListenEnabled) return;
        if (!remoteOpusDec || remoteSampleRate != sr || remoteChannels != ch) {
            if (remoteOpusDec) { opus_decoder_destroy(remoteOpusDec); remoteOpusDec = nullptr; }
            remoteSampleRate = sr; remoteChannels = ch;
            int err = OPUS_OK;
            remoteOpusDec = opus_decoder_create(sr, ch, &err);
            if (remoteSink) { remoteSink->stop(); delete remoteSink; remoteSink = nullptr; remoteOutIO = nullptr; }
            remoteFmt = QAudioFormat();
            remoteFmt.setSampleRate(sr);
            remoteFmt.setChannelCount(ch);
            remoteFmt.setSampleFormat(QAudioFormat::Int16);
            QAudioDevice outDev = QMediaDevices::defaultAudioOutput();
            remoteSink = new QAudioSink(outDev, remoteFmt, &app);
            remoteSink->setBufferSize(4096);
            remoteOutIO = remoteSink->start();
        }
        if (!remoteOpusDec || !remoteSink) return;
        QByteArray pcm;
        pcm.resize(frameSamples * ch * sizeof(opus_int16));
        int decodedSamples = opus_decode(remoteOpusDec,
                                        reinterpret_cast<const unsigned char*>(opus.constData()),
                                        opus.size(),
                                        reinterpret_cast<opus_int16*>(pcm.data()),
                                        frameSamples,
                                        0);
        if (decodedSamples <= 0) return;
        pcm.resize(decodedSamples * ch * sizeof(opus_int16));
        if (remoteSink->state() == QAudio::StoppedState) { remoteOutIO = remoteSink->start(); }
        if (remoteSink->state() == QAudio::SuspendedState) { remoteSink->resume(); }
        if (remoteOutIO) remoteOutIO->write(pcm);
    });

    QObject::connect(sender, &WebSocketSender::viewerListenMuteRequested, [&](bool mute) {
        remoteListenEnabled = !mute;
        if (mute) {
            if (remoteSink) { remoteSink->stop(); delete remoteSink; remoteSink = nullptr; remoteOutIO = nullptr; }
            if (remoteOpusDec) { opus_decoder_destroy(remoteOpusDec); remoteOpusDec = nullptr; }
        }
    });
    
    QObject::connect(captureTimer, &QTimer::timeout, []() {
        if (!isCapturing) return; // 只有在推流状态下才捕获
        if (isSwitching) return;   // 热切换过程中不断流，但暂时不抓帧
        
        auto captureStartTime = std::chrono::high_resolution_clock::now();
        QByteArray frameData = staticCapture->captureScreen();
        if (!frameData.isEmpty()) {
            auto captureEndTime = std::chrono::high_resolution_clock::now();
            auto captureLatency = std::chrono::duration_cast<std::chrono::microseconds>(captureEndTime - captureStartTime).count();
            frameCount++;
            // 只在延迟过高时输出警告
            if (captureLatency > 20000) { // 超过20ms时输出警告
            }
            
            // 瓦片检测已移除

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
    
    
    
    return app.exec();
}
