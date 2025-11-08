#include <QtWidgets/QApplication>
#include <QTimer>
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
#include "PerformanceMonitor.h" // 新增：性能监控头文件
#include "AnnotationOverlay.h"


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
                    if (q == "low" || q == "medium" || q == "high") {
                        return q;
                    }
                }
            }
            configFile.close();
        }
    }
    return "medium"; // 默认中画质
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

int main(int argc, char *argv[])
{
    // 安装崩溃守护与控制台日志重定向
    CrashGuard::install();
    ConsoleLogger::attachToParentConsole();
    ConsoleLogger::installQtMessageHandler();

    QApplication app(argc, argv);
    
    
    
    // 创建屏幕捕获对象
    ScreenCapture *capture = new ScreenCapture(&app);
    capture->setTargetScreenIndex(getScreenIndexFromConfig());
    if (!capture->initialize()) {
        return -1;
    }
    // qDebug() << "[CaptureProcess] 屏幕捕获模块初始化成功";
    
    // 初始化瓦片系统
    
    
    // 演示不同瓦片大小的性能对比
    QSize screenSize = capture->getScreenSize();
    
    
    
    // 计算推荐的瓦片大小
    QSize optimalSize = TileManager::calculateOptimalTileSize(screenSize);
    int recommendedCount = TileManager::getRecommendedTileCount(screenSize);
    
    
    // 启用自适应瓦片大小
    capture->getTileManager().setAdaptiveTileSize(true);
    
    
    // 使用推荐的瓦片大小初始化
    capture->initializeTileSystem(screenSize, optimalSize);
    capture->enableTileCapture(true);
    
    // 演示瓦片检测开关控制
    
    
    // 可以通过以下方式控制瓦片检测
    // capture->setTileDetectionEnabled(false);  // 禁用瓦片检测
    // capture->toggleTileDetection();           // 切换瓦片检测状态
    
    
    
    // ==================== 瓦片序列化功能测试 ====================
    
    
    // 获取瓦片管理器进行测试
    TileManager& tileManager = capture->getTileManager();
    
    // 测试1: 序列化单个瓦片信息
    
    const QVector<TileInfo>& allTiles = tileManager.getTiles();
    if (!allTiles.isEmpty()) {
        // 创建一个测试瓦片，设置有效的哈希值
        TileInfo originalTile = allTiles.first();
        originalTile.hash = 0x12345678; // 设置一个有效的测试哈希值
        
        QByteArray tileInfoData = tileManager.serializeTileInfo(originalTile);
    
        
        if (tileInfoData.isEmpty()) {
    
        } else {
            // 反序列化测试
            TileInfo deserializedTile = tileManager.deserializeTileInfo(tileInfoData);
            
            bool infoTestPassed = (deserializedTile.x == originalTile.x && 
                                   deserializedTile.y == originalTile.y &&
                                   deserializedTile.width == originalTile.width &&
                                   deserializedTile.height == originalTile.height &&
                                   deserializedTile.hash == originalTile.hash);
    
        }
    } else {
    
    }
    
    // 测试2: 序列化瓦片数据（模拟场景）
    
    QVector<int> testIndices = {0, 1, 2}; // 测试前3个瓦片
    QImage testImage(100, 100, QImage::Format_RGB32);
    testImage.fill(Qt::blue); // 填充蓝色作为测试数据
    
    
    QByteArray serializedData = tileManager.serializeTileData(testIndices, testImage);
    
    
    if (serializedData.isEmpty()) {
    
    } else {
        // 反序列化测试
        QVector<TileInfo> deserializedTiles;
        QVector<QImage> deserializedImages;
        bool deserializeSuccess = tileManager.deserializeTileData(serializedData, deserializedTiles, deserializedImages);
        
    
        
        // 验证数据完整性
        bool dataIntegrityTest = (deserializedTiles.size() == testIndices.size() && 
                                  deserializedImages.size() == testIndices.size());
    
        
        // 验证图像数据
        if (!deserializedImages.isEmpty()) {
            const QImage& firstImage = deserializedImages.first();
    
            bool imageDataValid = (firstImage.size() == testImage.size() && 
                                   firstImage.format() == testImage.format());
    
        }
    }
    
    
    // ==================== 序列化测试结束 ====================
    
    // 创建VP9编码器
    QSize actualScreenSize = capture->getScreenSize();
    
    VP9Encoder *encoder = new VP9Encoder(&app);
    if (!encoder->initialize(actualScreenSize.width(), actualScreenSize.height(), 60)) {
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
    // 创建透明批注覆盖层
    AnnotationOverlay *overlay = new AnnotationOverlay();
    
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
    
    // 创建性能监控器
    
    PerformanceMonitor *perfMonitor = new PerformanceMonitor(&app);
    
    // 注册WebSocketSender到性能监控器
    perfMonitor->registerWebSocketSender(sender);
    
    // 注册TileManager到性能监控器
    perfMonitor->registerTileManager(&capture->getTileManager());
    
    // 设置性能监控参数
    perfMonitor->setVerboseLogging(false);   
    
    // 启动性能监控（直接传递10秒间隔）
    perfMonitor->startMonitoring(10000); // 每10秒输出一次性能报告
    
    
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
    static bool tileMetadataSent = false; // 瓦片元数据发送标志
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
            std::cout << "[Capture] Warning: default input does not support Int16, using preferred format." << std::endl;
            micFormat = preferred;
            audioSampleRate = micFormat.sampleRate();
        }
    }

    QAudioSource *audioSource = new QAudioSource(inDev, micFormat, &app);
    QIODevice *audioInput = nullptr;
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
        msg["data_base64"] = QString::fromUtf8(opusOut.toBase64());
        QJsonDocument doc(msg);
        staticSender->sendTextMessage(doc.toJson(QJsonDocument::Compact));
        // 降低日志噪音：只在偶尔打印
        static int audioCount = 0; audioCount++; if (audioCount % 50 == 0) {
            std::cout << "[Capture] audio_opus sent: " << opusOut.size() << " bytes, sr="
                      << opusSampleRate << ", ch=1" << std::endl;
        }
    });

    // 新增：质量应用方法
    auto applyQualitySetting = [&](const QString &qualityRaw) {
        QString q = qualityRaw.toLower();
        if (q != "low" && q != "medium" && q != "high") {
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
            // 禁用瓦片检测（低质）
            staticCapture->setTileDetectionEnabled(false);
        } else {
            // 中/高质启用瓦片检测
            staticCapture->setTileDetectionEnabled(true);
            desired = orig;
        }

        // 重新初始化编码器以匹配目标分辨率
        staticEncoder->cleanup();
        if (!staticEncoder->initialize(desired.width(), desired.height(), 60)) {
            return; // 保持旧状态以避免崩溃
        }
        targetEncodeSize = staticEncoder->getFrameSize();

        // 按质量调整码率
        if (q == "low") {
            staticEncoder->setBitrate(200000); // 200 kbps
        } else if (q == "medium") {
            staticEncoder->setBitrate(300000); // 300 kbps
        } else { // high
            staticEncoder->setBitrate(1200000); // 1.2 Mbps
        }

        // 重置瓦片元数据发送标志（在启用检测的情况下会重新发送）
        tileMetadataSent = false;
        // 强制关键帧以快速稳定画面
        staticEncoder->forceKeyFrame();

        
    };

    // 启动时应用默认质量（来自配置）
    applyQualitySetting(currentQuality);
    
    // 连接批注事件到叠加层
    QObject::connect(sender, &WebSocketSender::annotationEventReceived,
                     overlay, &AnnotationOverlay::onAnnotationEvent);

    // 对齐叠加层到目标屏幕
    const auto screens = QApplication::screens();
    int screenIndex = getScreenIndexFromConfig();
    QScreen *targetScreen = nullptr;
    if (screenIndex >= 0 && screenIndex < screens.size()) {
        targetScreen = screens[screenIndex];
    } else {
        targetScreen = QApplication::primaryScreen();
    }
    overlay->alignToScreen(targetScreen);

    // 连接推流控制信号
    QObject::connect(sender, &WebSocketSender::streamingStarted, [captureTimer, overlay]() {
        if (!isCapturing) {
            isCapturing = true;
            tileMetadataSent = false; // 重置瓦片元数据发送标志
            captureTimer->start(33); // 30fps
            staticMouseCapture->startCapture(); // 开始鼠标捕获
            overlay->show();
            overlay->raise();
            // qDebug() << "[CaptureProcess] 开始屏幕捕获和鼠标捕获";
            // 注意：音频测试发送不再在推流开始时自动启动，改为由音频开关控制
        }
    });
    
    QObject::connect(sender, &WebSocketSender::streamingStopped, [captureTimer, overlay, audioTimer, audioSource, &audioInput]() {
        if (isCapturing) {
            isCapturing = false;
            captureTimer->stop();
            staticMouseCapture->stopCapture(); // 停止鼠标捕获
            overlay->hide();
            overlay->clear();
            // qDebug() << "[CaptureProcess] 停止屏幕捕获和鼠标捕获";
            audioTimer->stop();
            if (audioSource) {
                audioSource->stop();
                audioInput = nullptr;
            }
            if (opusEnc) { opus_encoder_destroy(opusEnc); opusEnc = nullptr; }
        }
    });

    // 将系统全局鼠标坐标转换为当前捕获屏幕的局部坐标后发送到观看端
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
    QObject::connect(sender, &WebSocketSender::switchScreenRequested, [overlay](const QString &direction, int targetIndex) {
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

        // 重新初始化瓦片系统（保持自适应设置）
        TileManager &tm = staticCapture->getTileManager();
        tm.setAdaptiveTileSize(true);
        QSize optimalSize = TileManager::calculateOptimalTileSize(newSize);
        staticCapture->initializeTileSystem(newSize, optimalSize);
        tileMetadataSent = false; // 切屏后需要重新发送瓦片元数据

        // 重新初始化编码器以匹配新分辨率
        staticEncoder->cleanup();
        if (!staticEncoder->initialize(newSize.width(), newSize.height(), 60)) {
            isSwitching = false;
            return;
        }
        staticEncoder->forceKeyFrame();

        // 对齐批注覆盖层到新屏幕
        QScreen *target = (currentScreenIndex >= 0 && currentScreenIndex < screens.size())
                            ? screens[currentScreenIndex]
                            : QApplication::primaryScreen();
        overlay->alignToScreen(target);
        overlay->show();
        overlay->raise();
        overlay->clear();
        
        // 切换完成，恢复捕获循环发帧
        isSwitching = false;
    });

    // 新增：处理质量变更请求
    QObject::connect(sender, &WebSocketSender::qualityChangeRequested, [&](const QString &quality) {
        applyQualitySetting(quality);
    });

    // 新增：处理音频开关请求（麦克风采集）
    QObject::connect(sender, &WebSocketSender::audioToggleRequested, [audioTimer, sender, audioSource, &audioInput](bool enabled) {
        if (enabled) {
            if (sender->isStreaming()) {
                if (audioSource) {
                    audioInput = audioSource->start();
                }
                // 懒加载创建 Opus 编码器
                if (!opusEnc) {
                    int err = OPUS_OK;
                    opusEnc = opus_encoder_create(opusSampleRate, 1, OPUS_APPLICATION_VOIP, &err);
                    if (err == OPUS_OK && opusEnc) {
                        opus_encoder_ctl(opusEnc, OPUS_SET_BITRATE(24000));
                        opus_encoder_ctl(opusEnc, OPUS_SET_VBR(1));
                        opus_encoder_ctl(opusEnc, OPUS_SET_COMPLEXITY(5));
                        opus_encoder_ctl(opusEnc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                        opus_encoder_ctl(opusEnc, OPUS_SET_INBAND_FEC(1));
                    } else {
                        std::cout << "[Capture] Opus encoder init failed: " << err << std::endl;
                    }
                }
                audioTimer->start();
            }
        } else {
            audioTimer->stop();
            if (audioSource) {
                audioSource->stop();
                audioInput = nullptr;
            }
            if (opusEnc) { opus_encoder_destroy(opusEnc); opusEnc = nullptr; }
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
            
            // 瓦片检测和发送逻辑
            if (staticCapture->isTileDetectionEnabled()) {
                // 执行瓦片检测
                staticCapture->performTileDetection(frameData);
                
                // 发送瓦片元数据（仅在第一次或瓦片配置改变时）
                if (!tileMetadataSent) {
                    TileManager& tileManager = staticCapture->getTileManager();
                    if (tileManager.isInitialized()) {
                        QVector<TileInfo> allTiles = tileManager.getAllTiles();
                        if (!allTiles.isEmpty()) {
                            staticSender->sendTileMetadata(allTiles);
                            tileMetadataSent = true;
                        }
                    }
                }
                
                // 发送变化的瓦片数据
                TileManager& tileManager = staticCapture->getTileManager();
                QVector<TileInfo> changedTiles = tileManager.getChangedTiles();
                if (!changedTiles.isEmpty()) {
                    // 获取变化瓦片的索引
                    QVector<int> changedIndices;
                    for (const TileInfo& tile : changedTiles) {
                        changedIndices.append(tile.x / tileManager.getTileWidth() * tileManager.getTileCountY() + 
                                            tile.y / tileManager.getTileHeight());
                    }
                    
                    // 将帧数据转换为QImage进行瓦片提取
                    QSize screenSize = staticCapture->getScreenSize();
                    QImage frameImage(reinterpret_cast<const uchar*>(frameData.constData()), 
                                    screenSize.width(), screenSize.height(), 
                                    QImage::Format_ARGB32);
                    
                    // 序列化变化的瓦片数据
                    QByteArray serializedData = tileManager.serializeTileData(changedIndices, frameImage);
                    if (!serializedData.isEmpty()) {
                        staticSender->sendTileData(changedIndices, serializedData);
                    }
                }
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
    
    
    
    return app.exec();
}