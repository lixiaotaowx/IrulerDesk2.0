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

class WebSocketReceiver : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketReceiver(QObject *parent = nullptr);
    ~WebSocketReceiver();
    
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
    
    QMutex m_mutex;
};

#endif // WEBSOCKETRECEIVER_H