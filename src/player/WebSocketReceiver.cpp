#include "WebSocketReceiver.h"
#include <QDateTime>
#include <QMutexLocker>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPoint>
#include <QSslError>
#include <QNetworkProxy>

WebSocketReceiver::WebSocketReceiver(QObject *parent)
    : QObject(parent)
    , m_webSocket(nullptr)
    , m_connected(false)
    , m_reconnectEnabled(true)
    , m_reconnectTimer(nullptr)
    , m_reconnectAttempts(0)
    , m_maxReconnectAttempts(10)
    , m_reconnectInterval(1000) // 极快重连间隔，最低延迟
    , m_statsTimer(nullptr)
    , m_connectionStartTime(0)
{
    setupWebSocket();
    
    memset(&m_stats, 0, sizeof(m_stats));
    
    // 设置统计更新定时器
    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &WebSocketReceiver::updateStats);
    m_statsTimer->start(1000); // 每秒更新一次统计
    
    // 设置重连定时器
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &WebSocketReceiver::attemptReconnect);
    
    qDebug() << "[WebSocketReceiver] WebSocket接收器已创建";
}

WebSocketReceiver::~WebSocketReceiver()
{
    disconnectFromServer();
    qDebug() << "[WebSocketReceiver] WebSocket接收器已销毁";
}

void WebSocketReceiver::setupWebSocket()
{
    if (m_webSocket) {
        m_webSocket->deleteLater();
    }
    
    m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    
    // 连接信号
    connect(m_webSocket, &QWebSocket::connected, this, &WebSocketReceiver::onConnected);
    connect(m_webSocket, &QWebSocket::disconnected, this, &WebSocketReceiver::onDisconnected);
    connect(m_webSocket, &QWebSocket::binaryMessageReceived, this, &WebSocketReceiver::onBinaryMessageReceived);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &WebSocketReceiver::onTextMessageReceived);
    connect(m_webSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &WebSocketReceiver::onError);
    connect(m_webSocket, &QWebSocket::sslErrors, this, &WebSocketReceiver::onSslErrors);
}

bool WebSocketReceiver::connectToServer(const QString &url)
{
    QMutexLocker locker(&m_mutex);
    
    // 如果已经连接到相同的URL，则不需要重新连接
    if (m_connected && m_serverUrl == url) {
        qDebug() << "[WebSocketReceiver] 已经连接到相同的服务器URL:" << url;
        return true;
    }
    
    // 如果已经连接但URL不同，先断开之前的连接
    if (m_connected) {
        qDebug() << "[WebSocketReceiver] 断开之前的连接，准备连接到新服务器:" << url;
        m_reconnectEnabled = false;
        stopReconnectTimer();
        if (m_webSocket) {
            m_webSocket->close();
        }
        m_connected = false;
    }
    
    // 清理缓存数据，确保切换设备时没有残留
    qDebug() << "[WebSocketReceiver] 清理缓存数据...";
    memset(&m_stats, 0, sizeof(m_stats));
    m_frameSizes.clear();
    m_connectionStartTime = 0;
    
    m_serverUrl = url;
    m_reconnectEnabled = true;
    qDebug() << "[WebSocketReceiver] 连接到WebSocket服务器:" << url;
    
    m_webSocket->open(QUrl(url));
    return true;
}

void WebSocketReceiver::disconnectFromServer()
{
    QMutexLocker locker(&m_mutex);
    
    m_reconnectEnabled = false;
    stopReconnectTimer();
    
    if (m_webSocket && m_connected) {
        qDebug() << "[WebSocketReceiver] 断开WebSocket连接";
        m_webSocket->close();
    }
    
    // 清理缓存数据
    qDebug() << "[WebSocketReceiver] 断开连接时清理缓存数据...";
    memset(&m_stats, 0, sizeof(m_stats));
    m_frameSizes.clear();
    m_connectionStartTime = 0;
    
    m_connected = false;
}

bool WebSocketReceiver::isConnected() const
{
    return m_connected;
}

void WebSocketReceiver::onConnected()
{
    QMutexLocker locker(&m_mutex);
    
    m_connected = true;
    m_reconnectAttempts = 0;
    m_connectionStartTime = QDateTime::currentMSecsSinceEpoch();
    stopReconnectTimer();
    
    qDebug() << "[WebSocketReceiver] WebSocket连接已建立";
    qDebug() << "[WebSocketReceiver] 连接URL:" << m_serverUrl;
    qDebug() << "[WebSocketReceiver] 本地地址:" << m_webSocket->localAddress().toString() << ":" << m_webSocket->localPort();
    qDebug() << "[WebSocketReceiver] 对端地址:" << m_webSocket->peerAddress().toString() << ":" << m_webSocket->peerPort();
    emit connected();
    emit connectionStatusChanged("已连接");
}

void WebSocketReceiver::onDisconnected()
{
    QMutexLocker locker(&m_mutex);
    
    bool wasConnected = m_connected;
    m_connected = false;
    
    qDebug() << "[WebSocketReceiver] WebSocket连接已断开";
    qDebug() << "[WebSocketReceiver] 关闭代码:" << m_webSocket->closeCode() << "原因:" << m_webSocket->closeReason();
    qDebug() << "[WebSocketReceiver] 当前状态:" << m_webSocket->state() << "连接URL:" << m_serverUrl;
    if (m_connectionStartTime > 0) {
        qint64 duration = QDateTime::currentMSecsSinceEpoch() - m_connectionStartTime;
        qDebug() << "[WebSocketReceiver] 本次连接时长(毫秒):" << duration;
    }
    
    if (wasConnected) {
        emit disconnected();
        emit connectionStatusChanged("已断开");
    }
    
    // 如果启用了重连，开始重连尝试
    if (m_reconnectEnabled && m_reconnectAttempts < m_maxReconnectAttempts) {
        startReconnectTimer();
    }
}

void WebSocketReceiver::onBinaryMessageReceived(const QByteArray &message)
{
    // 立即记录收到二进制消息的事实
    static int binaryMessageCount = 0;
    binaryMessageCount++;
    // 移除每帧打印，避免影响性能
    // qDebug() << "[WebSocketReceiver] [重要] 收到第" << binaryMessageCount << "个二进制消息，大小:" << message.size() << "字节";
    
    if (message.isEmpty() || message.size() < 8) {
        qDebug() << "[WebSocketReceiver] [错误] 收到无效的二进制消息，大小:" << message.size();
        return;
    }
    
    // 解析时间戳（前8字节）
    qint64 captureTimestamp = 0;
    memcpy(&captureTimestamp, message.data(), 8);
    
    // 提取实际的VP9编码数据（跳过前8字节时间戳）
    QByteArray frameData = message.mid(8);
    
    // 移除每帧打印，避免影响性能
    // qDebug() << "[WebSocketReceiver] [重要] 解析后的VP9数据大小:" << frameData.size() << "字节，时间戳:" << captureTimestamp;
    
    // 使用帧同步机制，防止跳闪
    static QMutex frameMutex;
    QMutexLocker frameLocker(&frameMutex);
    
    // 更新统计信息
    {
        QMutexLocker locker(&m_mutex);
        m_stats.totalFrames++;
        m_stats.totalBytes += frameData.size();
        m_frameSizes.append(frameData.size());
        
        // 保持最近100帧的大小用于计算平均值
        if (m_frameSizes.size() > 100) {
            m_frameSizes.removeFirst();
        }
    }
    
    // 使用直接连接实现最低延迟
    // 传递帧数据和捕获时间戳
    // 移除每帧打印，避免影响性能
    // qDebug() << "[WebSocketReceiver] [重要] 发射frameReceivedWithTimestamp信号";
    emit frameReceivedWithTimestamp(frameData, captureTimestamp);
}

void WebSocketReceiver::onTextMessageReceived(const QString &message)
{
    // 只在调试模式下输出日志，避免影响性能
    #ifdef QT_DEBUG
    qDebug() << "[WebSocketReceiver] 收到文本消息:" << message.left(50); // 只显示前50字符
    #endif
    
    // 解析JSON消息以处理特定类型
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    
    if (error.error == QJsonParseError::NoError && doc.isObject()) {
        QJsonObject obj = doc.object();
        QString type = obj["type"].toString();
        
        // 处理鼠标位置消息
        if (type == "mouse_position") {
            int x = obj["x"].toInt();
            int y = obj["y"].toInt();
            qint64 timestamp = obj["timestamp"].toVariant().toLongLong();
            
            // 发射鼠标位置信号
            emit mousePositionReceived(QPoint(x, y), timestamp);
            
            // 每100条鼠标消息输出一次统计（避免日志过多）
            static int mouseMessageCount = 0;
            mouseMessageCount++;
            if (mouseMessageCount % 100 == 0) {
                qDebug() << "[WebSocketReceiver] 已接收" << mouseMessageCount 
                         << "条鼠标位置消息，最新位置:" << QPoint(x, y);
            }
            return;
        }
    }
    
    // 文本消息通常用于控制信息，这里可以根据需要处理
    // 对于登录系统的广播消息，我们不需要特殊处理，只是忽略即可
}

void WebSocketReceiver::onError(QAbstractSocket::SocketError error)
{
    QString errorString;
    switch (error) {
    case QAbstractSocket::ConnectionRefusedError:
        errorString = "连接被拒绝";
        break;
    case QAbstractSocket::RemoteHostClosedError:
        errorString = "远程主机关闭连接";
        break;
    case QAbstractSocket::HostNotFoundError:
        errorString = "主机未找到";
        break;
    case QAbstractSocket::SocketTimeoutError:
        errorString = "连接超时";
        break;
    case QAbstractSocket::NetworkError:
        errorString = "网络错误";
        break;
    default:
        errorString = QString("未知错误 (%1)").arg(static_cast<int>(error));
        break;
    }
    
    qWarning() << "[WebSocketReceiver] WebSocket错误:" << errorString;
    qWarning() << "[WebSocketReceiver] 错误详情字符串:" << m_webSocket->errorString();
    qWarning() << "[WebSocketReceiver] 当前状态:" << m_webSocket->state();
    qWarning() << "[WebSocketReceiver] 请求URL:" << m_webSocket->requestUrl().toString();
    qWarning() << "[WebSocketReceiver] 本地地址:" << m_webSocket->localAddress().toString() << ":" << m_webSocket->localPort();
    qWarning() << "[WebSocketReceiver] 对端地址:" << m_webSocket->peerAddress().toString() << ":" << m_webSocket->peerPort();
    emit connectionError(errorString);
}

void WebSocketReceiver::onSslErrors(const QList<QSslError> &errors)
{
    qWarning() << "[WebSocketReceiver] SSL错误:";
    for (const QSslError &error : errors) {
        qWarning() << "  -" << error.errorString();
    }
    
    // 在生产环境中，应该验证SSL证书
    // 这里为了简化，忽略SSL错误
    m_webSocket->ignoreSslErrors();
}

void WebSocketReceiver::attemptReconnect()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_reconnectEnabled || m_connected) {
        return;
    }
    
    m_reconnectAttempts++;
    qDebug() << QString("[WebSocketReceiver] 重连尝试 %1/%2")
                .arg(m_reconnectAttempts)
                .arg(m_maxReconnectAttempts);
    
    if (m_reconnectAttempts <= m_maxReconnectAttempts) {
        setupWebSocket(); // 重新创建WebSocket对象
        m_webSocket->open(QUrl(m_serverUrl));
    } else {
        qCritical() << "[WebSocketReceiver] 达到最大重连次数，停止重连";
        emit connectionError("达到最大重连次数");
    }
}

void WebSocketReceiver::startReconnectTimer()
{
    if (m_reconnectTimer && !m_reconnectTimer->isActive()) {
        int interval = m_reconnectInterval * (1 + m_reconnectAttempts); // 指数退避
        qDebug() << QString("[WebSocketReceiver] %1毫秒后尝试重连").arg(interval);
        m_reconnectTimer->start(interval);
    }
}

void WebSocketReceiver::stopReconnectTimer()
{
    if (m_reconnectTimer && m_reconnectTimer->isActive()) {
        m_reconnectTimer->stop();
    }
}

void WebSocketReceiver::updateStats()
{
    QMutexLocker locker(&m_mutex);
    
    // 计算平均帧大小
    if (!m_frameSizes.isEmpty()) {
        int total = 0;
        for (int size : m_frameSizes) {
            total += size;
        }
        m_stats.averageFrameSize = static_cast<double>(total) / m_frameSizes.size();
    }
    
    // 计算连接时间
    if (m_connected && m_connectionStartTime > 0) {
        m_stats.connectionTime = QDateTime::currentMSecsSinceEpoch() - m_connectionStartTime;
    }
    
    emit statsUpdated(m_stats);
}

void WebSocketReceiver::sendWatchRequest(const QString &viewerId, const QString &targetId)
{
    if (!m_connected || !m_webSocket) {
        qDebug() << "[WebSocketReceiver] 未连接到服务器，无法发送观看请求";
        qDebug() << "[WebSocketReceiver] 当前状态:" << (m_webSocket ? m_webSocket->state() : QAbstractSocket::UnconnectedState) << "目标URL:" << m_serverUrl;
        return;
    }
    
    // 构造观看请求消息
    QJsonObject message;
    message["type"] = "watch_request";
    message["viewer_id"] = viewerId;
    message["target_id"] = targetId;
    
    QJsonDocument doc(message);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    
    qDebug() << "[WebSocketReceiver] 发送观看请求:" << jsonString;
    m_webSocket->sendTextMessage(jsonString);
    
    // 立即发送开始推流请求
    QJsonObject startStreamingMessage;
    startStreamingMessage["type"] = "start_streaming";
    
    QJsonDocument startStreamingDoc(startStreamingMessage);
    QString startStreamingJsonString = startStreamingDoc.toJson(QJsonDocument::Compact);
    
    qDebug() << "[WebSocketReceiver] 发送开始推流请求:" << startStreamingJsonString;
    m_webSocket->sendTextMessage(startStreamingJsonString);
}