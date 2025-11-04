#ifndef WEBSOCKETRECEIVER_H
#define WEBSOCKETRECEIVER_H

#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QUrl>
#include <QByteArray>
#include <QMutex>
#include <QDebug>
#include <QPoint>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QMap>
#include <QSet>

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
};

#endif // WEBSOCKETRECEIVER_H