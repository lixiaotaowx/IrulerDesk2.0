#ifndef VP9DECODER_H
#define VP9DECODER_H

#include <QObject>
#include <QByteArray>
#include <QSize>
#include <QMutex>
#include <QDebug>

// VP9解码库
#define VPX_CODEC_DISABLE_COMPAT 1
#include "vpx/vpx_decoder.h"
#include "vpx/vp8dx.h"

class VP9Decoder : public QObject
{
    Q_OBJECT

public:
    explicit VP9Decoder(QObject *parent = nullptr);
    ~VP9Decoder();
    
    // 初始化解码器
    bool initialize();
    
    // 清理资源
    void cleanup();
    
    // 解码VP9帧数据
    QByteArray decode(const QByteArray &encodedData);
    
    // 获取当前帧尺寸
    QSize getFrameSize() const { return m_frameSize; }
    
    // 获取解码统计信息
    struct DecoderStats {
        quint64 totalFrames;
        quint64 decodedFrames;
        quint64 errorFrames;
        double averageDecodeTime;
    };
    DecoderStats getStats() const { return m_stats; }

public slots:
    void decodeFrame(const QByteArray &encodedData);

signals:
    void frameDecoded(const QByteArray &frameData, const QSize &frameSize);
    void decoderError(const QString &error);
    void statsUpdated(const DecoderStats &stats);

private:
    bool m_initialized;
    vpx_codec_ctx_t m_codec;
    vpx_codec_iface_t *m_interface;
    
    QSize m_frameSize;
    QMutex m_mutex;
    
    // 统计信息
    DecoderStats m_stats;
    QList<double> m_decodeTimes;
    
    // 内部方法
    bool setupDecoder();
    QByteArray convertYUVToRGB(const vpx_image_t *img);
    void updateStats(double decodeTime, bool success);
};

#endif // VP9DECODER_H