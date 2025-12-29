#pragma once

#include <QObject>
#include <QWebSocket>
#include <QPixmap>
#include <QUrl>
#include <QByteArray>
#include <QTimer>
#include <QJsonObject>

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
    void attemptReconnect();

private:
    void scheduleReconnect();
    void requestLanOfferIfNeeded();
    void handleLanOfferMessage(const QJsonObject &obj);
    QString roomIdFromUrl(const QUrl &url) const;
    bool isSubscribeUrl(const QUrl &url) const;
    bool isPublishUrl(const QUrl &url) const;
    void startLanFallbackTimer();
    void stopLanFallbackTimer();

    QWebSocket *m_webSocket;
    bool m_isConnected;
    QByteArray m_lastSentBytes;
    qint64 m_lastSentAtMs = 0;
    QByteArray m_lastReceivedBytes;
    int m_jpegQuality = 30;
    QTimer *m_reconnectTimer = nullptr;
    QUrl m_lastUrl;
    QUrl m_pendingOpenUrl;
    bool m_hasPendingOpen = false;
    bool m_manualSwitchClose = false;
    bool m_shouldReconnect = false;
    int m_reconnectAttempts = 0;

    QTimer *m_lanFallbackTimer = nullptr;
    QUrl m_cloudFallbackUrl;
    bool m_lanSwitchInProgress = false;
    QTimer *m_lanFirstFrameTimer = nullptr;
    bool m_lanAwaitingFirstFrame = false;
    qint64 m_lanSwitchDisabledUntilMs = 0;
    QTimer *m_lanOfferRetryTimer = nullptr;
    int m_lanOfferRetryCount = 0;
    qint64 m_lastLanOfferRequestAtMs = 0;

    quint64 m_txFrames = 0;
    quint64 m_rxFrames = 0;
    qint64 m_lastTxLogAtMs = 0;
    qint64 m_lastRxLogAtMs = 0;
    qint64 m_lastTxSkipLogAtMs = 0;
    qint64 m_lastRxSkipLogAtMs = 0;
    qint64 m_lastDecodeFailLogAtMs = 0;
};
