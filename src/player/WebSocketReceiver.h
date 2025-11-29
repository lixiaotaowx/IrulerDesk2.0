#ifndef WEBSOCKETRECEIVER_H
#define WEBSOCKETRECEIVER_H

#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QtMultimedia/QAudioSource>
#include <QtMultimedia/QAudioSink>
#include <QtMultimedia/QAudioFormat>
#include <QtMultimedia/QMediaDevices>
#include <QUrl>
#include <QByteArray>
#include <QMutex>
#include <QPoint>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QQueue>
#include <opus/opus.h>

class WebSocketReceiver : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketReceiver(QObject *parent = nullptr);
    ~WebSocketReceiver();
    
    // 瓦片数据结构
    struct TileMetadata {
        int tileId;
        int x, y, width, height;
        int totalChunks;
        int dataSize;
        qint64 timestamp;
        QString format;
    };
    
    struct TileChunk {
        int tileId;
        int chunkIndex;
        int totalChunks;
        QByteArray data;
        qint64 timestamp;
    };
    
    struct TileUpdate {
        int tileId;
        int x, y, width, height;
        QByteArray deltaData;
        qint64 timestamp;
    };
    
    struct TileCache {
        TileMetadata metadata;
        QMap<int, QByteArray> chunks; // chunkIndex -> data
        QSet<int> receivedChunks;
        qint64 lastUpdateTime;
        bool isComplete;
    };
    
    // 连接到WebSocket服务器
    bool connectToServer(const QString &url);
    
    // 发送观看请求
    void sendWatchRequest(const QString &viewerId, const QString &targetId);
    // 发送远程批注事件：phase为"down"/"move"/"up"，坐标为源屏幕坐标，附带颜色ID
    void sendAnnotationEvent(const QString &phase, int x, int y, int colorId = 0);
    void sendTextAnnotation(const QString &text, int x, int y, int colorId, int fontSize);
    // 发送切换屏幕请求（滚动到下一屏幕）
    void sendSwitchScreenNext();
    // 按索引切换屏幕（不断流热切换，不修改配置）
    void sendSwitchScreenIndex(int index);
    // 发送质量设置（高/中/低）控制被观看者的编码质量
    void sendSetQuality(const QString &quality);
    // 发送音频测试开关（观看端控制被观看者是否发送测试音）
    void sendAudioToggle(bool enabled);
    void sendAudioGain(int percent);
    void setTalkEnabled(bool enabled);
    void sendViewerListenMute(bool mute);
    void sendLikeEvent();
    void setLocalInputDeviceFollowSystem();
    void setLocalInputDeviceById(const QString &id);
    bool isLocalInputFollowSystem() const { return m_followSystemInput; }
    QString localInputDeviceId() const { return m_localInputDeviceId; }
    void setViewerName(const QString &name);
    void sendViewerCursor(int x, int y);
    // 暂停推流但保留连接
    void sendStopStreaming();
    
    // 断开连接
    void disconnectFromServer();
    
    // 检查连接状态
    bool isConnected() const;
    
    // 获取接收统计信息
    struct ReceiverStats {
        quint64 totalFrames;
        quint64 totalBytes;
        double averageFrameSize;
        qint64 connectionTime;
        // 瓦片统计
        quint64 totalTiles;
        quint64 completedTiles;
        quint64 lostTiles;
        quint64 retransmissionRequests;
        double tileCompletionRate;
        
        // 详细性能统计
        quint64 totalTileBytes;           // 瓦片总字节数
        quint64 totalChunksReceived;      // 接收的数据块总数
        quint64 totalChunksLost;          // 丢失的数据块数
        double averageTileSize;           // 平均瓦片大小
        double tileTransferRate;          // 瓦片传输速率 (bytes/sec)
        double chunkLossRate;             // 数据块丢包率
        qint64 averageAssemblyTime;       // 平均瓦片组装时间 (ms)
        qint64 averageTransmissionTime;   // 平均传输时间 (ms)
        qint64 maxAssemblyTime;           // 最大组装时间 (ms)
        qint64 minAssemblyTime;           // 最小组装时间 (ms)
        
        // 网络性能统计
        double networkLatency;            // 网络延迟 (ms)
        double jitter;                    // 网络抖动 (ms)
        quint64 reconnectionCount;        // 重连次数
        qint64 totalDowntime;             // 总断线时间 (ms)
    };
    ReceiverStats getStats() const { return m_stats; }

signals:
    void connected();
    void disconnected();
    void frameReceived(const QByteArray &frameData);
    void frameReceivedWithTimestamp(const QByteArray &frameData, qint64 captureTimestamp);
    void mousePositionReceived(const QPoint &position, qint64 timestamp); // 新增：鼠标位置信号
    void connectionError(const QString &error);
    void connectionStatusChanged(const QString &status);
    void statsUpdated(const ReceiverStats &stats);
    
    // 瓦片相关信号
    void tileMetadataReceived(const TileMetadata &metadata);
    void tileChunkReceived(const TileChunk &chunk);
    void tileUpdateReceived(const TileUpdate &update);
    void tileCompleted(int tileId, const QByteArray &completeData);
    void tileDataLost(int tileId, const QSet<int> &missingChunks);
    void retransmissionRequested(int tileId, const QSet<int> &missingChunks);

    // 新增：音频帧信号（PCM）
    void audioFrameReceived(const QByteArray &pcmData, int sampleRate, int channels, int bitsPerSample, qint64 timestamp);
    void audioGainChanged(int percent);
    void avatarUpdateReceived(const QString &userId, int iconId);

private slots:
    void onConnected();
    void onDisconnected();
    void onBinaryMessageReceived(const QByteArray &message);
    void onTextMessageReceived(const QString &message);
    void onError(QAbstractSocket::SocketError error);
    void onSslErrors(const QList<QSslError> &errors);
    void attemptReconnect();
    void updateStats();

private:
    void setupWebSocket();
    void startReconnectTimer();
    void stopReconnectTimer();
    
    // 瓦片处理方法
    void processTileMessage(const QJsonObject &header, const QByteArray &binaryData);
    void handleTileMetadata(const QJsonObject &header);
    void handleTileData(const QJsonObject &header, const QByteArray &data);
    void handleTileUpdate(const QJsonObject &header, const QByteArray &data);
    void handleTileComplete(const QJsonObject &header);
    void assembleTile(int tileId);
    void checkTileTimeout();
    void requestRetransmission(int tileId, const QSet<int> &missingChunks);
    
    QWebSocket *m_webSocket;
    QString m_serverUrl;
    bool m_connected;
    bool m_reconnectEnabled;
    
    // 重连机制
    QTimer *m_reconnectTimer;
    int m_reconnectAttempts;
    int m_maxReconnectAttempts;
    int m_reconnectInterval; // 毫秒
    
    // 统计信息
    ReceiverStats m_stats;
    QTimer *m_statsTimer;
    qint64 m_connectionStartTime;
    QList<int> m_frameSizes;
    
    // 性能监控相关变量
    QList<qint64> m_assemblyTimes;        // 瓦片组装时间记录
    QList<qint64> m_transmissionTimes;    // 传输时间记录
    QList<qint64> m_latencyMeasurements;  // 延迟测量记录
    QHash<int, qint64> m_tileStartTimes;  // 瓦片开始接收时间
    QHash<int, qint64> m_chunkTimestamps; // 数据块时间戳
    qint64 m_lastStatsUpdateTime;         // 上次统计更新时间
    qint64 m_totalDowntimeStart;          // 断线开始时间
    quint64 m_totalChunksReceived;        // 总接收数据块数
    quint64 m_totalChunksLost;            // 总丢失数据块数
    quint64 m_totalTileBytes;             // 总瓦片字节数
    
    // 瓦片缓存和管理
    QHash<int, TileCache> m_tileCache; // tileId -> TileCache
    QTimer *m_tileTimeoutTimer;
    int m_tileTimeoutMs;
    int m_maxRetransmissionAttempts;
    QHash<int, int> m_retransmissionCounts; // tileId -> attempt count

    QMutex m_mutex;
    QMutex m_tileMutex;

    // 最近一次观看请求信息，用于重连后自动重发
    QString m_lastViewerId;
    QString m_lastTargetId;
    bool m_autoResendWatchRequest = true;
    QString m_lastViewerName;

    // 音频：Opus 解码器状态
    OpusDecoder *m_opusDecoder = nullptr;
    int m_opusSampleRate = 16000;
    int m_opusChannels = 1;
    bool m_opusInitialized = false;
    // 简易抖动缓冲（20ms节拍定时解码）
    QTimer *m_audioTimer = nullptr;
    QQueue<QByteArray> m_opusQueue;
    int m_audioFrameSamples = 0; // 每帧采样数（20ms）
    qint64 m_audioLastTimestamp = 0;
    int m_audioPrebufferFrames = 10;
    int m_audioTargetBufferFrames = 15;
    int m_audioMinBufferFrames = 6;
    int m_audioMaxBufferFrames = 30;
    int m_audioUnderflowCount = 0;
    void initOpusDecoderIfNeeded(int sampleRate, int channels);
    QAudioSource *m_localAudioSource = nullptr;
    QIODevice *m_localAudioInput = nullptr;
    QTimer *m_localAudioTimer = nullptr;
    OpusEncoder *m_localOpusEnc = nullptr;
    int m_localOpusSampleRate = 16000;
    int m_localOpusFrameSize = 16000 / 50;
    int m_localMicGainPercent = 100;
    bool m_followSystemInput = true;
    QString m_localInputDeviceId;
    QAudioFormat m_localInputFormat;
    QByteArray m_currentInputDeviceId;
    QTimer *m_inputPollTimer = nullptr;
    QByteArray m_rawInputBuffer;
    bool m_talkActive = false;
    void recreateLocalAudioSource();
    void handleLocalAudioState(QAudio::State st);
    int bytesPerSample(QAudioFormat::SampleFormat f) const;
    bool produceOpusFrame(QByteArray &out);

    // 文本瓦片消息兼容开关（默认关闭，通过环境变量 IRULER_TEXT_TILE=1 开启）
    bool m_textTileEnabled = false;
};

#endif // WEBSOCKETRECEIVER_H