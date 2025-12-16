#ifndef VP9ENCODER_H
#define VP9ENCODER_H

#include <QObject>
#include <QByteArray>
#include <QSize>
#include <QMutex>

// VP9编码库头文件
#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>
#include <libyuv.h>

class VP9Encoder : public QObject
{
    Q_OBJECT

public:
    explicit VP9Encoder(QObject *parent = nullptr);
    ~VP9Encoder();
    
    bool initialize(int width, int height, int fps);
    void cleanup();
    
    QByteArray encode(const QByteArray &frameData, int inputWidth = -1, int inputHeight = -1);
    
    // 编码参数
    void setBitrate(int bitrate) { m_bitrate = bitrate; }
    void setKeyFrameInterval(int interval) { m_keyFrameInterval = interval; }
    
    // 静态检测参数
    void setStaticThreshold(double threshold) { m_staticThreshold = threshold; }
    void setStaticBitrateReduction(double reduction) { m_staticBitrateReduction = reduction; }
    void setEnableStaticDetection(bool enable) { m_enableStaticDetection = enable; }
    void setSkipStaticFrames(bool skip) { m_skipStaticFrames = skip; }
    void setCpuUsed(int v) { m_cpuUsed = v; }
    void setQuantizerRange(int minQ, int maxQ) { m_minQuantizer = minQ; m_maxQuantizer = maxQ; }
    void setRateControl(int undershootPct, int overshootPct, int bufInitial, int bufOptimal, int bufTotal) { m_undershootPct = undershootPct; m_overshootPct = overshootPct; m_bufInitial = bufInitial; m_bufOptimal = bufOptimal; m_bufTotal = bufTotal; }
    void setDeadline(int v) { m_deadline = v; }
    void setQualityPreset(const QString &q);
    
    // 状态查询
    bool isInitialized() const { return m_initialized; }
    QSize getFrameSize() const { return m_frameSize; }
    int getFrameRate() const { return m_frameRate; }
    double getStaticThreshold() const { return m_staticThreshold; }
    bool isStaticDetectionEnabled() const { return m_enableStaticDetection; }
    
signals:
    void frameEncoded(const QByteArray &encodedData);
    void frameEncodedWithInfo(const QByteArray &encodedData, bool keyFrame);
    void error(const QString &errorMessage);

public slots:
    void forceKeyFrame(); // 强制生成关键帧

private:
    bool initializeEncoder();
    bool convertRGBAToYUV420(const QByteArray &rgbaData, int inputWidth, int inputHeight, uint8_t **yuvPlanes);
    QByteArray encodeFrame(const uint8_t *yPlane, const uint8_t *uPlane, const uint8_t *vPlane);
    
    // 静态检测相关方法
    double calculateFrameDifference(const QByteArray &currentFrame, const QByteArray &previousFrame);
    bool isFrameStatic(const QByteArray &frameData);
    void adjustBitrateForStaticContent(bool isStatic);
    
    // VP9编码器相关
    vpx_codec_ctx_t m_codec;
    vpx_codec_enc_cfg_t m_config;
    vpx_image_t m_rawImage;
    
    // 编码参数
    QSize m_frameSize;
    int m_frameRate;
    int m_bitrate;
    int m_keyFrameInterval;
    
    // 静态检测参数
    bool m_enableStaticDetection;
    double m_staticThreshold;           // 静态检测阈值 (0.0-1.0)
    double m_lowMotionThreshold;        // 低动态阈值 (0.0-1.0)
    double m_staticBitrateReduction;    // 静态内容码率减少比例 (0.0-1.0)
    bool m_skipStaticFrames;            // 是否跳过静态帧
    QByteArray m_previousFrameData;     // 上一帧数据用于比较
    double m_lastFrameDifference;       // 上一帧差异比例
    bool m_lastFrameWasStatic;          // 上一帧是否为静态
    int m_staticFrameCount;             // 连续静态帧计数
    int m_originalBitrate;              // 原始码率备份
    
    // 状态
    bool m_initialized;
    int m_frameCount;
    bool m_forceNextKeyFrame; // 强制下一帧为关键帧
    bool m_lastWasKey;
    
    // YUV转换缓冲区
    uint8_t *m_yPlane;
    uint8_t *m_uPlane;
    uint8_t *m_vPlane;
    int m_yPlaneSize;
    int m_uvPlaneSize;
    
    // 缩放缓冲
    QByteArray m_scaledArgbBuffer;
    
    // 线程安全
    QMutex m_mutex;

    int m_cpuUsed;
    int m_minQuantizer;
    int m_maxQuantizer;
    int m_undershootPct;
    int m_overshootPct;
    int m_bufInitial;
    int m_bufOptimal;
    int m_bufTotal;
    int m_deadline;
};

#endif // VP9ENCODER_H
