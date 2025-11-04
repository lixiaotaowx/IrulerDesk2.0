#include "WebSocketSender.h"
#include <QDebug>
#include <QMutexLocker>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

WebSocketSender::WebSocketSender(QObject *parent)
    : QObject(parent)
    , m_webSocket(nullptr)
    , m_connected(false)
    , m_isStreaming(false)  // 默认不推流
    , m_reconnectTimer(nullptr)
    , m_reconnectAttempts(0)
    , m_maxReconnectAttempts(10)
    , m_reconnectInterval(1000)
    , m_totalBytesSent(0)
    , m_totalFramesSent(0)
{
    setupWebSocket();
    
    // 设置重连定时器
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &WebSocketSender::attemptReconnect);
    
    qDebug() << "[WebSocketSender] WebSocket发送器已创建";
}

WebSocketSender::~WebSocketSender()
{
    disconnectFromServer();
    qDebug() << "[WebSocketSender] WebSocket发送器已销毁";
}

void WebSocketSender::setupWebSocket()
{
    if (m_webSocket) {
        m_webSocket->deleteLater();
    }
    
    m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    
    // 连接信号
    connect(m_webSocket, &QWebSocket::connected, this, &WebSocketSender::onConnected);
    connect(m_webSocket, &QWebSocket::disconnected, this, &WebSocketSender::onDisconnected);
    connect(m_webSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &WebSocketSender::onError);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &WebSocketSender::onTextMessageReceived);
}

bool WebSocketSender::connectToServer(const QString &url)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_connected) {
        qWarning() << "[WebSocketSender] 已经连接到服务器";
        return true;
    }
    
    m_serverUrl = url;
    qDebug() << "[WebSocketSender] 连接到WebSocket服务器:" << url;
    
    m_webSocket->open(QUrl(url));
    return true;
}

void WebSocketSender::disconnectFromServer()
{
    QMutexLocker locker(&m_mutex);
    
    stopReconnectTimer();
    
    if (m_webSocket && m_connected) {
        qDebug() << "[WebSocketSender] 断开WebSocket连接";
        m_webSocket->close();
    }
}

void WebSocketSender::sendFrame(const QByteArray &frameData)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_connected && m_webSocket && m_isStreaming) {
        qint64 bytesSent = m_webSocket->sendBinaryMessage(frameData);
        if (bytesSent > 0) {
            m_totalBytesSent += bytesSent;
            m_totalFramesSent++;
            
            emit frameSent(frameData.size());
            
            // 移除频繁的帧发送统计输出以提升性能
            // if (m_totalFramesSent % 100 == 0) {
            //     qDebug() << "[WebSocketSender] 已发送" << m_totalFramesSent << "帧，总字节数:" << m_totalBytesSent;
            // }
        } else {
            qDebug() << "[WebSocketSender] 发送帧数据失败";
        }
    } else {
        qDebug() << "[WebSocketSender] WebSocket未连接，无法发送帧数据";
    }
}

void WebSocketSender::sendTextMessage(const QString &message)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_connected && m_webSocket) {
        qint64 bytesSent = m_webSocket->sendTextMessage(message);
        if (bytesSent > 0) {
            // 移除频繁的文本消息统计输出以提升性能
            // static int textMessageCount = 0;
            // textMessageCount++;
            // if (textMessageCount % 100 == 0) {
            //     qDebug() << "[WebSocketSender] 已发送" << textMessageCount << "条文本消息";
            // }
        } else {
            qDebug() << "[WebSocketSender] 发送文本消息失败:" << message.left(50);
        }
    } else {
        qDebug() << "[WebSocketSender] WebSocket未连接，无法发送文本消息";
    }
}

void WebSocketSender::onConnected()
{
    QMutexLocker locker(&m_mutex);
    
    m_connected = true;
    m_reconnectAttempts = 0;
    stopReconnectTimer();
    
    qDebug() << "[WebSocketSender] 已连接到WebSocket服务器:" << m_serverUrl;
    emit connected();
}

void WebSocketSender::onDisconnected()
{
    QMutexLocker locker(&m_mutex);
    
    bool wasConnected = m_connected;
    m_connected = false;
    
    if (wasConnected) {
        qWarning() << "[WebSocketSender] 与WebSocket服务器断开连接";
        emit disconnected();
        
        // 启动重连
        startReconnectTimer();
    }
}

void WebSocketSender::onError(QAbstractSocket::SocketError socketError)
{
    QString errorString = m_webSocket->errorString();
    qCritical() << "[WebSocketSender] WebSocket错误:" << socketError << "-" << errorString;
    
    emit error(QString("WebSocket连接错误: %1").arg(errorString));
    
    // 连接失败时也尝试重连
    if (!m_connected) {
        startReconnectTimer();
    }
}

void WebSocketSender::attemptReconnect()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_connected || m_serverUrl.isEmpty()) {
        return;
    }
    
    m_reconnectAttempts++;
    if (m_reconnectAttempts > m_maxReconnectAttempts) {
        qCritical() << "[WebSocketSender] 重连失败，已达到最大重连次数:" << m_maxReconnectAttempts;
        emit error("WebSocket重连失败，已达到最大重连次数");
        return;
    }
    
    qDebug() << "[WebSocketSender] 尝试重连到服务器，第" << m_reconnectAttempts << "次";
    
    // 重新设置WebSocket
    setupWebSocket();
    m_webSocket->open(QUrl(m_serverUrl));
}

void WebSocketSender::startReconnectTimer()
{
    if (m_reconnectTimer && !m_reconnectTimer->isActive()) {
        int delay = m_reconnectInterval * (1 + m_reconnectAttempts); // 递增延迟
        m_reconnectTimer->start(delay);
        qDebug() << "[WebSocketSender] 启动重连定时器，延迟" << delay << "ms";
    }
}

void WebSocketSender::stopReconnectTimer()
{
    if (m_reconnectTimer && m_reconnectTimer->isActive()) {
        m_reconnectTimer->stop();
        qDebug() << "[WebSocketSender] 停止重连定时器";
    }
}

void WebSocketSender::startStreaming()
{
    QMutexLocker locker(&m_mutex);
    if (!m_isStreaming) {
        m_isStreaming = true;
        qDebug() << "[WebSocketSender] 开始推流";
        emit streamingStarted();
    }
}

void WebSocketSender::stopStreaming()
{
    QMutexLocker locker(&m_mutex);
    if (m_isStreaming) {
        m_isStreaming = false;
        qDebug() << "[WebSocketSender] 停止推流";
        emit streamingStopped();
    }
}

void WebSocketSender::onTextMessageReceived(const QString &message)
{
    qDebug() << "[WebSocketSender] 收到文本消息:" << message;
    
    // 解析JSON消息
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        qDebug() << "[WebSocketSender] JSON解析失败:" << error.errorString();
        return;
    }
    
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();
    
    if (type == "start_streaming") {
        qDebug() << "[WebSocketSender] 收到开始推流请求";
        startStreaming();
    } else if (type == "stop_streaming") {
        qDebug() << "[WebSocketSender] 收到停止推流请求";
        stopStreaming();
    }
}