#include "WebSocketClient.h"
#include <QDateTime>
#include <QMutexLocker>

WebSocketClient::WebSocketClient(QObject *parent)
    : QObject(parent)
    , m_webSocket(nullptr)
    , m_connected(false)
    , m_connectionStartTime(0)
{
    setupWebSocket();
    
    memset(&m_stats, 0, sizeof(m_stats));
    
    // 设置统计更新定时器
    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &WebSocketClient::updateStats);
    m_statsTimer->start(1000); // 每秒更新一次统计
    
    qDebug() << "[WebSocketClient] WebSocket客户端已创建";
}

WebSocketClient::~WebSocketClient()
{
    disconnectFromServer();
    qDebug() << "[WebSocketClient] WebSocket客户端已销毁";
}

void WebSocketClient::setupWebSocket()
{
    if (m_webSocket) {
        m_webSocket->deleteLater();
    }
    
    m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    
    // 连接信号
    connect(m_webSocket, &QWebSocket::connected, this, &WebSocketClient::onConnected);
    connect(m_webSocket, &QWebSocket::disconnected, this, &WebSocketClient::onDisconnected);
    connect(m_webSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &WebSocketClient::onError);
}

bool WebSocketClient::connectToServer(const QString &url)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_connected) {
        qWarning() << "[WebSocketClient] 已经连接到服务器";
        return true;
    }
    
    m_serverUrl = url;
    qDebug() << "[WebSocketClient] 连接到WebSocket服务器:" << url;
    
    m_connectionStartTime = QDateTime::currentMSecsSinceEpoch();
    m_webSocket->open(QUrl(url));
    
    return true;
}

void WebSocketClient::disconnectFromServer()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_webSocket && m_connected) {
        qDebug() << "[WebSocketClient] 断开WebSocket连接";
        m_webSocket->close();
    }
}

void WebSocketClient::sendFrame(const QByteArray &frameData)
{
    if (!m_connected || frameData.isEmpty()) {
        return;
    }
    
    // 调试：输出发送数据的前8字节
    static int frameCount = 0;
    if (frameCount < 5) { // 只输出前5帧的详细信息
        // qDebug() << "[WebSocketClient] 发送帧" << frameCount << "数据大小:" << frameData.size() << "字节"; // 已禁用以提升性能
        if (frameData.size() >= 8) {
            QString hexData;
            for (int i = 0; i < qMin(8, frameData.size()); i++) {
                hexData += QString("%1 ").arg(static_cast<uint8_t>(frameData[i]), 2, 16, QChar('0'));
            }
            qDebug() << "[WebSocketClient] 前8字节:" << hexData;
        }
        frameCount++;
    }
    
    // 发送二进制数据
    qint64 bytesSent = m_webSocket->sendBinaryMessage(frameData);
    
    if (bytesSent > 0) {
        // 更新统计信息
        {
            QMutexLocker locker(&m_mutex);
            m_stats.totalFrames++;
            m_stats.totalBytes += frameData.size();
        }
        
        emit frameSent(frameData);
    }
}

bool WebSocketClient::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_connected;
}

void WebSocketClient::onConnected()
{
    QMutexLocker locker(&m_mutex);
    
    m_connected = true;
    m_stats.connectionTime = QDateTime::currentMSecsSinceEpoch() - m_connectionStartTime;
    
    qDebug() << "[WebSocketClient] WebSocket连接已建立";
    emit connected();
}

void WebSocketClient::onDisconnected()
{
    QMutexLocker locker(&m_mutex);
    
    bool wasConnected = m_connected;
    m_connected = false;
    
    qDebug() << "[WebSocketClient] WebSocket连接已断开";
    
    if (wasConnected) {
        emit disconnected();
    }
}

void WebSocketClient::onError(QAbstractSocket::SocketError error)
{
    QString errorString = m_webSocket->errorString();
    qWarning() << "[WebSocketClient] WebSocket错误:" << error << errorString;
    
    emit connectionError(errorString);
}

void WebSocketClient::updateStats()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_stats.totalFrames > 0) {
        m_stats.averageFrameSize = static_cast<double>(m_stats.totalBytes) / m_stats.totalFrames;
    }
    
    emit statsUpdated(m_stats);
}