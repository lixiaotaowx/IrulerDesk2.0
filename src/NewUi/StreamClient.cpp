#include "StreamClient.h"
#include <QBuffer>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

StreamClient::StreamClient(QObject *parent)
    : QObject(parent)
    , m_webSocket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , m_isConnected(false)
{
    connect(m_webSocket, &QWebSocket::connected, this, &StreamClient::onConnected);
    connect(m_webSocket, &QWebSocket::disconnected, this, &StreamClient::onDisconnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &StreamClient::onTextMessageReceived);
    connect(m_webSocket, &QWebSocket::binaryMessageReceived, this, &StreamClient::onBinaryMessageReceived);
    connect(m_webSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred),
            this, &StreamClient::onError);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &StreamClient::attemptReconnect);
}

StreamClient::~StreamClient()
{
    if (m_webSocket) {
        m_shouldReconnect = false;
        if (m_reconnectTimer) {
            m_reconnectTimer->stop();
        }
        m_webSocket->close();
    }
}

void StreamClient::connectToServer(const QUrl &url)
{
    m_lastUrl = url;
    m_shouldReconnect = true;
    m_reconnectAttempts = 0;
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }
    emit logMessage(QStringLiteral("[StreamClient] connect %1").arg(url.toString()));
    m_webSocket->open(url);
}

void StreamClient::disconnectFromServer()
{
    m_shouldReconnect = false;
    m_reconnectAttempts = 0;
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }
    emit logMessage(QStringLiteral("[StreamClient] disconnect"));
    m_webSocket->close();
}

void StreamClient::setJpegQuality(int quality)
{
    m_jpegQuality = qBound(0, quality, 100);
}

void StreamClient::sendFrame(const QPixmap &pixmap, bool force)
{
    if (!m_isConnected) return;

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!pixmap.save(&buffer, "JPG", m_jpegQuality)) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 resendSameFrameIntervalMs = 20000;
    if (!force && !m_lastSentBytes.isEmpty() && bytes == m_lastSentBytes && (nowMs - m_lastSentAtMs) < resendSameFrameIntervalMs) {
        return;
    }

    const qint64 sent = m_webSocket->sendBinaryMessage(bytes);
    if (sent > 0) {
        m_lastSentBytes = bytes;
        m_lastSentAtMs = nowMs;
    }
}

qint64 StreamClient::sendTextMessage(const QString &message)
{
    if (!m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState) {
        emit logMessage(QStringLiteral("[StreamClient] sendTextMessage skipped: not connected"));
        return -1;
    }
    emit logMessage(QStringLiteral("[StreamClient] sendTextMessage %1").arg(message));
    return m_webSocket->sendTextMessage(message);
}

bool StreamClient::isConnected() const
{
    return m_isConnected;
}

void StreamClient::onConnected()
{
    m_isConnected = true;
    m_lastSentBytes.clear();
    m_lastReceivedBytes.clear();
    m_lastSentAtMs = 0;
    emit logMessage(QStringLiteral("[StreamClient] connected"));
    emit connected();
}

void StreamClient::onDisconnected()
{
    m_isConnected = false;
    emit logMessage(QStringLiteral("[StreamClient] disconnected"));
    emit disconnected();
    scheduleReconnect();
}

void StreamClient::onTextMessageReceived(const QString &message)
{
    const QString trimmed = message.trimmed();
    if (trimmed == QStringLiteral("start_streaming") || trimmed == QStringLiteral("start_streaming_request")) {
        emit logMessage(QStringLiteral("[StreamClient] rx start_streaming"));
        emit startStreamingRequested();
        return;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    const QJsonObject obj = doc.object();
    const QString type = obj.value("type").toString();
    if (type == QStringLiteral("start_streaming") || type == QStringLiteral("start_streaming_request")) {
        emit logMessage(QStringLiteral("[StreamClient] rx start_streaming"));
        emit startStreamingRequested();
    } else if (type == QStringLiteral("hover_stream")) {
        const QString targetId = obj.value("target_id").toString();
        const QString channelId = obj.value("channel_id").toString();
        const int fps = obj.value("fps").toInt(10);
        const bool enabled = obj.value("enabled").toBool(true);
        if (!channelId.isEmpty()) {
            emit logMessage(QStringLiteral("[StreamClient] rx hover_stream target_id=%1 channel_id=%2 fps=%3 enabled=%4")
                                .arg(targetId, channelId)
                                .arg(fps)
                                .arg(enabled ? QStringLiteral("true") : QStringLiteral("false")));
            emit hoverStreamRequested(targetId, channelId, fps, enabled);
        }
    }
}

void StreamClient::onBinaryMessageReceived(const QByteArray &message)
{
    // Handle received binary data (e.g. video frames from other streams)
    if (!m_lastReceivedBytes.isEmpty() && message == m_lastReceivedBytes) {
        return;
    }
    QPixmap pixmap;
    if (pixmap.loadFromData(message)) {
        m_lastReceivedBytes = message;
        emit frameReceived(pixmap);
    }
}

void StreamClient::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    emit logMessage(QStringLiteral("[StreamClient] error %1").arg(m_webSocket->errorString()));
    emit errorOccurred(m_webSocket->errorString());
    scheduleReconnect();
}

void StreamClient::scheduleReconnect()
{
    if (!m_shouldReconnect) {
        return;
    }
    if (!m_reconnectTimer || !m_webSocket) {
        return;
    }
    if (!m_lastUrl.isValid() || m_lastUrl.isEmpty()) {
        return;
    }
    const auto st = m_webSocket->state();
    if (st == QAbstractSocket::ConnectedState || st == QAbstractSocket::ConnectingState) {
        return;
    }
    if (m_reconnectTimer->isActive()) {
        return;
    }

    const int attempt = qMax(0, m_reconnectAttempts);
    const int delayMs = qMin(8000, 600 + attempt * 600);
    m_reconnectAttempts = attempt + 1;
    emit logMessage(QStringLiteral("[StreamClient] reconnect scheduled in %1ms url=%2")
                        .arg(delayMs)
                        .arg(m_lastUrl.toString()));
    m_reconnectTimer->start(delayMs);
}

void StreamClient::attemptReconnect()
{
    if (!m_shouldReconnect || !m_webSocket) {
        return;
    }
    if (!m_lastUrl.isValid() || m_lastUrl.isEmpty()) {
        return;
    }
    const auto st = m_webSocket->state();
    if (st == QAbstractSocket::ConnectedState || st == QAbstractSocket::ConnectingState) {
        return;
    }
    emit logMessage(QStringLiteral("[StreamClient] reconnect %1").arg(m_lastUrl.toString()));
    m_webSocket->open(m_lastUrl);
}
