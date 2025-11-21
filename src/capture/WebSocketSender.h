#ifndef WEBSOCKETSENDER_H
#define WEBSOCKETSENDER_H

#include <QObject>
#include <QWebSocket>
#include <QByteArray>
#include <QTimer>
#include <QMutex>
#include <QVector>

// 前向声明
struct TileInfo;

class WebSocketSender : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketSender(QObject *parent = nullptr);
    ~WebSocketSender();
    
    bool connectToServer(const QString &url);
    void disconnectFromServer();
    
    void sendFrame(const QByteArray &frameData);
    void sendTextMessage(const QString &message); // 新增：发送文本消息
    
    // 瓦片数据传输方法
    void sendTileData(const QVector<int> &tileIndices, const QByteArray &serializedData);
    void sendTileUpdate(const QVector<TileInfo> &updatedTiles, const QVector<QByteArray> &tileImages);
    void sendTileMetadata(const QVector<TileInfo> &allTiles);
    

    
    // 推流控制
    void startStreaming();
    void stopStreaming();
    bool isStreaming() const { return m_isStreaming; }
    
    // 状态查询
    bool isConnected() const { return m_connected; }
    QString getServerUrl() const { return m_serverUrl; }
    QString viewerName() const { return m_viewerName; }
    
    // 统计信息
    qint64 getTotalBytesSent() const { return m_totalBytesSent; }
    qint64 getTotalFramesSent() const { return m_totalFramesSent; }
    qint64 getTotalTilesSent() const { return m_totalTilesSent; }
    qint64 getTotalTileDataSent() const { return m_totalTileDataSent; }
    
    // 性能统计结构
    struct SenderStats {
        quint64 totalTilesSent;               // 总发送瓦片数
        quint64 totalBytesSent;               // 总发送字节数
        quint64 totalEncodingOperations;      // 总编码操作数
        qint64 totalEncodingTime;             // 总编码时间 (ms)
        qint64 totalSendingTime;              // 总发送时间 (ms)
        qint64 totalSerializationTime;        // 总序列化时间 (ms)
        qint64 averageEncodingTime;           // 平均编码时间 (ms)
        qint64 averageSendingTime;            // 平均发送时间 (ms)
        qint64 averageSerializationTime;      // 平均序列化时间 (ms)
        qint64 maxEncodingTime;               // 最大编码时间 (ms)
        qint64 minEncodingTime;               // 最小编码时间 (ms)
        qint64 maxSendingTime;                // 最大发送时间 (ms)
        qint64 minSendingTime;                // 最小发送时间 (ms)
        double sendingRate;                   // 发送速率 (bytes/s)
        double tileTransmissionRate;          // 瓦片传输速率 (tiles/s)
        qint64 lastSendTime;                  // 上次发送时间戳
        quint64 reconnectionCount;            // 重连次数
        qint64 totalDowntime;                 // 总断线时间 (ms)
    };
    
    // 获取性能统计
    SenderStats getSenderStats() const;
    void resetSenderStats();
    // 控制是否启用统计（默认关闭）
    void setStatsEnabled(bool enabled) { m_enableStats = enabled; }
    bool isStatsEnabled() const { return m_enableStats; }
    
signals:
    void connected();
    void disconnected();
    void frameSent(int frameSize);
    void error(const QString &errorMessage);
    void requestKeyFrame(); // 请求编码器生成关键帧
    void streamingStarted(); // 开始推流信号
    void streamingStopped(); // 停止推流信号
    
    // 瓦片相关信号
    void tileDataSent(int tileCount, int dataSize);
    void tileUpdateSent(int updatedTileCount);
    void tileMetadataSent(int totalTileCount);

    // 远程批注事件（观看端发来），包含颜色ID
    void annotationEventReceived(const QString &phase, int x, int y, const QString &viewerId, int colorId);
    void textAnnotationReceived(const QString &text, int x, int y, const QString &viewerId, int colorId, int fontSize);
    void likeRequested(const QString &viewerId);
    // 切换屏幕请求（观看端发来）
    void switchScreenRequested(const QString &direction, int index);
    // 质量变更请求（观看端发来）
    void qualityChangeRequested(const QString &quality);
    // 音频测试开关请求（观看端发来）
    void audioToggleRequested(bool enabled);
    void audioGainRequested(int percent);
    void viewerAudioOpusReceived(const QByteArray &opusData, int sampleRate, int channels, int frameSamples, qint64 timestamp);
    void viewerNameChanged(const QString &name);
    void viewerCursorReceived(const QString &viewerId, int x, int y, const QString &viewerName);
    void viewerNameUpdateReceived(const QString &viewerId, const QString &viewerName);

private slots:
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError socketError);
    void onTextMessageReceived(const QString &message); // 处理文本消息
    void attemptReconnect();

private:
    void setupWebSocket();
    void startReconnectTimer();
    void stopReconnectTimer();
    
    // 性能统计更新方法
    void updateSenderStats(qint64 encodingTime, qint64 sendingTime, qint64 serializationTime, qint64 bytesSent, int tilesSent);
    

    
// WebSocket客户端
    QWebSocket *m_webSocket;
    QString m_serverUrl;
    bool m_connected;
    bool m_isStreaming;  // 推流状态
    
    // 重连机制
    QTimer *m_reconnectTimer;
    int m_reconnectAttempts;
    int m_maxReconnectAttempts;
    int m_reconnectInterval;
    
    // 统计信息
    qint64 m_totalBytesSent;
    qint64 m_totalFramesSent;
    qint64 m_totalTilesSent;      // 瓦片发送统计
    qint64 m_totalTileDataSent;   // 瓦片数据字节统计
    
    // 性能监控相关
    SenderStats m_senderStats;                  // 性能统计数据
    QVector<qint64> m_encodingTimes;            // 最近的编码时间记录 (保留最近100次)
    QVector<qint64> m_sendingTimes;             // 最近的发送时间记录
    QVector<qint64> m_serializationTimes;       // 最近的序列化时间记录
    qint64 m_lastStatsUpdateTime;               // 上次统计更新时间
    qint64 m_disconnectStartTime;               // 断线开始时间
    bool m_enableStats = false;                 // 统计开关（默认关闭）
    
    // 线程安全
    mutable QMutex m_mutex;
    QString m_viewerName;
};

#endif // WEBSOCKETSENDER_H