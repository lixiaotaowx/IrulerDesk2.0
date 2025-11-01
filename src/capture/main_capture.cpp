#include <QGuiApplication>
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <iostream>
#include <chrono>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#endif
#include "ScreenCapture.h"
#include "VP9Encoder.h"
#include "WebSocketSender.h"
#include "MouseCapture.h" // 新增：鼠标捕获头文件
#include "PerformanceMonitor.h" // 新增：性能监控头文件

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
    
    qDebug() << "[CaptureProcess] 尝试读取配置文件，可能的路径:";
    for (const QString& path : possiblePaths) {
        qDebug() << "[CaptureProcess]   - " << path;
    }
    
    for (const QString& configFilePath : possiblePaths) {
        QFile configFile(configFilePath);
        if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qDebug() << "[CaptureProcess] 找到配置文件:" << configFilePath;
            QTextStream in(&configFile);
            QString line;
            while (!in.atEnd()) {
                line = in.readLine();
                if (line.startsWith("random_id=")) {
                    QString deviceId = line.mid(10); // 去掉"random_id="前缀
                    configFile.close();
                    qDebug() << "[CaptureProcess] 从配置文件读取到设备ID:" << deviceId;
                    return deviceId;
                }
            }
            configFile.close();
        }
    }
    
    // 如果读取失败，返回默认值
    qDebug() << "[CaptureProcess] 所有配置文件路径都无法读取，使用默认设备ID: 25561";
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
    
    qDebug() << "[CaptureProcess] 尝试读取服务器地址配置，可能的路径:";
    for (const QString& path : possiblePaths) {
        qDebug() << "[CaptureProcess]   - " << path;
    }
    
    for (const QString& configFilePath : possiblePaths) {
        QFile configFile(configFilePath);
        if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qDebug() << "[CaptureProcess] 找到配置文件:" << configFilePath;
            QTextStream in(&configFile);
            QString line;
            while (!in.atEnd()) {
                line = in.readLine();
                if (line.startsWith("server_address=")) {
                    QString serverAddress = line.mid(15); // 去掉"server_address="前缀
                    configFile.close();
                    qDebug() << "[CaptureProcess] 从配置文件读取到服务器地址:" << serverAddress;
                    return serverAddress;
                }
            }
            configFile.close();
        }
    }
    
    // 如果读取失败，返回默认值（腾讯云）
    qDebug() << "[CaptureProcess] 所有配置文件路径都无法读取服务器地址，使用默认 123.207.222.92:8765";
    return "123.207.222.92:8765";
}

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
        
        // 设置控制台编码为UTF-8以正确显示中文
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }
#endif

    QGuiApplication app(argc, argv);
    
    qDebug() << "[CaptureProcess] ========== 启动屏幕捕获进程 ==========";
    
    // 创建屏幕捕获对象
    qDebug() << "[CaptureProcess] 初始化屏幕捕获模块...";
    ScreenCapture *capture = new ScreenCapture(&app);
    if (!capture->initialize()) {
        qCritical() << "[CaptureProcess] 屏幕捕获初始化失败";
        return -1;
    }
    qDebug() << "[CaptureProcess] 屏幕捕获模块初始化成功";
    
    // 初始化瓦片系统
    qDebug() << "[Main] 初始化瓦片系统...";
    
    // 演示不同瓦片大小的性能对比
    QSize screenSize = QSize(1920, 1080); // 假设屏幕尺寸
    
    qDebug() << "[Main] === 瓦片大小性能分析 ===";
    qDebug() << "[Main] 64x64瓦片数量:" << (1920/64 + 1) * (1080/64 + 1) << "个";
    qDebug() << "[Main] 96x96瓦片数量:" << (1920/96 + 1) * (1080/96 + 1) << "个";
    qDebug() << "[Main] 128x128瓦片数量:" << (1920/128 + 1) * (1080/128 + 1) << "个";
    
    // 计算推荐的瓦片大小
    QSize optimalSize = TileManager::calculateOptimalTileSize(screenSize);
    int recommendedCount = TileManager::getRecommendedTileCount(screenSize);
    qDebug() << "[Main] 推荐瓦片大小:" << optimalSize << "瓦片数量:" << recommendedCount << "个";
    
    // 启用自适应瓦片大小
    capture->getTileManager().setAdaptiveTileSize(true);
    qDebug() << "[Main] 已启用自适应瓦片大小";
    
    // 使用推荐的瓦片大小初始化
    capture->initializeTileSystem(screenSize, optimalSize);
    capture->enableTileCapture(true);
    
    // 演示瓦片检测开关控制
    qDebug() << "[Main] 瓦片检测开关演示:";
    qDebug() << "[Main] 当前瓦片检测状态:" << (capture->isTileDetectionEnabled() ? "启用" : "禁用");
    
    // 可以通过以下方式控制瓦片检测
    // capture->setTileDetectionEnabled(false);  // 禁用瓦片检测
    // capture->toggleTileDetection();           // 切换瓦片检测状态
    
    qDebug() << "[Main] 瓦片系统初始化完成";
    qDebug() << "[Main] 总瓦片数:" << capture->getTileCount();
    qDebug() << "[Main] 瓦片检测已启用";
    
    // ==================== 瓦片序列化功能测试 ====================
    qDebug() << "[Main] 开始测试瓦片序列化功能...";
    
    // 获取瓦片管理器进行测试
    TileManager& tileManager = capture->getTileManager();
    
    // 测试1: 序列化单个瓦片信息
    qDebug() << "[Main] ========== 测试1: 序列化单个瓦片信息 ==========";
    const QVector<TileInfo>& allTiles = tileManager.getTiles();
    if (!allTiles.isEmpty()) {
        // 创建一个测试瓦片，设置有效的哈希值
        TileInfo originalTile = allTiles.first();
        originalTile.hash = 0x12345678; // 设置一个有效的测试哈希值
        qDebug() << "[Main] 原始瓦片信息: x=" << originalTile.x << "y=" << originalTile.y 
                 << "width=" << originalTile.width << "height=" << originalTile.height 
                 << "hash=" << originalTile.hash;
        
        QByteArray tileInfoData = tileManager.serializeTileInfo(originalTile);
        qDebug() << "[Main] 瓦片信息序列化数据大小:" << tileInfoData.size() << "字节";
        
        if (tileInfoData.isEmpty()) {
            qDebug() << "[Main] 错误: 序列化数据为空";
        } else {
            // 反序列化测试
            TileInfo deserializedTile = tileManager.deserializeTileInfo(tileInfoData);
            qDebug() << "[Main] 反序列化瓦片信息: x=" << deserializedTile.x << "y=" << deserializedTile.y 
                     << "width=" << deserializedTile.width << "height=" << deserializedTile.height 
                     << "hash=" << deserializedTile.hash;
            
            bool infoTestPassed = (deserializedTile.x == originalTile.x && 
                                   deserializedTile.y == originalTile.y &&
                                   deserializedTile.width == originalTile.width &&
                                   deserializedTile.height == originalTile.height &&
                                   deserializedTile.hash == originalTile.hash);
            qDebug() << "[Main] 瓦片信息序列化测试结果:" << (infoTestPassed ? "✓ 通过" : "✗ 失败");
        }
    } else {
        qDebug() << "[Main] 错误: 没有可用的瓦片进行测试";
    }
    
    // 测试2: 序列化瓦片数据（模拟场景）
    qDebug() << "[Main] ========== 测试2: 序列化瓦片数据 ==========";
    QVector<int> testIndices = {0, 1, 2}; // 测试前3个瓦片
    QImage testImage(100, 100, QImage::Format_RGB32);
    testImage.fill(Qt::blue); // 填充蓝色作为测试数据
    qDebug() << "[Main] 测试图像信息: 大小=" << testImage.size() << "格式=" << testImage.format();
    
    QByteArray serializedData = tileManager.serializeTileData(testIndices, testImage);
    qDebug() << "[Main] 瓦片数据序列化大小:" << serializedData.size() << "字节";
    
    if (serializedData.isEmpty()) {
        qDebug() << "[Main] 错误: 序列化数据为空";
    } else {
        // 反序列化测试
        QVector<TileInfo> deserializedTiles;
        QVector<QImage> deserializedImages;
        bool deserializeSuccess = tileManager.deserializeTileData(serializedData, deserializedTiles, deserializedImages);
        
        qDebug() << "[Main] 瓦片数据反序列化结果:" << (deserializeSuccess ? "✓ 成功" : "✗ 失败");
        qDebug() << "[Main] 反序列化瓦片数:" << deserializedTiles.size() << "期望:" << testIndices.size();
        qDebug() << "[Main] 反序列化图像数:" << deserializedImages.size() << "期望:" << testIndices.size();
        
        // 验证数据完整性
        bool dataIntegrityTest = (deserializedTiles.size() == testIndices.size() && 
                                  deserializedImages.size() == testIndices.size());
        qDebug() << "[Main] 数据完整性测试结果:" << (dataIntegrityTest ? "✓ 通过" : "✗ 失败");
        
        // 验证图像数据
        if (!deserializedImages.isEmpty()) {
            const QImage& firstImage = deserializedImages.first();
            qDebug() << "[Main] 第一个反序列化图像: 大小=" << firstImage.size() << "格式=" << firstImage.format();
            bool imageDataValid = (firstImage.size() == testImage.size() && 
                                   firstImage.format() == testImage.format());
            qDebug() << "[Main] 图像数据验证:" << (imageDataValid ? "✓ 通过" : "✗ 失败");
        }
    }
    
    qDebug() << "[Main] ========================================";
    qDebug() << "[Main] 瓦片序列化功能测试完成";
    qDebug() << "[Main] ========================================";
    // ==================== 序列化测试结束 ====================
    
    // 创建VP9编码器
    QSize actualScreenSize = capture->getScreenSize();
    qDebug() << "[CaptureProcess] 屏幕尺寸:" << actualScreenSize.width() << "x" << actualScreenSize.height();
    qDebug() << "[CaptureProcess] 初始化VP9编码器...";
    VP9Encoder *encoder = new VP9Encoder(&app);
    if (!encoder->initialize(actualScreenSize.width(), actualScreenSize.height(), 60)) {
        qCritical() << "[CaptureProcess] VP9编码器初始化失败";
        return -1;
    }
    qDebug() << "[CaptureProcess] VP9编码器初始化成功";
    
    // 在编码器准备好后再启动WebSocket发送器
    qDebug() << "[CaptureProcess] 启动WebSocket发送器...";
    WebSocketSender *sender = new WebSocketSender(&app);
    
    // 连接到WebSocket服务器 - 使用推流URL格式
    // 从配置文件读取设备ID和服务器地址，如果没有则使用默认值
    QString deviceId = getDeviceIdFromConfig(); // 从配置文件读取设备ID
    QString serverAddress = getServerAddressFromConfig(); // 从配置文件读取服务器地址
    qDebug() << "[CaptureProcess] 使用设备ID:" << deviceId;
    qDebug() << "[CaptureProcess] 使用服务器地址:" << serverAddress;
    QString serverUrl = QString("ws://%1/publish/%2").arg(serverAddress, deviceId);
    if (!sender->connectToServer(serverUrl)) {
        qCritical() << "[CaptureProcess] 连接到WebSocket服务器失败";
        return -1;
    }
    qDebug() << "[CaptureProcess] 正在连接到WebSocket服务器:" << serverUrl;
    
    // 创建性能监控器
    qDebug() << "[CaptureProcess] 初始化性能监控器...";
    PerformanceMonitor *perfMonitor = new PerformanceMonitor(&app);
    
    // 注册WebSocketSender到性能监控器
    perfMonitor->registerWebSocketSender(sender);
    
    // 注册TileManager到性能监控器
    perfMonitor->registerTileManager(&capture->getTileManager());
    
    // 设置性能监控参数
    perfMonitor->setVerboseLogging(true);   // 启用详细日志
    
    // 启动性能监控（直接传递10秒间隔）
    perfMonitor->startMonitoring(10000); // 每10秒输出一次性能报告
    qDebug() << "[CaptureProcess] 性能监控器已启动，报告间隔: 10秒";
    qDebug() << "[CaptureProcess] 已注册WebSocketSender和TileManager到性能监控器";
    
    // 连接信号槽
    qDebug() << "[CaptureProcess] 连接编码器和服务器信号槽...";
    QObject::connect(encoder, &VP9Encoder::frameEncoded,
                     sender, &WebSocketSender::sendFrame);
    
    // 连接首帧关键帧策略信号槽
    QObject::connect(sender, &WebSocketSender::requestKeyFrame,
                     encoder, &VP9Encoder::forceKeyFrame);
    qDebug() << "[CaptureProcess] [首帧策略] 已连接关键帧请求信号槽";
    
    // 创建鼠标捕获模块
    qDebug() << "[CaptureProcess] 初始化鼠标捕获模块...";
    MouseCapture *mouseCapture = new MouseCapture(&app);
    
    // 连接鼠标位置消息到WebSocket发送器
    QObject::connect(mouseCapture, &MouseCapture::mousePositionMessage,
                     sender, &WebSocketSender::sendTextMessage);
    qDebug() << "[CaptureProcess] 鼠标捕获模块初始化成功，已连接到WebSocket发送器";
    
    // 创建定时器进行屏幕捕获 - 优化：减少lambda捕获开销
    qDebug() << "[CaptureProcess] 创建屏幕捕获定时器(60fps)...";
    QTimer *captureTimer = new QTimer(&app);
    
    // 优化：使用静态变量避免lambda捕获开销
    static int frameCount = 0;
    static ScreenCapture *staticCapture = capture;
    static VP9Encoder *staticEncoder = encoder;
    static MouseCapture *staticMouseCapture = mouseCapture; // 新增：静态鼠标捕获指针
    static WebSocketSender *staticSender = sender; // 新增：静态WebSocket发送器指针
    static bool isCapturing = false; // 控制捕获状态
    static bool tileMetadataSent = false; // 瓦片元数据发送标志
    
    // 连接推流控制信号
    QObject::connect(sender, &WebSocketSender::streamingStarted, [captureTimer]() {
        if (!isCapturing) {
            isCapturing = true;
            tileMetadataSent = false; // 重置瓦片元数据发送标志
            captureTimer->start(33); // 30fps
            staticMouseCapture->startCapture(); // 开始鼠标捕获
            qDebug() << "[CaptureProcess] 开始屏幕捕获和鼠标捕获";
        }
    });
    
    QObject::connect(sender, &WebSocketSender::streamingStopped, [captureTimer]() {
        if (isCapturing) {
            isCapturing = false;
            captureTimer->stop();
            staticMouseCapture->stopCapture(); // 停止鼠标捕获
            qDebug() << "[CaptureProcess] 停止屏幕捕获和鼠标捕获";
        }
    });
    
    QObject::connect(captureTimer, &QTimer::timeout, []() {
        if (!isCapturing) return; // 只有在推流状态下才捕获
        
        auto captureStartTime = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(captureStartTime.time_since_epoch()).count();
        
        QByteArray frameData = staticCapture->captureScreen();
        if (!frameData.isEmpty()) {
            auto captureEndTime = std::chrono::high_resolution_clock::now();
            auto captureLatency = std::chrono::duration_cast<std::chrono::microseconds>(captureEndTime - captureStartTime).count();
            // qDebug() << "[延迟监控] 屏幕捕获完成，时间戳:" << timestamp << "μs，捕获耗时:" << captureLatency << "μs"; // 已禁用以提升性能
            
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
                            qDebug() << "[CaptureProcess] 发送瓦片元数据，总瓦片数:" << allTiles.size();
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
                        qDebug() << "[CaptureProcess] 发送变化瓦片数据，瓦片数:" << changedIndices.size() 
                                << "数据大小:" << serializedData.size() << "字节";
                    }
                }
            }
            
            // 继续正常的VP9编码流程
            staticEncoder->encode(frameData);
            frameCount++;
        }
    });
    
    // 默认不开始捕获，等待观看请求
    qDebug() << "[CaptureProcess] 屏幕捕获定时器已创建，等待观看请求...";
    qDebug() << "[CaptureProcess] ========== 捕获进程启动完成 ==========";
    
    qDebug() << "[CaptureProcess] 屏幕捕获进程已启动，等待观看请求...";
    
    return app.exec();
}