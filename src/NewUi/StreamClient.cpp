#include "StreamClient.h"
#include <QBuffer>

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

void StreamClient::sendFrame(const QPixmap &pixmap)
{
    if (!m_isConnected) return;

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    // Save as JPG for smaller size, quality 75
    pixmap.save(&buffer, "JPG", 75); 
    
    // In a real scenario, we might want to wrap this in a protocol (e.g., JSON header + binary body)
    // For now, based on previous stage 1, we just send binary data or simple text? 
    // The server expects binary data for frames in the basic implementation usually.
    // Let's check the server implementation if needed, but for now assuming binary send is okay.
    
    m_webSocket->sendBinaryMessage(bytes);
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
    emit connected();
}

void StreamClient::onDisconnected()
{
    m_isConnected = false;
    emit disconnected();
}

void StreamClient::onTextMessageReceived(const QString &message)
{
    Q_UNUSED(message);
}

void StreamClient::onBinaryMessageReceived(const QByteArray &message)
{
    // Handle received binary data (e.g. video frames from other streams)
    QPixmap pixmap;
    if (pixmap.loadFromData(message)) {
        emit frameReceived(pixmap);
    }
}

void StreamClient::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    emit errorOccurred(m_webSocket->errorString());
}
