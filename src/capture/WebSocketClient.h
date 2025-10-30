#ifndef WEBSOCKETCLIENT_H
#define WEBSOCKETCLIENT_H

#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QUrl>
#include <QByteArray>
#include <QMutex>
#include <QDebug>

class WebSocketClient : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketClient(QObject *parent = nullptr);
    ~WebSocketClient();
    
    // 连接到WebSocket服务器
    bool connectToServer(const QString &url);
    
    // 断开连接
    void disconnectFromServer();
    
    // 发送帧数据
    void sendFrame(const QByteArray &frameData);
    
    // 检查连接状态
    bool isConnected() const;
    
    // 获取发送统计信息
    struct ClientStats {
        quint64 totalFrames;
        quint64 totalBytes;
        double averageFrameSize;
        qint64 connectionTime;
    };
    ClientStats getStats() const { return m_stats; }

signals:
    void connected();
    void disconnected();
    void frameSent(const QByteArray &frameData);
    void connectionError(const QString &error);
    void statsUpdated(const ClientStats &stats);

private slots:
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError error);
    void updateStats();

private:
    QWebSocket *m_webSocket;
    bool m_connected;
    QString m_serverUrl;
    
    // 统计信息
    ClientStats m_stats;
    QTimer *m_statsTimer;
    mutable QMutex m_mutex;
    qint64 m_connectionStartTime;
    
    void setupWebSocket();
};

#endif // WEBSOCKETCLIENT_H