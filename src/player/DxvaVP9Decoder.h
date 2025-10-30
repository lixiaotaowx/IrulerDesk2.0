#ifndef DXVAVP9DECODER_H
#define DXVAVP9DECODER_H

#include <QObject>
#include <QByteArray>
#include <QSize>
#include <QMutex>
#include <QDebug>

// 前向声明，避免在头文件中包含Windows API
#ifdef _WIN32
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11VideoDevice;
struct ID3D11VideoContext;
struct ID3D11VideoDecoder;
struct ID3D11VideoDecoderOutputView;
struct ID3D11Texture2D;

template<typename T>
class ComPtr;
#endif

// VP9软件解码器作为fallback
#include "VP9Decoder.h"

class DxvaVP9Decoder : public QObject
{
    Q_OBJECT

public:
    explicit DxvaVP9Decoder(QObject *parent = nullptr);
    ~DxvaVP9Decoder();
    
    // 初始化解码器（优先尝试硬件加速）
    bool initialize();
    
    // 清理资源
    void cleanup();
    
    // 解码VP9帧数据
    QByteArray decode(const QByteArray &encodedData);
    
    // 获取当前帧尺寸
    QSize getFrameSize() const { return m_frameSize; }
    
    // 检查是否使用硬件加速
    bool isHardwareAccelerated() const { return m_useHardwareDecoding; }
    
    // 获取解码统计信息
    struct DecoderStats {
        quint64 totalFrames;
        quint64 decodedFrames;
        quint64 errorFrames;
        double averageDecodeTime;
        bool hardwareAccelerated;
        QString decoderType;
    };
    DecoderStats getStats() const { return m_stats; }

public slots:
    void decodeFrame(const QByteArray &encodedData);

signals:
    void frameDecoded(const QByteArray &frameData, const QSize &frameSize);
    void decoderError(const QString &error);
    void statsUpdated(const DecoderStats &stats);
    void hardwareStatusChanged(bool enabled);

private:
    // 硬件解码相关
#ifdef _WIN32
    bool initializeHardwareDecoder();
    void cleanupHardwareDecoder();
    bool checkVP9HardwareSupport();
    QByteArray decodeWithHardware(const QByteArray &encodedData);
    
    // 硬件解码相关 - 使用void*避免头文件依赖
    void* m_d3d11Device;
    void* m_d3d11Context;
    void* m_videoDevice;
    void* m_videoContext;
    void* m_videoDecoder;
    void* m_outputView;
    void* m_outputTexture;
    
    // 解码器配置
    void* m_vp9DecoderGuid;
    void* m_decoderDesc;
    void* m_decoderConfig;
#endif
    
    // 软件解码器作为fallback
    std::unique_ptr<VP9Decoder> m_softwareDecoder;
    
    // 状态管理
    bool m_hardwareInitialized;
    bool m_usingSoftwareFallback;
    bool m_initialized;
    
    // 解码器统计
    DecoderStats m_stats;
    QTimer *m_statsTimer;
    
    // 辅助方法
    void updateStats(double decodeTime, bool success, bool hardware);
    QByteArray convertNV12ToRGB(void* texture, const QSize& size);
    void fallbackToSoftware(const QString& reason);
    
    // 缺失的成员变量
    QSize m_frameSize;
    bool m_useHardwareDecoding;
    QMutex m_mutex;
    QList<double> m_decodeTimes;
};

#endif // DXVAVP9DECODER_H