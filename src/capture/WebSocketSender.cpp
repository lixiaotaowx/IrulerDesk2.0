#include "WebSocketSender.h"
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
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

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
    m_sendTimer = new QTimer(this);
    m_sendTimer->setInterval(0);
    connect(m_sendTimer, &QTimer::timeout, this, &WebSocketSender::onSendTimer);
}

WebSocketSender::~WebSocketSender()
{
    disconnectFromServer();
    
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
        return true;
    }
    
    m_serverUrl = url;
    qDebug() << "[Sender] Connecting to URL:" << url;
    
    m_webSocket->open(QUrl(url));
    return true;
}

void WebSocketSender::disconnectFromServer()
{
    QMutexLocker locker(&m_mutex);
    
    stopReconnectTimer();
    
    if (m_webSocket && m_connected) {
        
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
            
            // 恢复帧发送日志以调试生产者是否有数据发出
            if (m_totalFramesSent % 50 == 0) {
                qDebug() << "[WebSocketSender] Sent frames:" << m_totalFramesSent << "Total bytes:" << m_totalBytesSent;
            }
        } else {
            qDebug() << "[WebSocketSender] Send frame failed. Connected:" << m_connected << "Streaming:" << m_isStreaming;
        }
    } else {
         // 增加详细的未发送原因日志 (仅当尝试发送但条件不满足时)
         static int skipLogCount = 0;
         if (++skipLogCount % 100 == 0) {
             qDebug() << "[WebSocketSender] Cannot send frame. Connected:" << m_connected 
                      << "WebSocket:" << (m_webSocket != nullptr) 
                      << "Streaming:" << m_isStreaming;
         }
    }
}

void WebSocketSender::enqueueFrame(const QByteArray &frameData, bool keyFrame)
{
    QMutexLocker locker(&m_mutex);
    if (!m_connected || !m_webSocket || !m_isStreaming) {
        return;
    }
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (frameData.size() >= 8) {
        qint64 ts;
        memcpy(&ts, frameData.constData(), 8);
        if (nowMs - ts > m_queueMaxAgeMs) {
            return;
        }
    }

    if (m_frameQueue.size() >= m_maxQueueSize) {
        if (keyFrame) {
            m_frameQueue.clear();
            m_keyQueue.clear();
            m_frameQueue.enqueue(frameData);
            m_keyQueue.enqueue(true);
        } else {
            while (!m_frameQueue.isEmpty()) {
                const QByteArray &first = m_frameQueue.head();
                if (first.size() >= 8) {
                    qint64 ots;
                    memcpy(&ots, first.constData(), 8);
                    if (nowMs - ots > m_queueMaxAgeMs) {
                        m_frameQueue.dequeue();
                        m_keyQueue.dequeue();
                        m_droppedFramesDueToAge++;
                        continue;
                    }
                }
                break;
            }

            if (m_frameQueue.size() >= m_maxQueueSize) {
                int n = m_frameQueue.size();
                QQueue<QByteArray> newF;
                QQueue<bool> newK;
                bool dropped = false;
                for (int i = 0; i < n; ++i) {
                    QByteArray d = m_frameQueue.dequeue();
                    bool k = m_keyQueue.dequeue();
                    if (!dropped && !k) {
                        dropped = true;
                        m_droppedFramesDueToQueue++;
                        continue;
                    }
                    newF.enqueue(d);
                    newK.enqueue(k);
                }
                m_frameQueue = newF;
                m_keyQueue = newK;
            }

            if (m_frameQueue.size() < m_maxQueueSize) {
                m_frameQueue.enqueue(frameData);
                m_keyQueue.enqueue(false);
            }
        }
    } else {
        m_frameQueue.enqueue(frameData);
        m_keyQueue.enqueue(keyFrame);
    }
    if (m_sendTimer && !m_sendTimer->isActive()) {
        m_sendTimer->start();
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

            // 调试：验证音频消息是否发送
            if (message.contains("audio_opus")) {
                 static int audioMsgCount = 0;
                 if (++audioMsgCount % 50 == 0) {
                     qDebug() << "[WebSocketSender] Sent audio_opus message #" << audioMsgCount << " Size:" << message.size();
                 }
            }
        } else {
            // 仅对非高频消息启用错误日志
            if (!message.contains("mouse_position")) {
                 qDebug() << "[WebSocketSender] 发送文本消息失败:" << message.left(50);
            }
        }
    } else {
        static int connErrCount = 0;
        if (++connErrCount % 100 == 0) {
             qDebug() << "[WebSocketSender] WebSocket未连接，无法发送文本消息 (Count:" << connErrCount << ")";
        }
    }
}

void WebSocketSender::onConnected()
{
    bool isPublisher = false;
    {
        QMutexLocker locker(&m_mutex);
        
        m_connected = true;
        m_reconnectAttempts = 0;
        stopReconnectTimer();
        
        isPublisher = m_serverUrl.contains("/publish/");
    }
    
    emit connected();

    if (isPublisher) {
        if (!isManualApprovalEnabled()) {
            qDebug() << "[Sender] Publisher connected, auto-starting streaming (bypassing server trigger)";
            startStreaming();
            emit requestKeyFrame();
        }
    }
}

void WebSocketSender::onDisconnected()
{
    QMutexLocker locker(&m_mutex);
    
    bool wasConnected = m_connected;
    m_connected = false;
    m_frameQueue.clear();
    m_keyQueue.clear();
    
    if (wasConnected) {
        // 断开连接时必须停止推流，确保状态重置
        if (m_isStreaming) {
            m_isStreaming = false;
            emit streamingStopped();
        }
        
        emit disconnected();
        
        // 启动重连
        startReconnectTimer();
    }
}

void WebSocketSender::onError(QAbstractSocket::SocketError socketError)
{
    QString errorString = m_webSocket->errorString();
    
    
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
        
        emit error("WebSocket重连失败，已达到最大重连次数");
        return;
    }
    
    
    
    // 重新设置WebSocket
    setupWebSocket();
    m_webSocket->open(QUrl(m_serverUrl));
}

void WebSocketSender::startReconnectTimer()
{
    if (m_reconnectTimer && !m_reconnectTimer->isActive()) {
        int delay = m_reconnectInterval * (1 + m_reconnectAttempts); // 递增延迟
        m_reconnectTimer->start(delay);
        
    }
}

void WebSocketSender::stopReconnectTimer()
{
    if (m_reconnectTimer && m_reconnectTimer->isActive()) {
        m_reconnectTimer->stop();
        
    }
}

void WebSocketSender::startStreaming()
{
    QMutexLocker locker(&m_mutex);
    if (!m_isStreaming) {
        m_isStreaming = true;
        
        emit streamingStarted();
    }
}

void WebSocketSender::stopStreaming()
{
    QMutexLocker locker(&m_mutex);
    if (m_isStreaming) {
        m_isStreaming = false;
        m_frameQueue.clear();
        m_keyQueue.clear();
        
        emit streamingStopped();
    }
}

void WebSocketSender::forceKeyFrame()
{
    emit requestKeyFrame();
}

void WebSocketSender::onTextMessageReceived(const QString &message)
{
    
    
    // 解析JSON消息
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        
        return;
    }
    
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();
    
    if (type == "watch_request") {
        QString viewerName = obj.value("viewer_name").toString();
        if (!viewerName.isEmpty()) {
            m_viewerName = viewerName;
            emit viewerNameChanged(m_viewerName);
        }
        QString viewerId = obj["viewer_id"].toString();
        QString targetId = obj["target_id"].toString();
        int iconId = obj.value("viewer_icon_id").toInt(-1);

        if (isManualApprovalEnabled()) {
            if (m_waitingForApproval) {
                return;
            }
            m_pendingViewerId = viewerId;
            m_pendingTargetId = targetId;
            m_pendingViewerName = m_viewerName;
            m_pendingIconId = iconId;
            m_waitingForApproval = true;
            sendApprovalRequired(viewerId, targetId);
            emit watchRequestReceived(viewerId, m_viewerName, targetId, iconId);
        } else {
            if (m_isStreaming) {
                stopStreaming();
            }
            startStreaming();
            emit requestKeyFrame();
            sendWatchAccepted(viewerId, targetId);
        }
    } else if (type == "start_streaming" || type == "start_streaming_request") {
        if (isManualApprovalEnabled()) {
            return;
        }
        startStreaming();
        emit requestKeyFrame();
    } else if (type == "request_keyframe") {
        emit requestKeyFrame();
    } else if (type == "stop_streaming") {
        
        stopStreaming();
    } else if (type == "annotation_event") {
        QString phase = obj.value("phase").toString();
        int x = obj.value("x").toInt();
        int y = obj.value("y").toInt();
        QString viewerId = obj.value("viewer_id").toString();
        int colorId = obj.value("color_id").toInt(0);
        
        emit annotationEventReceived(phase, x, y, viewerId, colorId);
    } else if (type == "text_annotation") {
        QString txt = obj.value("text").toString();
        int x = obj.value("x").toInt();
        int y = obj.value("y").toInt();
        QString viewerId = obj.value("viewer_id").toString();
        int colorId = obj.value("color_id").toInt(0);
        int fontSize = obj.value("font_size").toInt(16);
        emit textAnnotationReceived(txt, x, y, viewerId, colorId, fontSize);
    } else if (type == "like_event") {
        QString viewerId = obj.value("viewer_id").toString();
        emit likeRequested(viewerId);
    } else if (type == "switch_screen") {
        QString direction = obj.value("direction").toString();
        int index = obj.value("index").toInt(-1);
        
        // 发射信号，由 main_capture 处理切换逻辑
        emit switchScreenRequested(direction, index);
    } else if (type == "set_quality") {
        QString quality = obj.value("quality").toString();
        
        emit qualityChangeRequested(quality);
    } else if (type == "audio_toggle") {
        bool enabled = obj.value("enabled").toBool(false);
        emit audioToggleRequested(enabled);
    } else if (type == "audio_gain") {
        int percent = obj.value("percent").toInt(100);
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        emit audioGainRequested(percent);
    } else if (type == "viewer_audio_opus") {
        QString vid = obj.value("sender_id").toString();
        if (vid.isEmpty()) vid = obj.value("viewer_id").toString();
        int sr = obj.value("sample_rate").toInt(16000);
        int ch = obj.value("channels").toInt(1);
        int frameSamples = obj.value("frame_samples").toInt(320);
        qint64 ts = obj.value("timestamp").toVariant().toLongLong();
        QByteArray b64 = obj.value("data_base64").toString().toUtf8();
        QByteArray opusData = QByteArray::fromBase64(b64);
        emit viewerAudioOpusReceived(vid, opusData, sr, ch, frameSamples, ts);
    } else if (type == "viewer_listen_mute") {
        bool mute = obj.value("mute").toBool(false);
        emit viewerListenMuteRequested(mute);
    } else if (type == "viewer_cursor") {
        QString vid = obj.value("viewer_id").toString();
        QString vname = obj.value("viewer_name").toString();
        int x = obj.value("x").toInt();
        int y = obj.value("y").toInt();
        emit viewerCursorReceived(vid, x, y, vname);
    } else if (type == "viewer_name_update") {
        QString vid = obj.value("viewer_id").toString();
        QString vname = obj.value("viewer_name").toString();
        emit viewerNameUpdateReceived(vid, vname);
    } else if (type == "viewer_exit" || type == "viewer_disconnected") {
        QString vid = obj.value("viewer_id").toString();
        if (!vid.isEmpty()) {
            emit viewerExited(vid);
        }
    }
}



void WebSocketSender::updateSenderStats(qint64 encodingTime, qint64 sendingTime, qint64 serializationTime, qint64 bytesSent)
{
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
    // 更新基本统计
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
        }
        m_lastStatsUpdateTime = currentTime;
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
    
    
}

void WebSocketSender::onSendTimer()
{
    QMutexLocker locker(&m_mutex);
    if (!m_connected || !m_webSocket || !m_isStreaming) {
        m_sendTimer->stop();
        return;
    }
    int burst = (m_frameQueue.size() >= 4) ? 3 : 2;
    while (burst-- > 0 && !m_frameQueue.isEmpty()) {
        QByteArray data = m_frameQueue.dequeue();
        bool key = m_keyQueue.dequeue();
        Q_UNUSED(key);
        qint64 bytesSent = m_webSocket->sendBinaryMessage(data);
        if (bytesSent > 0) {
            m_totalBytesSent += bytesSent;
            m_totalFramesSent++;
            emit frameSent(data.size());
        }
    }
    if (m_frameQueue.isEmpty()) {
        m_sendTimer->stop();
    }
}
bool WebSocketSender::isManualApprovalEnabled() const
{
    QStringList paths;
    paths << QCoreApplication::applicationDirPath() + "/config/app_config.txt";
    paths << QDir::currentPath() + "/config/app_config.txt";
    QString configPath;
    for (const QString &p : paths) {
        QFile f(p);
        if (f.exists()) { configPath = p; break; }
    }
    if (configPath.isEmpty()) return false;
    QFile f(configPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("manual_approval_enabled=")) {
                QString v = line.mid(QString("manual_approval_enabled=").length()).trimmed();
                f.close();
                return v.compare("true", Qt::CaseInsensitive) == 0 || v == "1";
            }
        }
        f.close();
    }
    return false;
}

void WebSocketSender::sendApprovalRequired(const QString &viewerId, const QString &targetId)
{
    QJsonObject response;
    response["type"] = "approval_required";
    response["viewer_id"] = viewerId;
    response["target_id"] = targetId;
    QJsonDocument responseDoc(response);
    sendTextMessage(responseDoc.toJson(QJsonDocument::Compact));
}

void WebSocketSender::sendWatchAccepted(const QString &viewerId, const QString &targetId)
{
    QJsonObject response;
    response["type"] = "watch_request_accepted";
    response["viewer_id"] = viewerId;
    response["target_id"] = targetId;
    QJsonDocument responseDoc(response);
    sendTextMessage(responseDoc.toJson(QJsonDocument::Compact));
}

void WebSocketSender::sendWatchRejected(const QString &viewerId, const QString &targetId)
{
    QJsonObject response;
    response["type"] = "watch_request_rejected";
    response["viewer_id"] = viewerId;
    response["target_id"] = targetId;
    QJsonDocument responseDoc(response);
    sendTextMessage(responseDoc.toJson(QJsonDocument::Compact));
}

void WebSocketSender::approveWatchRequest()
{
    if (!m_waitingForApproval) return;
    if (m_isStreaming) {
        stopStreaming();
    }
    startStreaming();
    emit requestKeyFrame();
    sendWatchAccepted(m_pendingViewerId, m_pendingTargetId);
    {
        QJsonObject ok;
        ok["type"] = "streaming_ok";
        ok["viewer_id"] = m_pendingViewerId;
        ok["target_id"] = m_pendingTargetId;
        QJsonDocument okDoc(ok);
        sendTextMessage(okDoc.toJson(QJsonDocument::Compact));
    }
    m_waitingForApproval = false;
}

void WebSocketSender::rejectWatchRequest()
{
    if (!m_waitingForApproval) return;
    sendWatchRejected(m_pendingViewerId, m_pendingTargetId);
    m_waitingForApproval = false;
}
