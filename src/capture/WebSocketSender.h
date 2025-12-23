#ifndef WEBSOCKETSENDER_H
#define WEBSOCKETSENDER_H

#include <QObject>
#include <QWebSocket>
#include <QByteArray>
#include <QTimer>
#include <QMutex>
#include <QVector>
#include <QQueue>

// 前向声明

class WebSocketSender : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketSender(QObject *parent = nullptr);
    ~WebSocketSender();
    
    bool connectToServer(const QString &url);
    void disconnectFromServer();
    
    void sendFrame(const QByteArray &frameData);
    void enqueueFrame(const QByteArray &frameData, bool keyFrame);
    void sendTextMessage(const QString &message); // 新增：发送文本消息
    
    // 推流控制
    void startStreaming();
    void stopStreaming(bool softStop = false);
    void forceKeyFrame();
    bool isStreaming() const { return m_isStreaming; }
    bool isAudioOnlyStreaming() const { return m_audioOnlyStreaming; }

    // 状态查询
    bool isConnected() const { return m_connected; }
    QString getServerUrl() const { return m_serverUrl; }
    QString viewerName() const { return m_viewerName; }
    
    // 统计信息
    qint64 getTotalBytesSent() const { return m_totalBytesSent; }
    qint64 getTotalFramesSent() const { return m_totalFramesSent; }
    
    // 性能统计结构
    struct SenderStats {
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
    void streamingStopped(bool softStop); // 停止推流信号
    
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
    void viewerAudioOpusReceived(const QString &viewerId, const QByteArray &opusData, int sampleRate, int channels, int frameSamples, qint64 timestamp);
    void viewerMicStateReceived(const QString &viewerId, bool enabled);
    void viewerNameChanged(const QString &name);
    void viewerCursorReceived(const QString &viewerId, int x, int y, const QString &viewerName);
    void viewerNameUpdateReceived(const QString &viewerId, const QString &viewerName);
    void viewerListenMuteRequested(bool mute);
    void viewerJoined(const QString &viewerId);
    void viewerExited(const QString &viewerId);
    void watchRequestReceived(const QString &viewerId, const QString &viewerName, const QString &targetId, int iconId);

private slots:
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError socketError);
    void onTextMessageReceived(const QString &message); // 处理文本消息
    void attemptReconnect();
    void onSendTimer();

private:
    void setupWebSocket();
    void startReconnectTimer();
    void stopReconnectTimer();
    bool isManualApprovalEnabled() const;
    void sendApprovalRequired(const QString &viewerId, const QString &targetId);
    void sendWatchAccepted(const QString &viewerId, const QString &targetId);
    void sendWatchRejected(const QString &viewerId, const QString &targetId);
public:
    void approveWatchRequest();
    void localApproveWatchRequest();
    void rejectWatchRequest();
    
    // 性能统计更新方法
    void updateSenderStats(qint64 encodingTime, qint64 sendingTime, qint64 serializationTime, qint64 bytesSent);
    
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

    QQueue<QByteArray> m_frameQueue;
    QQueue<bool> m_keyQueue;
    QTimer *m_sendTimer = nullptr;
    int m_maxQueueSize = 6;
    int m_queueMaxAgeMs = 200;
    qint64 m_droppedFramesDueToQueue = 0;
    qint64 m_droppedFramesDueToAge = 0;

    // 手动同意状态
    bool m_waitingForApproval = false;
    QString m_pendingViewerId;
    QString m_pendingTargetId;
    QString m_pendingViewerName;
    int m_pendingIconId = -1;
    bool m_pendingAudioOnly = false;
    bool m_audioOnlyStreaming = false;
};

#endif // WEBSOCKETSENDER_H
