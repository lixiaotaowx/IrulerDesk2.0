#pragma once

#include <QObject>
#include <QWebSocket>
#include <QPixmap>
#include <QUrl>
#include <QByteArray>

class StreamClient : public QObject {
    Q_OBJECT
public:
    explicit StreamClient(QObject *parent = nullptr);
    ~StreamClient();

    void connectToServer(const QUrl &url);
    void disconnectFromServer();
    void setJpegQuality(int quality);
    void sendFrame(const QPixmap &pixmap, bool force = false);
    qint64 sendTextMessage(const QString &message);
    bool isConnected() const;

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &errorMsg);
    void logMessage(const QString &msg);
    void frameReceived(const QPixmap &frame);
    void startStreamingRequested();
    void hoverStreamRequested(const QString &targetId, const QString &channelId, int fps, bool enabled);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString &message);
    void onBinaryMessageReceived(const QByteArray &message);
    void onError(QAbstractSocket::SocketError error);

private:
    QWebSocket *m_webSocket;
    bool m_isConnected;
    QByteArray m_lastSentBytes;
    qint64 m_lastSentAtMs = 0;
    QByteArray m_lastReceivedBytes;
    int m_jpegQuality = 30;
};
