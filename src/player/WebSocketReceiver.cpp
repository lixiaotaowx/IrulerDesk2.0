#include "WebSocketReceiver.h"
#include <QDateTime>
#include <QMutexLocker>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPoint>
#include <QSslError>
#include <QNetworkProxy>
#include <cmath>

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
    , m_lastStatsUpdateTime(0)
    , m_totalDowntimeStart(0)
    , m_totalChunksReceived(0)
    , m_totalChunksLost(0)
    , m_totalTileBytes(0)
    , m_tileTimeoutTimer(nullptr)
    , m_tileTimeoutMs(5000) // 5秒超时
    , m_maxRetransmissionAttempts(3)
{
    setupWebSocket();
    
    memset(&m_stats, 0, sizeof(m_stats));
    m_lastStatsUpdateTime = QDateTime::currentMSecsSinceEpoch();
    
    // 设置统计更新定时器
    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &WebSocketReceiver::updateStats);
    m_statsTimer->start(1000); // 每秒更新一次统计
    
    // 设置重连定时器
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &WebSocketReceiver::attemptReconnect);
    
    // 设置瓦片超时检查定时器
    m_tileTimeoutTimer = new QTimer(this);
    connect(m_tileTimeoutTimer, &QTimer::timeout, this, &WebSocketReceiver::checkTileTimeout);
    m_tileTimeoutTimer->start(1000); // 每秒检查一次超时
    
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
    
    // 性能监控：更新重连和断线统计
    if (m_totalDowntimeStart > 0) {
        qint64 downtime = m_connectionStartTime - m_totalDowntimeStart;
        m_stats.totalDowntime += downtime;
        m_stats.reconnectionCount++;
        m_totalDowntimeStart = 0;
    }
    
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
    
    // 性能监控：记录断线开始时间
    if (wasConnected) {
        m_totalDowntimeStart = QDateTime::currentMSecsSinceEpoch();
    }
    
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
    
    if (message.isEmpty() || message.size() < 4) {
        qDebug() << "[WebSocketReceiver] [错误] 收到无效的二进制消息，大小:" << message.size();
        return;
    }
    
    // 读取JSON头部长度（前4字节）
    quint32 headerLength = 0;
    memcpy(&headerLength, message.data(), 4);
    
    if (headerLength == 0 || headerLength > message.size() - 4) {
        // 如果没有JSON头部，按照旧格式处理（兼容性）
        if (message.size() >= 8) {
            // 解析时间戳（前8字节）
            qint64 captureTimestamp = 0;
            memcpy(&captureTimestamp, message.data(), 8);
            
            // 提取实际的VP9编码数据（跳过前8字节时间戳）
            QByteArray frameData = message.mid(8);
            
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
            
            // 发射传统帧信号
            emit frameReceivedWithTimestamp(frameData, captureTimestamp);
        }
        return;
    }
    
    // 解析JSON头部
    QByteArray headerData = message.mid(4, headerLength);
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(headerData, &error);
    
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug() << "[WebSocketReceiver] [错误] JSON头部解析失败:" << error.errorString();
        return;
    }
    
    QJsonObject header = doc.object();
    QString messageType = header["type"].toString();
    
    // 提取二进制数据部分
    QByteArray binaryData = message.mid(4 + headerLength);
    
    // 根据消息类型处理
    if (messageType.startsWith("tile_")) {
        processTileMessage(header, binaryData);
    } else {
        // 处理其他类型的消息（如传统帧数据）
        qDebug() << "[WebSocketReceiver] 收到未知消息类型:" << messageType;
    }
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
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
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
        m_stats.connectionTime = currentTime - m_connectionStartTime;
    }
    
    // 更新详细性能统计
    m_stats.totalTileBytes = m_totalTileBytes;
    m_stats.totalChunksReceived = m_totalChunksReceived;
    m_stats.totalChunksLost = m_totalChunksLost;
    
    // 计算平均瓦片大小
    if (m_stats.completedTiles > 0) {
        m_stats.averageTileSize = static_cast<double>(m_totalTileBytes) / m_stats.completedTiles;
    }
    
    // 计算瓦片传输速率
    qint64 timeDiff = currentTime - m_lastStatsUpdateTime;
    if (timeDiff > 0) {
        m_stats.tileTransferRate = static_cast<double>(m_totalTileBytes) / (timeDiff / 1000.0);
    }
    
    // 计算数据块丢包率
    quint64 totalChunks = m_totalChunksReceived + m_totalChunksLost;
    if (totalChunks > 0) {
        m_stats.chunkLossRate = static_cast<double>(m_totalChunksLost) / totalChunks;
    }
    
    // 计算平均组装时间
    if (!m_assemblyTimes.isEmpty()) {
        qint64 totalAssemblyTime = 0;
        m_stats.maxAssemblyTime = 0;
        m_stats.minAssemblyTime = LLONG_MAX;
        
        for (qint64 time : m_assemblyTimes) {
            totalAssemblyTime += time;
            if (time > m_stats.maxAssemblyTime) {
                m_stats.maxAssemblyTime = time;
            }
            if (time < m_stats.minAssemblyTime) {
                m_stats.minAssemblyTime = time;
            }
        }
        m_stats.averageAssemblyTime = totalAssemblyTime / m_assemblyTimes.size();
    }
    
    // 计算平均传输时间
    if (!m_transmissionTimes.isEmpty()) {
        qint64 totalTransmissionTime = 0;
        for (qint64 time : m_transmissionTimes) {
            totalTransmissionTime += time;
        }
        m_stats.averageTransmissionTime = totalTransmissionTime / m_transmissionTimes.size();
    }
    
    // 计算网络延迟和抖动
    if (!m_latencyMeasurements.isEmpty()) {
        qint64 totalLatency = 0;
        for (qint64 latency : m_latencyMeasurements) {
            totalLatency += latency;
        }
        m_stats.networkLatency = static_cast<double>(totalLatency) / m_latencyMeasurements.size();
        
        // 计算抖动（延迟的标准差）
        double variance = 0;
        for (qint64 latency : m_latencyMeasurements) {
            double diff = latency - m_stats.networkLatency;
            variance += diff * diff;
        }
        m_stats.jitter = sqrt(variance / m_latencyMeasurements.size());
    }
    
    m_lastStatsUpdateTime = currentTime;
    
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

// 瓦片消息处理方法实现
void WebSocketReceiver::processTileMessage(const QJsonObject &header, const QByteArray &binaryData)
{
    QString messageType = header["type"].toString();
    
    if (messageType == "tile_metadata") {
        handleTileMetadata(header);
    } else if (messageType == "tile_data") {
        handleTileData(header, binaryData);
    } else if (messageType == "tile_update") {
        handleTileUpdate(header, binaryData);
    } else if (messageType == "tile_complete") {
        handleTileComplete(header);
    } else {
        qDebug() << "[WebSocketReceiver] 未知的瓦片消息类型:" << messageType;
    }
}

void WebSocketReceiver::handleTileMetadata(const QJsonObject &header)
{
    TileMetadata metadata;
    metadata.tileId = header["tile_id"].toInt();
    metadata.x = header["x"].toInt();
    metadata.y = header["y"].toInt();
    metadata.width = header["width"].toInt();
    metadata.height = header["height"].toInt();
    metadata.totalChunks = header["total_chunks"].toInt();
    metadata.dataSize = header["data_size"].toInt();
    metadata.timestamp = header["timestamp"].toVariant().toLongLong();
    metadata.format = header["format"].toString();
    
    QMutexLocker locker(&m_tileMutex);
    
    // 创建或更新瓦片缓存
    TileCache &cache = m_tileCache[metadata.tileId];
    cache.metadata = metadata;
    cache.chunks.clear();
    cache.receivedChunks.clear();
    cache.lastUpdateTime = QDateTime::currentMSecsSinceEpoch();
    cache.isComplete = false;
    
    // 更新统计
    m_stats.totalTiles++;
    
    qDebug() << "[WebSocketReceiver] 收到瓦片元数据: ID=" << metadata.tileId 
             << "位置=(" << metadata.x << "," << metadata.y << ")"
             << "大小=" << metadata.width << "x" << metadata.height
             << "分块数=" << metadata.totalChunks;
    
    emit tileMetadataReceived(metadata);
}

void WebSocketReceiver::handleTileData(const QJsonObject &header, const QByteArray &data)
{
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
    TileChunk chunk;
    chunk.tileId = header["tile_id"].toInt();
    chunk.chunkIndex = header["chunk_index"].toInt();
    chunk.totalChunks = header["total_chunks"].toInt();
    chunk.timestamp = header["timestamp"].toVariant().toLongLong();
    chunk.data = data;
    
    QMutexLocker locker(&m_tileMutex);
    
    // 性能监控：记录数据块接收
    m_totalChunksReceived++;
    m_totalTileBytes += data.size();
    
    // 记录网络延迟（如果有时间戳）
    if (chunk.timestamp > 0) {
        qint64 latency = currentTime - chunk.timestamp;
        m_latencyMeasurements.append(latency);
        // 保持最近100个延迟测量
        if (m_latencyMeasurements.size() > 100) {
            m_latencyMeasurements.removeFirst();
        }
    }
    
    // 检查瓦片缓存是否存在
    if (!m_tileCache.contains(chunk.tileId)) {
        qDebug() << "[WebSocketReceiver] 收到未知瓦片的数据块: ID=" << chunk.tileId;
        return;
    }
    
    TileCache &cache = m_tileCache[chunk.tileId];
    
    // 记录瓦片开始接收时间（第一个数据块）
    if (chunk.chunkIndex == 0 && !m_tileStartTimes.contains(chunk.tileId)) {
        m_tileStartTimes[chunk.tileId] = currentTime;
    }
    
    // 存储数据块
    cache.chunks[chunk.chunkIndex] = chunk.data;
    cache.receivedChunks.insert(chunk.chunkIndex);
    cache.lastUpdateTime = currentTime;
    
    qDebug() << "[WebSocketReceiver] 收到瓦片数据块: ID=" << chunk.tileId 
             << "块索引=" << chunk.chunkIndex << "/" << chunk.totalChunks
             << "数据大小=" << chunk.data.size();
    
    emit tileChunkReceived(chunk);
    
    // 检查是否收到所有块
    qDebug() << "[WebSocketReceiver] 瓦片" << chunk.tileId << "已收到" 
             << cache.receivedChunks.size() << "/" << cache.metadata.totalChunks << "个数据块";
    
    if (cache.receivedChunks.size() == cache.metadata.totalChunks) {
        qDebug() << "[WebSocketReceiver] 瓦片" << chunk.tileId << "所有数据块已收到，开始组装";
        
        // 记录传输时间
        if (m_tileStartTimes.contains(chunk.tileId)) {
            qint64 transmissionTime = currentTime - m_tileStartTimes[chunk.tileId];
            m_transmissionTimes.append(transmissionTime);
            m_tileStartTimes.remove(chunk.tileId);
            // 保持最近100个传输时间记录
            if (m_transmissionTimes.size() > 100) {
                m_transmissionTimes.removeFirst();
            }
        }
        
        assembleTile(chunk.tileId);
    }
}

void WebSocketReceiver::handleTileUpdate(const QJsonObject &header, const QByteArray &data)
{
    TileUpdate update;
    update.tileId = header["tile_id"].toInt();
    update.x = header["x"].toInt();
    update.y = header["y"].toInt();
    update.width = header["width"].toInt();
    update.height = header["height"].toInt();
    update.timestamp = header["timestamp"].toVariant().toLongLong();
    update.deltaData = data;
    
    qDebug() << "[WebSocketReceiver] 收到瓦片更新: ID=" << update.tileId 
             << "位置=(" << update.x << "," << update.y << ")"
             << "增量数据大小=" << update.deltaData.size();
    
    emit tileUpdateReceived(update);
}

void WebSocketReceiver::handleTileComplete(const QJsonObject &header)
{
    int tileId = header["tile_id"].toInt();
    qint64 timestamp = header["timestamp"].toVariant().toLongLong();
    
    qDebug() << "[WebSocketReceiver] 收到瓦片完成消息: ID=" << tileId 
             << "时间戳=" << timestamp;
    
    QMutexLocker locker(&m_tileMutex);
    
    // 检查瓦片是否存在且已完成
    if (m_tileCache.contains(tileId)) {
        TileCache &cache = m_tileCache[tileId];
        if (cache.isComplete) {
            qDebug() << "[WebSocketReceiver] 瓦片" << tileId << "已完成，发送tileCompleted信号";
            
            // 组装完整数据
            QByteArray completeData;
            for (int i = 0; i < cache.metadata.totalChunks; ++i) {
                if (cache.chunks.contains(i)) {
                    completeData.append(cache.chunks[i]);
                }
            }
            
            emit tileCompleted(tileId, completeData);
        } else {
            qDebug() << "[WebSocketReceiver] 瓦片" << tileId << "尚未完成，等待更多数据块";
        }
    } else {
        qDebug() << "[WebSocketReceiver] 收到未知瓦片的完成消息: ID=" << tileId;
    }
}

void WebSocketReceiver::assembleTile(int tileId)
{
    qint64 assemblyStartTime = QDateTime::currentMSecsSinceEpoch();
    qDebug() << "[WebSocketReceiver] 开始组装瓦片: ID=" << tileId;
    
    // 注意：调用此方法的函数应该已经获取了m_tileMutex锁
    
    if (!m_tileCache.contains(tileId)) {
        qDebug() << "[WebSocketReceiver] 瓦片缓存中不存在: ID=" << tileId;
        return;
    }
    
    TileCache &cache = m_tileCache[tileId];
    qDebug() << "[WebSocketReceiver] 瓦片缓存信息: ID=" << tileId 
             << "总块数=" << cache.metadata.totalChunks 
             << "已收到块数=" << cache.chunks.size();
    
    // 按顺序组装数据块
    QByteArray completeData;
    for (int i = 0; i < cache.metadata.totalChunks; ++i) {
        if (!cache.chunks.contains(i)) {
            qDebug() << "[WebSocketReceiver] 瓦片组装失败，缺少块:" << i;
            return;
        }
        qDebug() << "[WebSocketReceiver] 添加数据块" << i << "大小=" << cache.chunks[i].size();
        completeData.append(cache.chunks[i]);
    }
    
    qDebug() << "[WebSocketReceiver] 数据块组装完成，总大小=" << completeData.size();
    
    cache.isComplete = true;
    
    // 性能监控：记录组装时间
    qint64 assemblyEndTime = QDateTime::currentMSecsSinceEpoch();
    qint64 assemblyTime = assemblyEndTime - assemblyStartTime;
    m_assemblyTimes.append(assemblyTime);
    // 保持最近100个组装时间记录
    if (m_assemblyTimes.size() > 100) {
        m_assemblyTimes.removeFirst();
    }
    
    // 更新统计
    m_stats.completedTiles++;
    if (m_stats.totalTiles > 0) {
        m_stats.tileCompletionRate = static_cast<double>(m_stats.completedTiles) / m_stats.totalTiles;
    }
    
    qDebug() << "[WebSocketReceiver] 瓦片组装完成: ID=" << tileId 
             << "完整数据大小=" << completeData.size()
             << "组装耗时=" << assemblyTime << "ms";
    
    qDebug() << "[WebSocketReceiver] 准备发送tileCompleted信号: ID=" << tileId;
    
    emit tileCompleted(tileId, completeData);
    
    qDebug() << "[WebSocketReceiver] tileCompleted信号已发送: ID=" << tileId;
    
    // 清理缓存（可选，根据需要保留一段时间）
    // m_tileCache.remove(tileId);
}

void WebSocketReceiver::checkTileTimeout()
{
    QMutexLocker locker(&m_tileMutex);
    
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    QList<int> timedOutTiles;
    
    for (auto it = m_tileCache.begin(); it != m_tileCache.end(); ++it) {
        TileCache &cache = it.value();
        
        if (!cache.isComplete && 
            (currentTime - cache.lastUpdateTime) > m_tileTimeoutMs) {
            
            int tileId = it.key();
            timedOutTiles.append(tileId);
            
            // 检查缺失的块
            QSet<int> missingChunks;
            for (int i = 0; i < cache.metadata.totalChunks; ++i) {
                if (!cache.receivedChunks.contains(i)) {
                    missingChunks.insert(i);
                }
            }
            
            // 检查重传次数
            int retransmissionCount = m_retransmissionCounts.value(tileId, 0);
            if (retransmissionCount < m_maxRetransmissionAttempts) {
                requestRetransmission(tileId, missingChunks);
                m_retransmissionCounts[tileId] = retransmissionCount + 1;
                cache.lastUpdateTime = currentTime; // 重置超时时间
            } else {
                // 超过最大重传次数，标记为丢失
                qDebug() << "[WebSocketReceiver] 瓦片超时且重传次数超限: ID=" << tileId;
                m_stats.lostTiles++;
                emit tileDataLost(tileId, missingChunks);
            }
        }
    }
    
    // 清理超时且无法恢复的瓦片
    for (int tileId : timedOutTiles) {
        if (m_retransmissionCounts.value(tileId, 0) >= m_maxRetransmissionAttempts) {
            m_tileCache.remove(tileId);
            m_retransmissionCounts.remove(tileId);
        }
    }
}

void WebSocketReceiver::requestRetransmission(int tileId, const QSet<int> &missingChunks)
{
    if (!m_connected || !m_webSocket) {
        return;
    }
    
    QJsonObject request;
    request["type"] = "retransmission_request";
    request["tile_id"] = tileId;
    
    QJsonArray chunksArray;
    for (int chunkIndex : missingChunks) {
        chunksArray.append(chunkIndex);
    }
    request["missing_chunks"] = chunksArray;
    
    QJsonDocument doc(request);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    
    m_webSocket->sendTextMessage(jsonString);
    m_stats.retransmissionRequests++;
    
    qDebug() << "[WebSocketReceiver] 请求重传瓦片: ID=" << tileId 
             << "缺失块数=" << missingChunks.size();
    
    emit retransmissionRequested(tileId, missingChunks);
}