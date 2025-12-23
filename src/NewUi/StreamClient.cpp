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
}

StreamClient::~StreamClient()
{
    if (m_webSocket) {
        m_webSocket->close();
    }
}

void StreamClient::connectToServer(const QUrl &url)
{
    emit logMessage(QStringLiteral("[StreamClient] connect %1").arg(url.toString()));
    m_webSocket->open(url);
}

void StreamClient::disconnectFromServer()
{
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
}
