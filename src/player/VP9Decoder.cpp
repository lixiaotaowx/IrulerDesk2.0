#include "VP9Decoder.h"
#include <QElapsedTimer>
#include <QMutexLocker>
#include <cstring>

// libyuv用于颜色空间转换
#include "libyuv.h"

VP9Decoder::VP9Decoder(QObject *parent)
    : QObject(parent)
    , m_initialized(false)
    , m_interface(nullptr)
{
    memset(&m_codec, 0, sizeof(m_codec));
    memset(&m_stats, 0, sizeof(m_stats));
}

VP9Decoder::~VP9Decoder()
{
    cleanup();
}

bool VP9Decoder::initialize()
{
    QElapsedTimer timer;
    timer.start();
    qDebug() << "[VP9Decoder] [诊断] VP9解码器初始化开始";
    
    QMutexLocker locker(&m_mutex);
    qDebug() << "[VP9Decoder] [诊断] 获取互斥锁耗时:" << timer.elapsed() << "ms";
    
    if (m_initialized) {
        qWarning() << "[VP9Decoder] 解码器已经初始化";
        return true;
    }
    
    qDebug() << "[VP9Decoder] 初始化VP9解码器...";
    qDebug() << "[VP9Decoder] [诊断] 开始设置解码器，当前耗时:" << timer.elapsed() << "ms";
    
    if (!setupDecoder()) {
        qCritical() << "[VP9Decoder] 解码器设置失败";
        return false;
    }
    
    qDebug() << "[VP9Decoder] [诊断] 解码器设置完成，耗时:" << timer.elapsed() << "ms";
    m_initialized = true;
    qDebug() << "[VP9Decoder] VP9解码器初始化成功";
    qDebug() << "[VP9Decoder] [诊断] VP9解码器初始化总耗时:" << timer.elapsed() << "ms";
    
    return true;
}

void VP9Decoder::cleanup()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_initialized) {
        qDebug() << "[VP9Decoder] 清理VP9解码器资源...";
        
        vpx_codec_destroy(&m_codec);
        m_initialized = false;
        
        qDebug() << "[VP9Decoder] VP9解码器资源已清理";
    }
}

QByteArray VP9Decoder::decode(const QByteArray &encodedData)
{
    if (!m_initialized) {
        qWarning() << "[VP9Decoder] 解码器未初始化";
        return QByteArray();
    }
    
    if (encodedData.isEmpty()) {
        return QByteArray();
    }
    
    // 立即解码策略：不等待关键帧，直接解码显示
    static int frameCount = 0;
    static bool firstFrameReceived = false;
    
    if (!firstFrameReceived) {
        firstFrameReceived = true;
    }
    frameCount++;
    
    QElapsedTimer timer;
    timer.start();
    
    // 诊断：获取互斥锁
    static int decodeFrameCount = 0;
    
    QMutexLocker locker(&m_mutex);
    
    // 解码VP9数据
    vpx_codec_err_t res = vpx_codec_decode(&m_codec, 
                                          reinterpret_cast<const uint8_t*>(encodedData.constData()),
                                          encodedData.size(), 
                                          nullptr, 
                                          0);
    
    // VP9解码器可能报告内部错误但实际解码成功，忽略错误继续处理
    if (res != VPX_CODEC_OK) {
        // 只在严重错误时输出日志
        if (res == VPX_CODEC_CORRUPT_FRAME || res == VPX_CODEC_MEM_ERROR) {
            qWarning() << "[VP9Decoder] VP9解码严重错误:" << vpx_codec_err_to_string(res);
        }
    }
    
    // 获取解码后的帧
    vpx_codec_iter_t iter = nullptr;
    vpx_image_t *img = vpx_codec_get_frame(&m_codec, &iter);
    
    if (!img) {
        return QByteArray();
    }
    
    // 检查是否为关键帧 - 使用更准确的方法
    // 在VP9中，关键帧通常在解码后的vpx_image_t结构中没有直接标识
    // 我们需要通过其他方式检测，比如检查帧的特征
    static int keyFrameCount = 0;
    static int totalFrameCount = 0;
    totalFrameCount++;
    
    // 更新帧尺寸
    m_frameSize = QSize(img->d_w, img->d_h);
    
    // 转换YUV到RGB
    QByteArray rgbData = convertYUVToRGB(img);
    
    double decodeTime = timer.elapsed();
    updateStats(decodeTime, true);
    
    // 发射信号
    emit frameDecoded(rgbData, m_frameSize);
    
    return rgbData;
}

bool VP9Decoder::setupDecoder()
{
    // 获取VP9解码器接口
    m_interface = vpx_codec_vp9_dx();
    if (!m_interface) {
        qCritical() << "[VP9Decoder] 无法获取VP9解码器接口";
        return false;
    }
    
    // 初始化解码器配置 - 使用多线程以提升解码速度
    vpx_codec_dec_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.threads = 4; // 使用4线程解码以提升速度
    
    qDebug() << "[VP9Decoder] 使用解码器配置: 4线程模式(最低延迟优化)";
    
    // 初始化解码器 - 使用最基本的配置
    vpx_codec_err_t res = vpx_codec_dec_init(&m_codec, m_interface, &cfg, 0);
    if (res != VPX_CODEC_OK) {
        qCritical() << "[VP9Decoder] 解码器初始化失败:" << vpx_codec_error(&m_codec);
        return false;
    }
    
    // 设置解码器控制参数以匹配编码器
    // 注意：VP9解码器的并行解码设置通过线程数控制，无需额外设置
    
    qDebug() << "[VP9Decoder] 解码器配置完成: 单线程模式, 确保兼容性";
    
    return true;
}

QByteArray VP9Decoder::convertYUVToRGB(const vpx_image_t *img)
{
    if (!img) {
        qWarning() << "[VP9Decoder] 无效的图像指针";
        return QByteArray();
    }
    
    int width = img->d_w;
    int height = img->d_h;
    
    // 检查VP9图像格式 - 支持I420和I444格式
    bool isI420 = (img->fmt == VPX_IMG_FMT_I420);
    bool isI444 = (img->fmt == VPX_IMG_FMT_I444);
    
    if (!isI420 && !isI444) {
        qWarning() << "[VP9Decoder] 不支持的VP9图像格式:" << img->fmt << "仅支持I420和I444";
        return QByteArray();
    }
    
    // 分配ARGB缓冲区 (ARGB格式，每像素4字节)
    QByteArray rgbData(width * height * 4, 0);
    uint8_t *rgbBuffer = reinterpret_cast<uint8_t*>(rgbData.data());
    
    // 获取YUV平面指针和步长
    uint8_t *yPlane = img->planes[VPX_PLANE_Y];
    uint8_t *uPlane = img->planes[VPX_PLANE_U];
    uint8_t *vPlane = img->planes[VPX_PLANE_V];
    
    int yStride = img->stride[VPX_PLANE_Y];
    int uStride = img->stride[VPX_PLANE_U];
    int vStride = img->stride[VPX_PLANE_V];
    
    // 检查VP9颜色空间元数据 - 这是关键的修复！
    // VP9帧包含颜色空间(cs)和颜色范围(range)信息
    int colorSpace = img->cs;  // 0=未知, 1=BT.601, 2=BT.709, 3=SMPTE-170, 4=SMPTE-240, 5=BT.2020, 6=保留, 7=sRGB
    int colorRange = img->range; // 0=Studio Range(16-235), 1=Full Range(0-255)
    
    int result = 0;
    
    // 根据VP9颜色空间和范围选择正确的libyuv转换函数
    if (isI420) {
        // VP9 4:2:0格式 - 根据颜色空间选择转换函数
        if (colorSpace == 2 && colorRange == 1) {
            // BT.709 Full Range - 使用专用转换函数
            result = libyuv::H420ToARGB(
                yPlane, yStride,  // Y平面
                uPlane, uStride,  // U平面
                vPlane, vStride,  // V平面
                rgbBuffer, width * 4,  // ARGB输出缓冲区和步长
                width, height
            );
        } else if (colorSpace == 1 && colorRange == 0) {
            // BT.601 Studio Range - 使用标准I420转换
            result = libyuv::I420ToARGB(
                yPlane, yStride,  // Y平面
                uPlane, uStride,  // U平面
                vPlane, vStride,  // V平面
                rgbBuffer, width * 4,  // ARGB输出缓冲区和步长
                width, height
            );
        } else if (colorRange == 1) {
            // Full Range - 使用J420转换（JPEG颜色范围）
            result = libyuv::J420ToARGB(
                yPlane, yStride,  // Y平面
                uPlane, uStride,  // U平面
                vPlane, vStride,  // V平面
                rgbBuffer, width * 4,  // ARGB输出缓冲区和步长
                width, height
            );
        } else {
            // 默认使用I420转换（Studio Range）
            result = libyuv::I420ToARGB(
                yPlane, yStride,  // Y平面
                uPlane, uStride,  // U平面
                vPlane, vStride,  // V平面
                rgbBuffer, width * 4,  // ARGB输出缓冲区和步长
                width, height
            );
        }
    } else if (isI444) {
        // VP9 4:4:4格式 - 根据颜色范围选择转换函数
        if (colorRange == 1) {
            // Full Range - 使用J444转换
            result = libyuv::J444ToARGB(
                yPlane, yStride,  // Y平面
                uPlane, uStride,  // U平面
                vPlane, vStride,  // V平面
                rgbBuffer, width * 4,  // ARGB输出缓冲区和步长
                width, height
            );
        } else {
            // Studio Range - 使用I444转换
            result = libyuv::I444ToARGB(
                yPlane, yStride,  // Y平面
                uPlane, uStride,  // U平面
                vPlane, vStride,  // V平面
                rgbBuffer, width * 4,  // ARGB输出缓冲区和步长
                width, height
            );
        }
    }
    
    if (result != 0) {
        qWarning() << "[VP9Decoder] libyuv颜色转换失败:" << result 
                   << "格式:" << (isI420 ? "I420" : "I444")
                   << "颜色空间:" << colorSpace << "颜色范围:" << colorRange;
        return QByteArray();
    }
    
    return rgbData;
}

void VP9Decoder::updateStats(double decodeTime, bool success)
{
    m_stats.totalFrames++;
    
    if (success) {
        m_stats.decodedFrames++;
        m_decodeTimes.append(decodeTime);
        
        // 保持最近100帧的解码时间用于计算平均值
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
    
    // 每100帧发射一次统计更新信号
    if (m_stats.totalFrames % 100 == 0) {
        emit statsUpdated(m_stats);
    }
}

void VP9Decoder::decodeFrame(const QByteArray &encodedData)
{
    if (encodedData.isEmpty()) {
        return;
    }
    
    // 调用原有的decode方法
    QByteArray result = decode(encodedData);
}