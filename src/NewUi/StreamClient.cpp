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
    m_webSocket->open(url);
}

void StreamClient::disconnectFromServer()
{
    m_webSocket->close();
}

void StreamClient::sendFrame(const QPixmap &pixmap, bool force)
{
    if (!m_isConnected) return;

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!pixmap.save(&buffer, "JPG", 30)) {
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
        return -1;
    }
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
    emit connected();
}

void StreamClient::onDisconnected()
{
    m_isConnected = false;
    emit disconnected();
}

void StreamClient::onTextMessageReceived(const QString &message)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    const QJsonObject obj = doc.object();
    const QString type = obj.value("type").toString();
    if (type == QStringLiteral("start_streaming")) {
        emit startStreamingRequested();
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
    emit errorOccurred(m_webSocket->errorString());
}
