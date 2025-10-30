#include "DxvaVP9Decoder.h"
#include "VP9Decoder.h"
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QApplication>
#include <QDebug>
#include <QThread>

// 暂时移除所有Windows头文件包含，避免与Qt MOC系统冲突
// 硬件解码功能将在后续版本中重新实现

// GUID定义暂时移除，等待硬件解码功能重新实现

DxvaVP9Decoder::DxvaVP9Decoder(QObject *parent)
    : QObject(parent)
    , m_initialized(false)
    , m_hardwareInitialized(false)
    , m_usingSoftwareFallback(false)
    , m_useHardwareDecoding(true)
    , m_frameSize(0, 0)
    , m_statsTimer(nullptr)
#ifdef _WIN32
    , m_d3d11Device(nullptr)
    , m_d3d11Context(nullptr)
    , m_videoDevice(nullptr)
    , m_videoContext(nullptr)
    , m_videoDecoder(nullptr)
    , m_outputView(nullptr)
    , m_outputTexture(nullptr)
    , m_vp9DecoderGuid(nullptr)
    , m_decoderDesc(nullptr)
    , m_decoderConfig(nullptr)
#endif
{
    memset(&m_stats, 0, sizeof(m_stats));
    
    // 创建软件解码器作为fallback
    m_softwareDecoder = std::make_unique<VP9Decoder>();
    
    qDebug() << "[DxvaVP9Decoder] 创建DXVA VP9解码器，软件fallback已准备";
}

DxvaVP9Decoder::~DxvaVP9Decoder()
{
    cleanup();
}

bool DxvaVP9Decoder::initialize()
{
    QElapsedTimer timer;
    timer.start();
    qDebug() << "[DxvaVP9Decoder] [诊断] DXVA VP9解码器初始化开始";
    
    QMutexLocker locker(&m_mutex);
    qDebug() << "[DxvaVP9Decoder] [诊断] 获取互斥锁耗时:" << timer.elapsed() << "ms";
    
    if (m_initialized) {
        qWarning() << "[DxvaVP9Decoder] 解码器已经初始化";
        return true;
    }
    
    qDebug() << "[DxvaVP9Decoder] 初始化VP9解码器，优先尝试硬件加速...";
    
#ifdef _WIN32
    // 尝试初始化硬件解码器
    qDebug() << "[DxvaVP9Decoder] [诊断] 开始尝试硬件解码器初始化，当前耗时:" << timer.elapsed() << "ms";
    qint64 hardwareInitStart = timer.elapsed();
    if (initializeHardwareDecoder()) {
        qDebug() << "[DxvaVP9Decoder] [诊断] 硬件解码器初始化耗时:" << (timer.elapsed() - hardwareInitStart) << "ms";
        m_useHardwareDecoding = true;
        m_hardwareInitialized = true;
        m_stats.hardwareAccelerated = true;
        m_stats.decoderType = "DXVA 2.0 Hardware";
        qDebug() << "[DxvaVP9Decoder] 硬件加速初始化成功";
        emit hardwareStatusChanged(true);
    } else {
        qDebug() << "[DxvaVP9Decoder] [诊断] 硬件解码器初始化失败，耗时:" << (timer.elapsed() - hardwareInitStart) << "ms";
        qWarning() << "[DxvaVP9Decoder] 硬件加速不可用，使用软件解码";
        fallbackToSoftware("硬件初始化失败");
    }
#else
    qDebug() << "[DxvaVP9Decoder] 非Windows平台，使用软件解码";
    fallbackToSoftware("非Windows平台");
#endif
    
    // 如果硬件解码失败，初始化软件解码器
    if (!m_useHardwareDecoding) {
        qDebug() << "[DxvaVP9Decoder] [诊断] 开始初始化软件解码器，当前耗时:" << timer.elapsed() << "ms";
        qint64 softwareInitStart = timer.elapsed();
        if (!m_softwareDecoder->initialize()) {
            qCritical() << "[DxvaVP9Decoder] 软件解码器初始化失败";
            return false;
        }
        qDebug() << "[DxvaVP9Decoder] [诊断] 软件解码器初始化耗时:" << (timer.elapsed() - softwareInitStart) << "ms";
    }
    
    m_initialized = true;
    qDebug() << "[DxvaVP9Decoder] VP9解码器初始化完成，使用" 
             << (m_useHardwareDecoding ? "硬件加速" : "软件解码");
    qDebug() << "[DxvaVP9Decoder] [诊断] DXVA VP9解码器初始化总耗时:" << timer.elapsed() << "ms";
    
    return true;
}

void DxvaVP9Decoder::cleanup()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_initialized) {
        qDebug() << "[DxvaVP9Decoder] 清理VP9解码器资源...";
        
#ifdef _WIN32
        if (m_useHardwareDecoding) {
            cleanupHardwareDecoder();
        }
#endif
        
        if (m_softwareDecoder) {
            m_softwareDecoder->cleanup();
        }
        
        m_initialized = false;
        m_useHardwareDecoding = false;
        
        qDebug() << "[DxvaVP9Decoder] VP9解码器资源已清理";
    }
}

QByteArray DxvaVP9Decoder::decode(const QByteArray &encodedData)
{
    if (!m_initialized) {
        qWarning() << "[DxvaVP9Decoder] 解码器未初始化";
        return QByteArray();
    }
    
    if (encodedData.isEmpty()) {
        return QByteArray();
    }
    
    QElapsedTimer timer;
    timer.start();
    
    QByteArray result;
    bool success = false;
    
    // 尝试硬件解码
    if (m_useHardwareDecoding) {
#ifdef _WIN32
        result = decodeWithHardware(encodedData);
        success = !result.isEmpty();
        
        // 如果硬件解码失败，fallback到软件解码
        if (!success) {
            qWarning() << "[DxvaVP9Decoder] 硬件解码失败，切换到软件解码";
            fallbackToSoftware("硬件解码失败");
        }
#endif
    }
    
    // 使用软件解码（fallback或主要方式）
    if (!m_useHardwareDecoding || !success) {
        result = m_softwareDecoder->decode(encodedData);
        success = !result.isEmpty();
        
        if (success) {
            m_frameSize = m_softwareDecoder->getFrameSize();
        }
    }
    
    double decodeTime = timer.elapsed();
    updateStats(decodeTime, success, m_useHardwareDecoding && success);
    
    if (success) {
        emit frameDecoded(result, m_frameSize);
    } else {
        emit decoderError("解码失败");
    }
    
    return result;
}

#ifdef _WIN32
bool DxvaVP9Decoder::initializeHardwareDecoder()
{
    // 暂时返回false，使用软件解码器
    // TODO: 实现完整的DXVA硬件解码器初始化
    qDebug() << "[DxvaVP9Decoder] 硬件解码器初始化暂未实现，使用软件fallback";
    return false;
}

bool DxvaVP9Decoder::checkVP9HardwareSupport()
{
    // 暂时返回false，使用软件解码器
    // TODO: 实现VP9硬件支持检查
    qDebug() << "[DxvaVP9Decoder] VP9硬件支持检查暂未实现";
    return false;
}

QByteArray DxvaVP9Decoder::decodeWithHardware(const QByteArray &encodedData)
{
    // 暂时返回空，使用软件解码器
    // TODO: 实现DXVA VP9硬件解码
    Q_UNUSED(encodedData)
    qDebug() << "[DxvaVP9Decoder] 硬件解码暂未实现";
    return QByteArray();
}

void DxvaVP9Decoder::cleanupHardwareDecoder()
{
    qDebug() << "[DxvaVP9Decoder] 清理硬件解码器资源";
    
    // 清理硬件资源
    // TODO: 实现完整的硬件资源清理
    m_d3d11Device = nullptr;
    m_d3d11Context = nullptr;
    m_videoDevice = nullptr;
    m_videoContext = nullptr;
    m_videoDecoder = nullptr;
    m_outputView = nullptr;
    m_outputTexture = nullptr;
    m_vp9DecoderGuid = nullptr;
    m_decoderDesc = nullptr;
    m_decoderConfig = nullptr;
}

QByteArray DxvaVP9Decoder::convertNV12ToRGB(void* texture, const QSize& size)
{
    // NV12到RGB转换实现
    // 这里应该实现GPU纹理到CPU内存的复制和颜色空间转换
    Q_UNUSED(texture)
    Q_UNUSED(size)
    
    return QByteArray();
}
#endif

void DxvaVP9Decoder::fallbackToSoftware(const QString& reason)
{
    if (m_useHardwareDecoding) {
        qWarning() << "[DxvaVP9Decoder] 切换到软件解码，原因:" << reason;
        
        m_useHardwareDecoding = false;
        m_stats.hardwareAccelerated = false;
        m_stats.decoderType = "libvpx Software";
        
        emit hardwareStatusChanged(false);
        
        // 确保软件解码器已初始化
        if (!m_softwareDecoder->getStats().totalFrames && m_initialized) {
            m_softwareDecoder->initialize();
        }
    }
}

void DxvaVP9Decoder::updateStats(double decodeTime, bool success, bool hardware)
{
    m_stats.totalFrames++;
    
    if (success) {
        m_stats.decodedFrames++;
        m_decodeTimes.append(decodeTime);
        
        // 保持最近100帧的解码时间
        if (m_decodeTimes.size() > 100) {
            m_decodeTimes.removeFirst();
        }
        
        // 计算平均解码时间
        double total = 0;
        for (double time : m_decodeTimes) {
            total += time;
        }
        m_stats.averageDecodeTime = total / m_decodeTimes.size();
    } else {
        m_stats.errorFrames++;
    }
    
    m_stats.hardwareAccelerated = hardware;
    
    // 每10帧发送一次统计更新
    if (m_stats.totalFrames % 10 == 0) {
        emit statsUpdated(m_stats);
    }
}

void DxvaVP9Decoder::decodeFrame(const QByteArray &encodedData)
{
    // 直接调用decode，不重复发射信号
    decode(encodedData);
}