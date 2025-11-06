#include "VP9Encoder.h"
#include <QMutexLocker>
#include <QDateTime>
#include <cstring>
#include <algorithm>
#include <chrono>

VP9Encoder::VP9Encoder(QObject *parent)
    : QObject(parent)
    , m_frameRate(30)  // 保持30fps流畅度
    , m_bitrate(300000) // 降低到300 kbps，最低可看清质量
    , m_keyFrameInterval(30) // 30帧关键帧间隔
    , m_initialized(false)
    , m_frameCount(0)
    , m_forceNextKeyFrame(false) // 初始化强制关键帧标志
    , m_yPlane(nullptr)
    , m_uPlane(nullptr)
    , m_vPlane(nullptr)
    , m_yPlaneSize(0)
    , m_uvPlaneSize(0)
    // 静态检测参数初始化 - 更激进的流量节省
    , m_enableStaticDetection(true)      // 启用静态检测
    , m_staticThreshold(0.01)            // 降低到1%变化阈值，更敏感
    , m_staticBitrateReduction(0.15)     // 静态内容码率减少85%，更激进
    , m_skipStaticFrames(false)          // 不跳帧，只降码率
    , m_lastFrameWasStatic(false)
    , m_staticFrameCount(0)
    , m_originalBitrate(300000)
{
    memset(&m_codec, 0, sizeof(m_codec));
    memset(&m_config, 0, sizeof(m_config));
    memset(&m_rawImage, 0, sizeof(m_rawImage));
}

VP9Encoder::~VP9Encoder()
{
    cleanup();
}

bool VP9Encoder::initialize(int width, int height, int fps)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_initialized) {
        cleanup();
    }
    
    
    
    m_frameSize = QSize(width, height);
    m_frameRate = fps;
    m_originalBitrate = m_bitrate;  // 保存原始码率
    
    // 分配YUV缓冲区
    m_yPlaneSize = width * height;
    m_uvPlaneSize = (width / 2) * (height / 2);
    
    m_yPlane = new uint8_t[m_yPlaneSize];
    m_uPlane = new uint8_t[m_uvPlaneSize];
    m_vPlane = new uint8_t[m_uvPlaneSize];
    
    if (!m_yPlane || !m_uPlane || !m_vPlane) {
        cleanup();
        return false;
    }
    
    // 初始化VP9编码器
    if (!initializeEncoder()) {
        cleanup();
        return false;
    }
    
    m_initialized = true;
    m_frameCount = 0;
    
    
    return true;
}

void VP9Encoder::cleanup()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_initialized) {
        return;
    }
    
    
    
    // 销毁编码器
    if (vpx_codec_destroy(&m_codec) != VPX_CODEC_OK) {
        
    }
    
    // 释放YUV缓冲区
    delete[] m_yPlane;
    delete[] m_uPlane;
    delete[] m_vPlane;
    m_yPlane = nullptr;
    m_uPlane = nullptr;
    m_vPlane = nullptr;
    
    // 释放原始图像
    vpx_img_free(&m_rawImage);
    
    m_initialized = false;
}

QByteArray VP9Encoder::encode(const QByteArray &frameData)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_initialized) {
        return QByteArray();
    }
    
    if (frameData.isEmpty()) {
        return QByteArray();
    }
    
    auto encodeStartTime = std::chrono::high_resolution_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(encodeStartTime.time_since_epoch()).count();
    
    // 静态检测逻辑
    bool isStatic = false;
    if (m_enableStaticDetection && !m_previousFrameData.isEmpty()) {
        isStatic = isFrameStatic(frameData);
        adjustBitrateForStaticContent(isStatic);
        
        // 静态检测调试日志（已禁用以提升性能）
        // static int logCounter = 0;
        // if (++logCounter % 30 == 0) { // 每30帧输出一次日志
        //     qDebug() << "[VP9Encoder] 静态检测状态 - 当前帧:" << (isStatic ? "静态" : "动态") 
        //              << "连续静态帧数:" << m_staticFrameCount 
        //              << "当前码率:" << m_bitrate;
        // }
        
        // 如果启用跳帧且当前帧为静态，则跳过编码
        if (m_skipStaticFrames && isStatic && m_staticFrameCount > 3) {
            // qDebug() << "[VP9Encoder] 跳过静态帧，连续静态帧数:" << m_staticFrameCount;
            return QByteArray(); // 返回空数据表示跳帧
        }
    }
    
    // 转换RGBA到YUV420
    uint8_t *yuvPlanes[3] = { m_yPlane, m_uPlane, m_vPlane };
    if (!convertRGBAToYUV420(frameData, yuvPlanes)) {
        return QByteArray();
    }
    
    // 先递增帧计数器，确保第一帧从1开始
    m_frameCount++;
    
    // 编码帧
    QByteArray encodedData = encodeFrame(m_yPlane, m_uPlane, m_vPlane);
    if (!encodedData.isEmpty()) {
        emit frameEncoded(encodedData);
        
        // 保存当前帧数据用于下次比较
        if (m_enableStaticDetection) {
            m_previousFrameData = frameData;
        }
    } else {
        // 如果编码失败，回退帧计数器
        m_frameCount--;
    }
    
    auto encodeEndTime = std::chrono::high_resolution_clock::now();
    auto encodeLatency = std::chrono::duration_cast<std::chrono::microseconds>(encodeEndTime - encodeStartTime).count();
    
    static int encodeCounter = 0;
    static int totalEncodeTime = 0;
    static int maxEncodeTime = 0;
    static int minEncodeTime = INT_MAX;
    
    encodeCounter++;
    totalEncodeTime += encodeLatency;
    maxEncodeTime = std::max(maxEncodeTime, (int)encodeLatency);
    minEncodeTime = std::min(minEncodeTime, (int)encodeLatency);
    
    // 每1000帧输出一次简化统计，避免影响性能
    if (encodeCounter % 1000 == 0) {
        int avgEncodeTime = totalEncodeTime / encodeCounter;
        // 移除统计日志以提升性能
        // qDebug() << "[VP9编码器] [统计] 第" << encodeCounter << "帧，平均耗时:" << avgEncodeTime << "μs";
    }
    // 移除每帧的延迟监控打印以提升性能
    
    return encodedData;
}

void VP9Encoder::forceKeyFrame()
{
    QMutexLocker locker(&m_mutex);
    m_forceNextKeyFrame = true;
    // 移除关键帧请求日志以提升性能
    // qDebug() << "[VP9Encoder] [首帧策略] 收到强制关键帧请求，下一帧将强制为关键帧";
}

bool VP9Encoder::initializeEncoder()
{
    // 获取默认配置
    vpx_codec_err_t res = vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &m_config, 0);
    if (res != VPX_CODEC_OK) {
        return false;
    }
    
    // 确保分辨率是偶数（VP9要求）
    int width = m_frameSize.width();
    int height = m_frameSize.height();
    if (width % 2 != 0) width = (width / 2) * 2;
    if (height % 2 != 0) height = (height / 2) * 2;
    
    // 调整分辨率为16的倍数（VP9要求）
    int adjustedWidth = (width + 15) & ~15;
    int adjustedHeight = (height + 15) & ~15;
    
    
    
    // 优化实时编码参数 - 最低可看清质量设置
    m_config.g_w = width;
    m_config.g_h = height;
    m_config.g_timebase.num = 1;
    m_config.g_timebase.den = m_frameRate;
    m_config.rc_target_bitrate = m_bitrate / 1000; // kbps
    m_config.g_error_resilient = 1; // 启用错误恢复
    m_config.g_pass = VPX_RC_ONE_PASS; // 单遍编码
    m_config.g_lag_in_frames = 0; // 零延迟
    m_config.rc_end_usage = VPX_CBR; // 恒定比特率模式
    // 调整量化器范围 - 最低质量但可看清
    m_config.rc_min_quantizer = 10;  // 提高最低量化器，降低质量
    m_config.rc_max_quantizer = 56; // 提高最大量化器，进一步降低质量
    // 优化缓冲区设置 - 极低延迟配置
    m_config.rc_undershoot_pct = 100; // 最大下冲容忍度
    m_config.rc_overshoot_pct = 100; // 最大上冲容忍度
    m_config.rc_buf_initial_sz = 10; // 极小初始缓冲区
    m_config.rc_buf_optimal_sz = 20; // 极小最优缓冲区
    m_config.rc_buf_sz = 30; // 极小总缓冲区大小
    // 关键帧设置 - 减少关键帧频率以节省流量
    m_config.kf_mode = VPX_KF_AUTO;
    m_config.kf_min_dist = 0;
    m_config.kf_max_dist = 60; // 每60帧一个关键帧，节省流量
    
    // 更新实际使用的分辨率
    m_frameSize = QSize(width, height);
    
    // 初始化编码器
    res = vpx_codec_enc_init(&m_codec, vpx_codec_vp9_cx(), &m_config, 0);
    if (res != VPX_CODEC_OK) {
        return false;
    }
    
    // 设置编码器控制参数 - 极速实时编码和多线程优化
    vpx_codec_err_t ctrl_res;
    
    // 使用极速实时编码速度 (cpu-used=8，优先速度)
    ctrl_res = vpx_codec_control(&m_codec, VP8E_SET_CPUUSED, 8);
    if (ctrl_res != VPX_CODEC_OK) {
        
    }
    
    // 启用瓦片列以提升并行性能 (增加到3列)
    ctrl_res = vpx_codec_control(&m_codec, VP9E_SET_TILE_COLUMNS, 3);
    if (ctrl_res != VPX_CODEC_OK) {
        
    }
    
    // 启用帧并行解码以提升多线程性能
    ctrl_res = vpx_codec_control(&m_codec, VP9E_SET_FRAME_PARALLEL_DECODING, 1);
    if (ctrl_res != VPX_CODEC_OK) {
        
    }
    
    // 启用行级多线程编码
    ctrl_res = vpx_codec_control(&m_codec, VP9E_SET_ROW_MT, 1);
    if (ctrl_res != VPX_CODEC_OK) {
        
    }
    
    // 启用自适应量化模式以优化编码效率
    ctrl_res = vpx_codec_control(&m_codec, VP9E_SET_AQ_MODE, 3);
    if (ctrl_res != VPX_CODEC_OK) {
        
    }
    
    // 设置静态阈值参数 - VP9特有的静态检测优化
    ctrl_res = vpx_codec_control(&m_codec, VP8E_SET_STATIC_THRESHOLD, 1);
    if (ctrl_res != VPX_CODEC_OK) {
        
    } else {
        
    }
    
    // 启用噪声敏感度设置以改善静态内容编码
    ctrl_res = vpx_codec_control(&m_codec, VP8E_SET_NOISE_SENSITIVITY, 1);
    if (ctrl_res != VPX_CODEC_OK) {
        
    }
    
    // 设置实时模式 - 禁用自动替代参考帧
    ctrl_res = vpx_codec_control(&m_codec, VP8E_SET_ENABLEAUTOALTREF, 0);
    if (ctrl_res != VPX_CODEC_OK) {
        
    }
    
    // VP9不使用VP8E_SET_ERROR_RESILIENT，而是通过其他方式提高兼容性
    // 确保帧并行解码被禁用以提高兼容性（已在上面设置）
    
    
    // 分配原始图像
    if (!vpx_img_alloc(&m_rawImage, VPX_IMG_FMT_I420, m_config.g_w, m_config.g_h, 1)) {
        return false;
    }
    
    return true;
}

bool VP9Encoder::convertRGBAToYUV420(const QByteArray &rgbaData, uint8_t **yuvPlanes)
{
    if (rgbaData.size() != m_frameSize.width() * m_frameSize.height() * 4) {
        return false;
    }
    
    const uint8_t *rgbaPtr = reinterpret_cast<const uint8_t*>(rgbaData.constData());
    int width = m_frameSize.width();
    int height = m_frameSize.height();
    
    // 使用libyuv库进行硬件加速的RGBA到YUV420转换
    // 修复颜色空间转换，使用正确的BT.709色彩空间
    int result = libyuv::ARGBToI420(
        rgbaPtr, width * 4,  // ARGB源数据和步长
        yuvPlanes[0], width,  // Y平面
        yuvPlanes[1], width / 2,  // U平面  
        yuvPlanes[2], width / 2,  // V平面
        width, height
    );
    
    // 应用BT.709色彩矩阵修正（修复蓝色变黄色问题）
    // 这是常见的RGB到YUV转换色彩偏移问题
    if (result == 0) {
        // 对U和V平面进行轻微调整以修正色彩偏移
        uint8_t* uPlane = yuvPlanes[1];
        uint8_t* vPlane = yuvPlanes[2];
        int uvSize = (width / 2) * (height / 2);
        
        for (int i = 0; i < uvSize; i++) {
            // 修正色彩偏移：减少U分量，增加V分量
            int u = uPlane[i] - 128;
            int v = vPlane[i] - 128;
            
            // 应用色彩修正矩阵
            u = u * 0.95;  // 轻微减少蓝色分量
            v = v * 1.05;  // 轻微增加红色分量
            
            uPlane[i] = std::max(0, std::min(255, u + 128));
            vPlane[i] = std::max(0, std::min(255, v + 128));
        }
    }
    
    if (result != 0) {
        return false;
    }
    
    return true;
}

QByteArray VP9Encoder::encodeFrame(const uint8_t *yPlane, const uint8_t *uPlane, const uint8_t *vPlane)
{
    // 复制YUV数据到原始图像
    memcpy(m_rawImage.planes[VPX_PLANE_Y], yPlane, m_yPlaneSize);
    memcpy(m_rawImage.planes[VPX_PLANE_U], uPlane, m_uvPlaneSize);
    memcpy(m_rawImage.planes[VPX_PLANE_V], vPlane, m_uvPlaneSize);
    
    // 编码帧 - 超快启动关键帧策略：第一帧立即关键帧，后续渐进式
    vpx_enc_frame_flags_t flags = 0;
    bool shouldForceKeyFrame = false;
    
    if (m_frameCount == 1) { // 第一帧必须是关键帧，立即显示
        shouldForceKeyFrame = true;
        // 移除帧打印以提升性能
    } else if (m_frameCount == 2 || m_frameCount == 3) { // 前3帧为关键帧，确保稳定
        shouldForceKeyFrame = true;
        // 移除帧打印以提升性能
    } else if (m_forceNextKeyFrame) {
        shouldForceKeyFrame = true;
        // 移除帧打印以提升性能
        m_forceNextKeyFrame = false;
    } else if (m_frameCount <= 180) { // 前3秒（60fps * 3秒 = 180帧）
        // 每30帧生成一个关键帧（约0.5秒间隔），减少编码延迟
        if (m_frameCount % 30 == 0) {
            shouldForceKeyFrame = true;
            // 移除帧打印以提升性能
        }
    } else if (m_frameCount % 60 == 0) { // 3秒后每60帧一个关键帧（1秒间隔）
        shouldForceKeyFrame = true;
        // 移除帧打印以提升性能
    }
    
    if (shouldForceKeyFrame) {
        flags = VPX_EFLAG_FORCE_KF;
    }
    
    vpx_codec_err_t res = vpx_codec_encode(&m_codec, &m_rawImage, m_frameCount, 1, flags, VPX_DL_GOOD_QUALITY);
    if (res != VPX_CODEC_OK) {
        return QByteArray();
    }
    
    // 获取编码后的数据
    QByteArray encodedData;
    vpx_codec_iter_t iter = nullptr;
    const vpx_codec_cx_pkt_t *pkt;
    
    while ((pkt = vpx_codec_get_cx_data(&m_codec, &iter)) != nullptr) {
        if (pkt->kind == VPX_CODEC_CX_FRAME_PKT) {
            const char *frameData = static_cast<const char*>(pkt->data.frame.buf);
            int frameSize = static_cast<int>(pkt->data.frame.sz);
            
            // 添加时间戳到编码数据前（8字节，毫秒时间戳）
            qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
            QByteArray timestampData;
            timestampData.resize(8);
            memcpy(timestampData.data(), &timestamp, 8);
            
            // 组合时间戳和编码数据
            encodedData.append(timestampData);
            encodedData.append(frameData, frameSize);
            
            // 调试信息
            static int debugFrameCount = 0;
            
            // 移除性能统计打印以提升性能
            // 每20帧统计一次性能 - 已禁用
            // if (debugFrameCount % 20 == 0) {
            //     static qint64 lastStatsTime = QDateTime::currentMSecsSinceEpoch();
            //     qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
            //     qint64 timeDiff = currentTime - lastStatsTime;
            //     double fps = timeDiff > 0 ? (20.0 * 1000.0 / timeDiff) : 0.0;
            //     qDebug() << "[VP9Encoder] 性能统计 - 帧" << debugFrameCount << ", 最近20帧用时:" << timeDiff << "ms, 平均FPS:" << QString::number(fps, 'f', 1);
            //     lastStatsTime = currentTime;
            // }
            
            // 删除详细帧日志以提升性能
            // if (debugFrameCount < 5) { // 输出前5帧的详细信息 - 已禁用
            //     bool isKeyFrame = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
            //     qDebug() << "[VP9Encoder] 编码帧" << debugFrameCount 
            //              << "大小:" << frameSize << "字节"
            //              << (isKeyFrame ? "(关键帧)" : "(普通帧)");
            // }
            
            debugFrameCount++; // 每次编码都递增计数器
            break;
        }
    }
    
    return encodedData;
}

// 静态检测相关方法实现
double VP9Encoder::calculateFrameDifference(const QByteArray &currentFrame, const QByteArray &previousFrame)
{
    if (currentFrame.size() != previousFrame.size() || currentFrame.isEmpty()) {
        return 1.0; // 完全不同
    }
    
    const int frameSize = currentFrame.size();
    const uint8_t *current = reinterpret_cast<const uint8_t*>(currentFrame.constData());
    const uint8_t *previous = reinterpret_cast<const uint8_t*>(previousFrame.constData());
    
    // 采样检测以提高性能 - 每16个像素检测一个
    const int sampleStep = 16 * 4; // RGBA格式，每个像素4字节，每16个像素采样
    int totalSamples = 0;
    int differentSamples = 0;
    
    for (int i = 0; i < frameSize; i += sampleStep) {
        totalSamples++;
        
        // 计算RGB差异（忽略Alpha通道）
        int rDiff = abs(current[i] - previous[i]);
        int gDiff = abs(current[i + 1] - previous[i + 1]);
        int bDiff = abs(current[i + 2] - previous[i + 2]);
        
        // 如果任何颜色通道差异超过阈值，认为像素发生变化
        if (rDiff > 8 || gDiff > 8 || bDiff > 8) {
            differentSamples++;
        }
    }
    
    return totalSamples > 0 ? (double)differentSamples / totalSamples : 0.0;
}

bool VP9Encoder::isFrameStatic(const QByteArray &frameData)
{
    if (m_previousFrameData.isEmpty()) {
        m_previousFrameData = frameData;
        m_lastFrameWasStatic = false;
        m_staticFrameCount = 0;
        return false;
    }
    
    double difference = calculateFrameDifference(frameData, m_previousFrameData);
    bool isStatic = difference < m_staticThreshold;
    
    if (isStatic) {
        if (m_lastFrameWasStatic) {
            m_staticFrameCount++;
        } else {
            m_staticFrameCount = 1;
            
        }
    } else {
        if (m_lastFrameWasStatic && m_staticFrameCount > 0) {
            
        }
        m_staticFrameCount = 0;
    }
    
    m_lastFrameWasStatic = isStatic;
    
    // 每100帧输出一次静态检测统计（用于调试）
    static int staticDebugCounter = 0;
    if (++staticDebugCounter % 100 == 0) {
        
    }
    
    return isStatic;
}

void VP9Encoder::adjustBitrateForStaticContent(bool isStatic)
{
    if (!m_enableStaticDetection) {
        return;
    }
    
    int targetBitrate;
    if (isStatic && m_staticFrameCount > 3) { // 降低触发阈值，更快响应
        // 静态内容使用更低的码率 - 85%减少
        targetBitrate = static_cast<int>(m_originalBitrate * m_staticBitrateReduction);
        
    } else {
        // 动态内容使用原始码率
        targetBitrate = m_originalBitrate;
        if (!isStatic && m_bitrate != m_originalBitrate) {
            
        }
    }
    
    // 只有当码率需要改变时才更新
    if (targetBitrate != m_bitrate) {
        m_bitrate = targetBitrate;
        
        // 动态更新编码器码率
        m_config.rc_target_bitrate = m_bitrate / 1000; // kbps
        vpx_codec_err_t res = vpx_codec_enc_config_set(&m_codec, &m_config);
        if (res != VPX_CODEC_OK) {
            
        } else {
            
        }
    }
}