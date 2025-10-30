#ifndef WEBSOCKETSENDER_H
#define WEBSOCKETSENDER_H

#include <QObject>
#include <QWebSocket>
#include <QByteArray>
#include <QTimer>
#include <QMutex>

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
    
    // 推流控制
    void startStreaming();
    void stopStreaming();
    bool isStreaming() const { return m_isStreaming; }
    
    // 状态查询
    bool isConnected() const { return m_connected; }
    QString getServerUrl() const { return m_serverUrl; }
    
    // 统计信息
    qint64 getTotalBytesSent() const { return m_totalBytesSent; }
    qint64 getTotalFramesSent() const { return m_totalFramesSent; }
    
signals:
    void connected();
    void disconnected();
    void frameSent(int frameSize);
    void error(const QString &errorMessage);
    void requestKeyFrame(); // 请求编码器生成关键帧
    void streamingStarted(); // 开始推流信号
    void streamingStopped(); // 停止推流信号

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
    
    // 线程安全
    QMutex m_mutex;
};

#endif // WEBSOCKETSENDER_H