#include "WebSocketSender.h"
#include "TileManager.h"  // 包含TileInfo结构体定义
#include <QDebug>
#include <QMutexLocker>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>     // 添加QJsonArray头文件
#include <QJsonParseError>
#include <QDataStream>
#include <QBuffer>
#include <cstring>
#include <climits>

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
    , m_totalTilesSent(0)      // 初始化瓦片统计
    , m_totalTileDataSent(0)   // 初始化瓦片数据统计
    , m_lastStatsUpdateTime(QDateTime::currentMSecsSinceEpoch())
    , m_disconnectStartTime(0)
{
    // 初始化性能统计
    memset(&m_senderStats, 0, sizeof(m_senderStats));
    m_senderStats.minEncodingTime = LLONG_MAX;
    m_senderStats.minSendingTime = LLONG_MAX;
    m_senderStats.lastSendTime = QDateTime::currentMSecsSinceEpoch();
    
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
            // qDebug() << "[WebSocketSender] 发送帧数据失败"; // 已禁用以提升性能
        }
    } else {
        // qDebug() << "[WebSocketSender] WebSocket未连接，无法发送帧数据"; // 已禁用以提升性能
    }
}

void WebSocketSender::sendTextMessage(const QString &message)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_connected && m_webSocket) {
        qint64 bytesSent = m_webSocket->sendTextMessage(message);
        if (bytesSent > 0) {
            // 每100条文本消息输出一次统计信息（避免鼠标消息日志过多）
            static int textMessageCount = 0;
            textMessageCount++;
            if (textMessageCount % 100 == 0) {
                // qDebug() << "[WebSocketSender] 已发送" << textMessageCount << "条文本消息"; // 已禁用以提升性能
            }
        } else {
            // qDebug() << "[WebSocketSender] 发送文本消息失败:" << message.left(50); // 已禁用以提升性能
        }
    } else {
        // qDebug() << "[WebSocketSender] WebSocket未连接，无法发送文本消息"; // 已禁用以提升性能
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
    
    if (type == "watch_request") {
        qDebug() << "[WebSocketSender] 收到观看请求";
        QString viewerId = obj["viewer_id"].toString();
        QString targetId = obj["target_id"].toString();
        qDebug() << "[WebSocketSender] 观看者ID:" << viewerId << "目标ID:" << targetId;
        
        // 响应观看请求，开始推流
        startStreaming();
        
        // 可以在这里发送确认消息给观看者（如果需要的话）
        QJsonObject response;
        response["type"] = "watch_request_accepted";
        response["viewer_id"] = viewerId;
        response["target_id"] = targetId;
        
        QJsonDocument responseDoc(response);
        sendTextMessage(responseDoc.toJson(QJsonDocument::Compact));
        
    } else if (type == "start_streaming") {
        qDebug() << "[WebSocketSender] 收到开始推流请求";
        startStreaming();
    } else if (type == "stop_streaming") {
        qDebug() << "[WebSocketSender] 收到停止推流请求";
        stopStreaming();
    } else if (type == "annotation_event") {
        QString phase = obj.value("phase").toString();
        int x = obj.value("x").toInt();
        int y = obj.value("y").toInt();
        QString viewerId = obj.value("viewer_id").toString();
        qDebug() << "[WebSocketSender] 收到批注事件:" << phase << x << y << "viewer:" << viewerId;
        emit annotationEventReceived(phase, x, y, viewerId);
    } else if (type == "switch_screen") {
        QString direction = obj.value("direction").toString();
        int index = obj.value("index").toInt(-1);
        qDebug() << "[WebSocketSender] 收到切换屏幕请求: direction=" << direction << ", index=" << index;
        // 发射信号，由 main_capture 处理切换逻辑
        emit switchScreenRequested(direction, index);
    }
}

// 瓦片数据传输方法实现
void WebSocketSender::sendTileData(const QVector<int> &tileIndices, const QByteArray &serializedData)
{
    QMutexLocker locker(&m_mutex);
    
    // 开始性能监控
    qint64 serializationStartTime = QDateTime::currentMSecsSinceEpoch();
    qint64 encodingTime = 0; // 这里没有编码操作，设为0
    
    if (!m_connected || !m_isStreaming) {
        qDebug() << "[WebSocketSender] 未连接或未开始推流，无法发送瓦片数据";
        return;
    }
    
    // 创建瓦片数据消息
    QJsonObject message;
    message["type"] = "tile_data";
    message["tile_count"] = tileIndices.size();
    message["data_size"] = serializedData.size();
    
    // 将瓦片索引转换为JSON数组
    QJsonArray indicesArray;
    for (int index : tileIndices) {
        indicesArray.append(index);
    }
    message["tile_indices"] = indicesArray;
    
    // 发送JSON消息头
    QJsonDocument doc(message);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    
    // 组合消息：JSON头 + 分隔符 + 二进制数据
    QByteArray fullMessage;
    fullMessage.append(jsonData);
    fullMessage.append("\n---TILE_DATA_SEPARATOR---\n");
    fullMessage.append(serializedData);
    
    qint64 serializationEndTime = QDateTime::currentMSecsSinceEpoch();
    qint64 serializationTime = serializationEndTime - serializationStartTime;
    
    // 开始发送计时
    qint64 sendingStartTime = QDateTime::currentMSecsSinceEpoch();
    qint64 bytesSent = m_webSocket->sendBinaryMessage(fullMessage);
    qint64 sendingEndTime = QDateTime::currentMSecsSinceEpoch();
    qint64 sendingTime = sendingEndTime - sendingStartTime;
    
    if (bytesSent > 0) {
        m_totalBytesSent += bytesSent;
        m_totalTilesSent += tileIndices.size();
        m_totalTileDataSent += serializedData.size();
        
        // 更新性能统计
        updateSenderStats(encodingTime, sendingTime, serializationTime, bytesSent, tileIndices.size());
        
        emit tileDataSent(tileIndices.size(), serializedData.size());
        
        qDebug() << "[WebSocketSender] 发送瓦片数据:" 
                 << "瓦片数量=" << tileIndices.size() 
                 << "数据大小=" << serializedData.size() << "字节"
                 << "发送耗时=" << sendingTime << "ms";
    }
}

void WebSocketSender::sendTileUpdate(const QVector<TileInfo> &updatedTiles, const QVector<QByteArray> &tileImages)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_connected || !m_isStreaming) {
        qDebug() << "[WebSocketSender] 未连接或未开始推流，无法发送瓦片更新";
        return;
    }
    
    if (updatedTiles.size() != tileImages.size()) {
        qDebug() << "[WebSocketSender] 瓦片信息和图像数据数量不匹配";
        return;
    }
    
    // 序列化瓦片更新数据
    QByteArray serializedData;
    QDataStream stream(&serializedData, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    
    // 写入瓦片数量
    stream << static_cast<quint32>(updatedTiles.size());
    
    // 写入每个瓦片的信息和图像数据
    for (int i = 0; i < updatedTiles.size(); ++i) {
        const TileInfo &tile = updatedTiles[i];
        const QByteArray &imageData = tileImages[i];
        
        // 写入瓦片信息
        stream << tile.x << tile.y << tile.width << tile.height << tile.hash;
        
        // 写入图像数据大小和数据
        stream << static_cast<quint32>(imageData.size());
        stream.writeRawData(imageData.constData(), imageData.size());
    }
    
    // 创建消息头
    QJsonObject message;
    message["type"] = "tile_update";
    message["updated_count"] = updatedTiles.size();
    message["data_size"] = serializedData.size();
    
    QJsonDocument doc(message);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    
    // 组合消息
    QByteArray fullMessage;
    fullMessage.append(jsonData);
    fullMessage.append("\n---TILE_UPDATE_SEPARATOR---\n");
    fullMessage.append(serializedData);
    
    qint64 bytesSent = m_webSocket->sendBinaryMessage(fullMessage);
    if (bytesSent > 0) {
        m_totalBytesSent += bytesSent;
        m_totalTilesSent += updatedTiles.size();
        m_totalTileDataSent += serializedData.size();
        
        emit tileUpdateSent(updatedTiles.size());
        
        qDebug() << "[WebSocketSender] 发送瓦片更新:" 
                 << "更新瓦片数=" << updatedTiles.size() 
                 << "数据大小=" << serializedData.size() << "字节";
    }
}

void WebSocketSender::sendTileMetadata(const QVector<TileInfo> &allTiles)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_connected) {
        qDebug() << "[WebSocketSender] 未连接，无法发送瓦片元数据";
        return;
    }
    
    // 创建瓦片元数据消息
    QJsonObject message;
    message["type"] = "tile_metadata";
    message["total_tiles"] = allTiles.size();
    
    QJsonArray tilesArray;
    for (const TileInfo &tile : allTiles) {
        QJsonObject tileObj;
        tileObj["x"] = tile.x;
        tileObj["y"] = tile.y;
        tileObj["width"] = tile.width;
        tileObj["height"] = tile.height;
        tileObj["hash"] = static_cast<qint64>(tile.hash);
        tilesArray.append(tileObj);
    }
    message["tiles"] = tilesArray;
    
    QJsonDocument doc(message);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    
    qint64 bytesSent = m_webSocket->sendTextMessage(QString::fromUtf8(jsonData));
    if (bytesSent > 0) {
        m_totalBytesSent += bytesSent;
        
        emit tileMetadataSent(allTiles.size());
        
        qDebug() << "[WebSocketSender] 发送瓦片元数据:" 
                  << "瓦片总数=" << allTiles.size() 
                  << "数据大小=" << jsonData.size() << "字节";
    }
}

void WebSocketSender::updateSenderStats(qint64 encodingTime, qint64 sendingTime, qint64 serializationTime, qint64 bytesSent, int tilesSent)
{
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
    // 更新基本统计
    m_senderStats.totalTilesSent += tilesSent;
    m_senderStats.totalBytesSent += bytesSent;
    m_senderStats.totalEncodingOperations++;
    m_senderStats.totalEncodingTime += encodingTime;
    m_senderStats.totalSendingTime += sendingTime;
    m_senderStats.totalSerializationTime += serializationTime;
    m_senderStats.lastSendTime = currentTime;
    
    // 更新编码时间统计
    if (encodingTime > 0) {
        if (encodingTime > m_senderStats.maxEncodingTime) {
            m_senderStats.maxEncodingTime = encodingTime;
        }
        if (encodingTime < m_senderStats.minEncodingTime) {
            m_senderStats.minEncodingTime = encodingTime;
        }
        m_encodingTimes.append(encodingTime);
        if (m_encodingTimes.size() > 100) {
            m_encodingTimes.removeFirst();
        }
    }
    
    // 更新发送时间统计
    if (sendingTime > m_senderStats.maxSendingTime) {
        m_senderStats.maxSendingTime = sendingTime;
    }
    if (sendingTime < m_senderStats.minSendingTime) {
        m_senderStats.minSendingTime = sendingTime;
    }
    m_sendingTimes.append(sendingTime);
    if (m_sendingTimes.size() > 100) {
        m_sendingTimes.removeFirst();
    }
    
    // 更新序列化时间统计
    m_serializationTimes.append(serializationTime);
    if (m_serializationTimes.size() > 100) {
        m_serializationTimes.removeFirst();
    }
    
    // 计算平均时间
    if (m_senderStats.totalEncodingOperations > 0) {
        m_senderStats.averageEncodingTime = m_senderStats.totalEncodingTime / m_senderStats.totalEncodingOperations;
    }
    if (m_sendingTimes.size() > 0) {
        qint64 totalSendingTime = 0;
        for (qint64 time : m_sendingTimes) {
            totalSendingTime += time;
        }
        m_senderStats.averageSendingTime = totalSendingTime / m_sendingTimes.size();
    }
    
    // 计算发送速率（每秒字节数和瓦片数）
    qint64 timeDiff = currentTime - m_lastStatsUpdateTime;
    if (timeDiff >= 1000) { // 每秒更新一次速率
        if (timeDiff > 0) {
            m_senderStats.sendingRate = (m_senderStats.totalBytesSent * 1000.0) / timeDiff;
            m_senderStats.tileTransmissionRate = (m_senderStats.totalTilesSent * 1000.0) / timeDiff;
        }
        m_lastStatsUpdateTime = currentTime;
    }
    
    // 每1000次发送输出详细统计
    if (m_senderStats.totalTilesSent % 1000 == 0) {
        qDebug() << "[WebSocketSender] 发送统计:"
                 << "总瓦片=" << m_senderStats.totalTilesSent
                 << "总字节=" << m_senderStats.totalBytesSent
                 << "平均编码时间=" << m_senderStats.averageEncodingTime << "ms"
                 << "平均发送时间=" << m_senderStats.averageSendingTime << "ms"
                 << "发送速率=" << (m_senderStats.sendingRate / 1024.0) << "KB/s"
                 << "瓦片传输速率=" << m_senderStats.tileTransmissionRate << "tiles/s";
    }
}

WebSocketSender::SenderStats WebSocketSender::getSenderStats() const
{
    QMutexLocker locker(&m_mutex);
    return m_senderStats;
}

void WebSocketSender::resetSenderStats()
{
    QMutexLocker locker(&m_mutex);
    
    // 清空统计数据
    memset(&m_senderStats, 0, sizeof(m_senderStats));
    m_senderStats.minEncodingTime = LLONG_MAX;
    m_senderStats.minSendingTime = LLONG_MAX;
    m_senderStats.lastSendTime = QDateTime::currentMSecsSinceEpoch();
    
    // 清空时间记录
    m_encodingTimes.clear();
    m_sendingTimes.clear();
    m_serializationTimes.clear();
    
    // 重置更新时间
    m_lastStatsUpdateTime = QDateTime::currentMSecsSinceEpoch();
    
    qDebug() << "[WebSocketSender] 性能统计已重置";
}