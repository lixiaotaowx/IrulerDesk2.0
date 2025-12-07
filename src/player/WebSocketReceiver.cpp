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
#include <iostream>
#include <QtMultimedia/QAudioSource>
#include <QtMultimedia/QMediaDevices>
#include <QtMultimedia/QAudioFormat>
#include <QVector>

WebSocketReceiver::WebSocketReceiver(QObject *parent)
    : QObject(parent)
    , m_webSocket(nullptr)
    , m_connected(false)
    , m_reconnectEnabled(true)
    , m_reconnectTimer(nullptr)
    , m_reconnectAttempts(0)
    , m_maxReconnectAttempts(10)
    , m_reconnectInterval(300) // 更低延迟的重连基准间隔
    , m_statsTimer(nullptr)
    , m_connectionStartTime(0)
    , m_lastStatsUpdateTime(0)
    , m_totalDowntimeStart(0)
{
    setupWebSocket();
    
    memset(&m_stats, 0, sizeof(m_stats));
    m_lastStatsUpdateTime = QDateTime::currentMSecsSinceEpoch();
    
    // 设置统计更新定时器
    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &WebSocketReceiver::updateStats);
    m_statsTimer->start(1000); // 每秒更新一次统计
    
    // 音频：初始化20ms节拍的抖动缓冲定时器
    m_audioTimer = new QTimer(this);
    m_audioTimer->setInterval(20);
    connect(m_audioTimer, &QTimer::timeout, [this]() {
        int baseSr = m_opusInitialized ? m_opusSampleRate : 16000;
        int outCh = 1;
        int frameSamples = m_audioFrameSamples > 0 ? m_audioFrameSamples : (baseSr / 50);
        QByteArray mixOut;
        mixOut.resize(frameSamples * outCh * sizeof(opus_int16));
        memset(mixOut.data(), 0, mixOut.size());
        bool anySource = false;

        if (m_opusInitialized && m_opusDecoder) {
            QByteArray opusData;
            if (!m_opusQueue.isEmpty()) opusData = m_opusQueue.dequeue();
            QByteArray pcm;
            pcm.resize(frameSamples * m_opusChannels * sizeof(opus_int16));
            int decoded = opus_decode(m_opusDecoder,
                                      opusData.isEmpty() ? nullptr : reinterpret_cast<const unsigned char*>(opusData.constData()),
                                      opusData.isEmpty() ? 0 : opusData.size(),
                                      reinterpret_cast<opus_int16*>(pcm.data()),
                                      frameSamples,
                                      0);
            if (decoded > 0) {
                pcm.resize(decoded * m_opusChannels * sizeof(opus_int16));
                const opus_int16* src = reinterpret_cast<const opus_int16*>(pcm.constData());
                opus_int16* dst = reinterpret_cast<opus_int16*>(mixOut.data());
                for (int i = 0; i < decoded; ++i) {
                    int s = dst[i] + src[i];
                    if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
                    dst[i] = static_cast<opus_int16>(s);
                }
                anySource = true;
            }
        }

        for (auto it = m_peerDecoders.begin(); it != m_peerDecoders.end(); ++it) {
            OpusDecoder* dec = it.value();
            if (!dec) continue;
            int ps = m_peerFrameSamples.value(it.key(), frameSamples);
            QByteArray qd;
            QQueue<QByteArray>& q = m_peerQueues[it.key()];
            if (!q.isEmpty()) qd = q.dequeue();
            QByteArray pcm;
            pcm.resize(ps * outCh * sizeof(opus_int16));
            int decoded = opus_decode(dec,
                                      qd.isEmpty() ? nullptr : reinterpret_cast<const unsigned char*>(qd.constData()),
                                      qd.isEmpty() ? 0 : qd.size(),
                                      reinterpret_cast<opus_int16*>(pcm.data()),
                                      ps,
                                      0);
            if (decoded > 0) {
                pcm.resize(decoded * outCh * sizeof(opus_int16));
                const opus_int16* src = reinterpret_cast<const opus_int16*>(pcm.constData());
                opus_int16* dst = reinterpret_cast<opus_int16*>(mixOut.data());
                int n = std::min(decoded, frameSamples);
                for (int i = 0; i < n; ++i) {
                    int s = dst[i] + src[i];
                    if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
                    dst[i] = static_cast<opus_int16>(s);
                }
                anySource = true;
            }
        }

        if (m_audioLastTimestamp == 0) {
            m_audioLastTimestamp = QDateTime::currentMSecsSinceEpoch() * 1000;
        } else {
            m_audioLastTimestamp += 20000;
        }
        emit audioFrameReceived(mixOut, baseSr, outCh, 16, m_audioLastTimestamp);
        if (!anySource) {
            int readyPeers = 0;
            for (auto itq = m_peerQueues.begin(); itq != m_peerQueues.end(); ++itq) { if (!itq.value().isEmpty()) { readyPeers++; break; } }
            if (!readyPeers && m_opusQueue.isEmpty()) {
                if (m_audioTimer->isActive()) m_audioTimer->stop();
            }
        }
    });
    // 定时器按需在收到第一帧后启动
    
    // 设置重连定时器
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &WebSocketReceiver::attemptReconnect);
    
    // 环境变量控制已移除
    // 日志清理：移除冗余控制台输出

    m_inputPollTimer = new QTimer(this);
    m_inputPollTimer->setInterval(500);
    connect(m_inputPollTimer, &QTimer::timeout, this, [this]() {
        if (!m_followSystemInput) return;
        QAudioDevice defIn = QMediaDevices::defaultAudioInput();
        if (m_currentInputDeviceId != defIn.id()) {
            bool wasActive = m_localAudioTimer && m_localAudioTimer->isActive();
            setLocalInputDeviceFollowSystem();
            if (wasActive) setTalkEnabled(true);
        }
    });
    m_inputPollTimer->start();
}


WebSocketReceiver::~WebSocketReceiver()
{
    disconnectFromServer();
    
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
        return true;
    }
    
    // 如果已经连接但URL不同，先断开之前的连接
    if (m_connected) {
        m_reconnectEnabled = false;
        stopReconnectTimer();
        if (m_webSocket) {
            m_webSocket->close();
        }
        m_connected = false;
    }
    
    // 清理缓存数据，确保切换设备时没有残留
    memset(&m_stats, 0, sizeof(m_stats));
    m_frameSizes.clear();
    m_connectionStartTime = 0;
    m_reconnectAttempts = 0; // 主动连接归零重连计数，确保首次尝试立即进行
    m_serverUrl = url;
    m_reconnectEnabled = true;

    m_webSocket->open(QUrl(url));
    return true;
}

void WebSocketReceiver::disconnectFromServer()
{
    QMutexLocker locker(&m_mutex);
    
    m_reconnectEnabled = false;
    stopReconnectTimer();
    
    if (m_webSocket && m_connected) {
        // 在断开前通知采集端停止推流
        QJsonObject stopMsg;
        stopMsg["type"] = "stop_streaming";
        QJsonDocument stopDoc(stopMsg);
        m_webSocket->sendTextMessage(stopDoc.toJson(QJsonDocument::Compact));

        m_webSocket->close();
    }
    
    // 停止音频定时器与清空音频队列，销毁解码器，避免断开后仍有PLC输出
    if (m_audioTimer && m_audioTimer->isActive()) {
        m_audioTimer->stop();
    }
    m_opusQueue.clear();
    for (auto it = m_peerDecoders.begin(); it != m_peerDecoders.end(); ++it) {
        if (it.value()) opus_decoder_destroy(it.value());
    }
    m_peerDecoders.clear();
    m_peerQueues.clear();
    m_peerSampleRates.clear();
    m_peerChannels.clear();
    m_peerFrameSamples.clear();
    if (m_opusDecoder) {
        opus_decoder_destroy(m_opusDecoder);
        m_opusDecoder = nullptr;
    }
    m_opusInitialized = false;
    m_audioLastTimestamp = 0;
    m_audioFrameSamples = 0;

    // 清理缓存数据
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
    // 先在互斥保护下更新内部状态并抓取最近的viewer/target副本，然后释放锁
    QString viewerIdCopy;
    QString targetIdCopy;
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

        viewerIdCopy = m_lastViewerId;
        targetIdCopy = m_lastTargetId;
    }

    emit connected();
    emit connectionStatusChanged("已连接");

    // 日志清理：移除冗余连接状态输出

    // 重连后自动重发观看请求，确保推流立即恢复（避免互斥锁重入造成死锁）
    if (m_autoResendWatchRequest && !viewerIdCopy.isEmpty() && !targetIdCopy.isEmpty()) {
        sendWatchRequest(viewerIdCopy, targetIdCopy);
    }
}

void WebSocketReceiver::onDisconnected()
{
    QMutexLocker locker(&m_mutex);
    
    // 断开后确保音频完全停止
    if (m_audioTimer && m_audioTimer->isActive()) {
        m_audioTimer->stop();
    }
    m_opusQueue.clear();
    for (auto it = m_peerDecoders.begin(); it != m_peerDecoders.end(); ++it) {
        if (it.value()) opus_decoder_destroy(it.value());
    }
    m_peerDecoders.clear();
    m_peerQueues.clear();
    m_peerSampleRates.clear();
    m_peerChannels.clear();
    m_peerFrameSamples.clear();
    if (m_opusDecoder) {
        opus_decoder_destroy(m_opusDecoder);
        m_opusDecoder = nullptr;
    }
    m_opusInitialized = false;
    m_audioLastTimestamp = 0;
    m_audioFrameSamples = 0;

    if (m_localAudioTimer && m_localAudioTimer->isActive()) {
        m_localAudioTimer->stop();
    }
    if (m_localAudioSource) {
        m_localAudioSource->stop();
    }
    m_localAudioInput = nullptr;
    if (m_localOpusEnc) { opus_encoder_destroy(m_localOpusEnc); m_localOpusEnc = nullptr; }

    bool wasConnected = m_connected;
    m_connected = false;
    
    // 性能监控：记录断线开始时间
    if (wasConnected) {
        m_totalDowntimeStart = QDateTime::currentMSecsSinceEpoch();
    }
    
    if (m_connectionStartTime > 0) {
        Q_UNUSED(wasConnected);
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
        return;
    }
    
    QJsonObject header = doc.object();
    QString messageType = header["type"].toString();
    
    // 提取二进制数据部分
    QByteArray binaryData = message.mid(4 + headerLength);
    
    // 根据消息类型处理
    // 处理其他类型的消息（如传统帧数据）
}

void WebSocketReceiver::onTextMessageReceived(const QString &message)
{
    // 只在调试模式下输出日志，避免影响性能
    
    
    // 解析JSON消息以处理特定类型
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    
    if (error.error == QJsonParseError::NoError && doc.isObject()) {
        QJsonObject obj = doc.object();
        QString type = obj["type"].toString();
        // 日志清理：不再输出文本消息类型
        
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
            }
            return;
        } else if (type == "audio_pcm") {
            int sampleRate = obj.value("sample_rate").toInt(16000);
            int channels = obj.value("channels").toInt(1);
            int bitsPerSample = obj.value("bits_per_sample").toInt(16);
            qint64 timestamp = obj.value("timestamp").toVariant().toLongLong();
            QString base64 = obj.value("data_base64").toString();
            QByteArray pcm = QByteArray::fromBase64(base64.toUtf8());

            // 日志清理：移除音频帧接收日志

            emit audioFrameReceived(pcm, sampleRate, channels, bitsPerSample, timestamp);
            return;
        } else if (type == "audio_opus") {
            int sampleRate = obj.value("sample_rate").toInt(16000);
            int channels = obj.value("channels").toInt(1);
            int frameSamples = obj.value("frame_samples").toInt(sampleRate / 50);
            qint64 timestamp = obj.value("timestamp").toVariant().toLongLong();
            QString base64 = obj.value("data_base64").toString();
            QByteArray opusData = QByteArray::fromBase64(base64.toUtf8());

            initOpusDecoderIfNeeded(sampleRate, channels);
            if (!m_opusInitialized || !m_opusDecoder) {
                return; // 解码器不可用
            }
            // 入队以按固定20ms节拍解码，降低颤抖
            m_opusSampleRate = sampleRate;
            m_opusChannels = channels;
            m_audioFrameSamples = frameSamples;
            m_opusQueue.enqueue(opusData);
            if (!m_audioTimer->isActive()) {
                if (m_opusQueue.size() >= m_audioPrebufferFrames) {
                    m_audioLastTimestamp = timestamp;
                    m_audioTimer->start();
                }
            }
            return;
        } else if (type == "viewer_audio_opus") {
            QString fromId = obj.value("viewer_id").toString();
            if (!fromId.isEmpty()) {
                QMutexLocker locker(&m_mutex);
                if (fromId == m_lastViewerId) {
                    return;
                }
            }
            int sampleRate = obj.value("sample_rate").toInt(16000);
            int channels = obj.value("channels").toInt(1);
            int frameSamples = obj.value("frame_samples").toInt(sampleRate / 50);
            qint64 timestamp = obj.value("timestamp").toVariant().toLongLong();
            QByteArray opusData = QByteArray::fromBase64(obj.value("data_base64").toString().toUtf8());
            OpusDecoder* dec = m_peerDecoders.value(fromId, nullptr);
            if (!dec || m_peerSampleRates.value(fromId) != sampleRate || m_peerChannels.value(fromId) != channels) {
                if (dec) { opus_decoder_destroy(dec); }
                int err = OPUS_OK;
                dec = opus_decoder_create(sampleRate, channels, &err);
                if (err != OPUS_OK || !dec) {
                    return;
                }
                m_peerDecoders[fromId] = dec;
                m_peerSampleRates[fromId] = sampleRate;
                m_peerChannels[fromId] = channels;
            }
            m_peerFrameSamples[fromId] = frameSamples;
            m_peerQueues[fromId].enqueue(opusData);
            if (!m_audioTimer->isActive()) {
                bool ready = m_opusQueue.size() >= m_audioPrebufferFrames;
                if (!ready) {
                    for (auto it = m_peerQueues.begin(); it != m_peerQueues.end(); ++it) {
                        if (it.value().size() >= m_audioPrebufferFrames) { ready = true; break; }
                    }
                }
                if (ready) { m_audioLastTimestamp = timestamp; m_audioTimer->start(); }
            }
            return;
        } else if (type == "streaming_ok") {
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
    
    emit connectionError(errorString);
}

void WebSocketReceiver::onSslErrors(const QList<QSslError> &errors)
{
    for (const QSslError &error : errors) {
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
    
    
    if (m_reconnectAttempts <= m_maxReconnectAttempts) {
        setupWebSocket(); // 重新创建WebSocket对象
        m_webSocket->open(QUrl(m_serverUrl));
    } else {
        emit connectionError("达到最大重连次数");
    }
}

void WebSocketReceiver::startReconnectTimer()
{
    if (!m_reconnectTimer) return;
    // 首次重连立即尝试，以贴近首次启动速度
    if (m_reconnectAttempts == 0) {
        QTimer::singleShot(0, this, &WebSocketReceiver::attemptReconnect);
        return;
    }
    if (!m_reconnectTimer->isActive()) {
        int interval = m_reconnectInterval * (1 + m_reconnectAttempts); // 低基准退避
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

void WebSocketReceiver::initOpusDecoderIfNeeded(int sampleRate, int channels)
{
    if (!m_opusInitialized || sampleRate != m_opusSampleRate || channels != m_opusChannels) {
        if (m_opusDecoder) {
            opus_decoder_destroy(m_opusDecoder);
            m_opusDecoder = nullptr;
        }
        int err = OPUS_OK;
        m_opusDecoder = opus_decoder_create(sampleRate, channels, &err);
        if (err == OPUS_OK && m_opusDecoder) {
            m_opusInitialized = true;
            m_opusSampleRate = sampleRate;
            m_opusChannels = channels;
        } else {
            m_opusInitialized = false;
        }
    }
}

void WebSocketReceiver::sendWatchRequest(const QString &viewerId, const QString &targetId)
{
    // 记录最近一次观看请求信息，用于重连后自动重发
    {
        QMutexLocker locker(&m_mutex);
        m_lastViewerId = viewerId;
        m_lastTargetId = targetId;
    }

    if (!m_connected || !m_webSocket) {
        return;
    }

    // 构造观看请求消息
    QJsonObject message;
    message["type"] = "watch_request";
    message["viewer_id"] = viewerId;
    message["target_id"] = targetId;
    if (!m_lastViewerName.isEmpty()) {
        message["viewer_name"] = m_lastViewerName;
    }
    
    QJsonDocument doc(message);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    // 日志清理：移除观看请求打印
    
    m_webSocket->sendTextMessage(jsonString);
    
    // 立即发送开始推流请求
    QJsonObject startStreamingMessage;
    startStreamingMessage["type"] = "start_streaming";
    
    QJsonDocument startStreamingDoc(startStreamingMessage);
    QString startStreamingJsonString = startStreamingDoc.toJson(QJsonDocument::Compact);
    // 日志清理：移除开始推流提示
    
    m_webSocket->sendTextMessage(startStreamingJsonString);
}

void WebSocketReceiver::setViewerName(const QString &name)
{
    QMutexLocker locker(&m_mutex);
    m_lastViewerName = name;
    if (m_connected && m_webSocket) {
        QString viewerId = m_lastViewerId;
        QString targetId = m_lastTargetId;
        if (!viewerId.isEmpty() && !targetId.isEmpty()) {
            QJsonObject message;
            message["type"] = "viewer_name_update";
            message["viewer_id"] = viewerId;
            message["target_id"] = targetId;
            message["viewer_name"] = m_lastViewerName;
            message["timestamp"] = QDateTime::currentMSecsSinceEpoch();
            QJsonDocument doc(message);
            m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
        }
    }
}

void WebSocketReceiver::sendViewerCursor(int x, int y)
{
    if (!m_connected || !m_webSocket) {
        return;
    }
    QString viewerId;
    QString targetId;
    {
        QMutexLocker locker(&m_mutex);
        viewerId = m_lastViewerId;
        targetId = m_lastTargetId;
    }
    if (viewerId.isEmpty() || targetId.isEmpty()) {
        return;
    }
    QJsonObject message;
    message["type"] = "viewer_cursor";
    message["viewer_id"] = viewerId;
    message["target_id"] = targetId;
    message["x"] = x;
    message["y"] = y;
    if (!m_lastViewerName.isEmpty()) {
        message["viewer_name"] = m_lastViewerName;
    }
    message["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    QJsonDocument doc(message);
    m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
}

void WebSocketReceiver::sendAnnotationEvent(const QString &phase, int x, int y, int colorId)
{
    if (!m_connected || !m_webSocket) {
        return;
    }

    // 使用最近一次的viewer/target信息进行路由
    QString viewerId;
    QString targetId;
    {
        QMutexLocker locker(&m_mutex);
        viewerId = m_lastViewerId;
        targetId = m_lastTargetId;
    }

    if (viewerId.isEmpty() || targetId.isEmpty()) {
        return;
    }

    QJsonObject message;
    message["type"] = "annotation_event";
    message["phase"] = phase; // "down" / "move" / "up"
    message["x"] = x;
    message["y"] = y;
    message["viewer_id"] = viewerId;
    message["target_id"] = targetId;
    message["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    message["color_id"] = colorId;

    QJsonDocument doc(message);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    // 降低日志噪音：不打印高频的 move 事件，仅统计；其他关键阶段简要打印
    static int moveEventCount = 0;
    if (phase == "move") {
        moveEventCount++;
    } else {
    }
    m_webSocket->sendTextMessage(jsonString);
}

void WebSocketReceiver::sendTextAnnotation(const QString &text, int x, int y, int colorId, int fontSize)
{
    if (!m_connected || !m_webSocket) {
        return;
    }

    QString viewerId;
    QString targetId;
    {
        QMutexLocker locker(&m_mutex);
        viewerId = m_lastViewerId;
        targetId = m_lastTargetId;
    }
    if (viewerId.isEmpty() || targetId.isEmpty()) {
        return;
    }

    QJsonObject message;
    message["type"] = "text_annotation";
    message["text"] = text;
    message["x"] = x;
    message["y"] = y;
    message["color_id"] = colorId;
    message["font_size"] = fontSize;
    message["viewer_id"] = viewerId;
    message["target_id"] = targetId;
    message["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(message);
    m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
}

void WebSocketReceiver::sendSwitchScreenNext()
{
    if (!m_connected || !m_webSocket) {
        return;
    }

    QString viewerId;
    QString targetId;
    {
        QMutexLocker locker(&m_mutex);
        viewerId = m_lastViewerId;
        targetId = m_lastTargetId;
    }

    if (viewerId.isEmpty() || targetId.isEmpty()) {
        return;
    }

    QJsonObject message;
    message["type"] = "switch_screen";
    message["direction"] = "next";
    message["viewer_id"] = viewerId;
    message["target_id"] = targetId;
    message["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(message);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    m_webSocket->sendTextMessage(jsonString);
}

void WebSocketReceiver::sendLikeEvent()
{
    if (!m_connected || !m_webSocket) {
        return;
    }
    QString viewerId;
    QString targetId;
    {
        QMutexLocker locker(&m_mutex);
        viewerId = m_lastViewerId;
        targetId = m_lastTargetId;
    }
    if (viewerId.isEmpty() || targetId.isEmpty()) {
        return;
    }
    QJsonObject message;
    message["type"] = "like_event";
    message["viewer_id"] = viewerId;
    message["target_id"] = targetId;
    message["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    QJsonDocument doc(message);
    m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
}

void WebSocketReceiver::sendSwitchScreenIndex(int index)
{
    if (!m_connected || !m_webSocket) {
        return;
    }

    QString viewerId;
    QString targetId;
    {
        QMutexLocker locker(&m_mutex);
        viewerId = m_lastViewerId;
        targetId = m_lastTargetId;
    }

    if (viewerId.isEmpty() || targetId.isEmpty()) {
        return;
    }

    QJsonObject message;
    message["type"] = "switch_screen";
    message["direction"] = "index";
    message["index"] = index;
    message["viewer_id"] = viewerId;
    message["target_id"] = targetId;
    message["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(message);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    m_webSocket->sendTextMessage(jsonString);
}

void WebSocketReceiver::sendStopStreaming()
{
    if (!m_connected || !m_webSocket) {
        return;
    }
    QJsonObject stopMsg;
    stopMsg["type"] = "stop_streaming";
    QJsonDocument stopDoc(stopMsg);
    m_webSocket->sendTextMessage(stopDoc.toJson(QJsonDocument::Compact));
}

void WebSocketReceiver::sendSetQuality(const QString &quality)
{
    if (!m_connected || !m_webSocket) {
        return;
    }

    QString viewerId;
    QString targetId;
    {
        QMutexLocker locker(&m_mutex);
        viewerId = m_lastViewerId;
        targetId = m_lastTargetId;
    }

    if (viewerId.isEmpty() || targetId.isEmpty()) {
        return;
    }

    QString normalized = quality.toLower();
    if (normalized != "low" && normalized != "medium" && normalized != "high" && normalized != "extreme") {
        normalized = "medium";
    }

    QJsonObject message;
    message["type"] = "set_quality";
    message["quality"] = normalized;
    message["viewer_id"] = viewerId;
    message["target_id"] = targetId;
    message["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(message);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    m_webSocket->sendTextMessage(jsonString);
}

void WebSocketReceiver::sendAudioToggle(bool enabled)
{
    if (!m_connected || !m_webSocket) {
        return;
    }

    QString viewerId;
    QString targetId;
    {
        QMutexLocker locker(&m_mutex);
        viewerId = m_lastViewerId;
        targetId = m_lastTargetId;
    }

    if (viewerId.isEmpty() || targetId.isEmpty()) {
        // 没有观看会话信息则不发送
        return;
    }

    QJsonObject message;
    message["type"] = "audio_toggle";
    message["enabled"] = enabled;
    message["viewer_id"] = viewerId;
    message["target_id"] = targetId;
    message["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(message);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    m_webSocket->sendTextMessage(jsonString);
}

void WebSocketReceiver::sendViewerListenMute(bool mute)
{
    if (!m_connected || !m_webSocket) {
        return;
    }

    QString viewerId;
    QString targetId;
    {
        QMutexLocker locker(&m_mutex);
        viewerId = m_lastViewerId;
        targetId = m_lastTargetId;
    }

    if (viewerId.isEmpty() || targetId.isEmpty()) {
        return;
    }

    QJsonObject message;
    message["type"] = "viewer_listen_mute";
    message["mute"] = mute;
    message["viewer_id"] = viewerId;
    message["target_id"] = targetId;
    message["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(message);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    m_webSocket->sendTextMessage(jsonString);
}

void WebSocketReceiver::sendAudioGain(int percent)
{
    if (!m_connected || !m_webSocket) {
        return;
    }

    QString viewerId;
    QString targetId;
    {
        QMutexLocker locker(&m_mutex);
        viewerId = m_lastViewerId;
        targetId = m_lastTargetId;
    }

    if (viewerId.isEmpty() || targetId.isEmpty()) {
        return;
    }

    int p = percent;
    if (p < 0) p = 0;
    if (p > 100) p = 100;

    QJsonObject message;
    message["type"] = "audio_gain";
    message["percent"] = p;
    message["viewer_id"] = viewerId;
    message["target_id"] = targetId;
    message["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc(message);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    m_webSocket->sendTextMessage(jsonString);
}

void WebSocketReceiver::setTalkEnabled(bool enabled)
{
    if (enabled) {
        m_talkActive = true;
        if (!m_localAudioSource) {
            QAudioDevice inDev = QMediaDevices::defaultAudioInput();
            if (!m_followSystemInput && !m_localInputDeviceId.isEmpty()) {
                const auto devs = QMediaDevices::audioInputs();
                for (const auto &d : devs) { if (d.id() == m_localInputDeviceId) { inDev = d; break; } }
            }
            QAudioFormat fmt;
            fmt.setSampleRate(m_localOpusSampleRate);
            fmt.setChannelCount(1);
            fmt.setSampleFormat(QAudioFormat::Int16);
            if (!inDev.isFormatSupported(fmt)) {
                QAudioFormat pf = inDev.preferredFormat();
                m_localInputFormat = pf;
            } else {
                m_localInputFormat = fmt;
            }
            m_localAudioSource = new QAudioSource(inDev, m_localInputFormat, this);
            QObject::connect(m_localAudioSource, &QAudioSource::stateChanged, this, [this](QAudio::State st){ handleLocalAudioState(st); });
            m_currentInputDeviceId = inDev.id();
        }
        if (!m_localOpusEnc) {
            int err = OPUS_OK;
            m_localOpusEnc = opus_encoder_create(m_localOpusSampleRate, 1, OPUS_APPLICATION_VOIP, &err);
            if (err == OPUS_OK && m_localOpusEnc) {
                opus_encoder_ctl(m_localOpusEnc, OPUS_SET_BITRATE(24000));
                opus_encoder_ctl(m_localOpusEnc, OPUS_SET_VBR(1));
                opus_encoder_ctl(m_localOpusEnc, OPUS_SET_COMPLEXITY(5));
                opus_encoder_ctl(m_localOpusEnc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                opus_encoder_ctl(m_localOpusEnc, OPUS_SET_INBAND_FEC(1));
            }
        }
        if (!m_localAudioTimer) {
            m_localAudioTimer = new QTimer(this);
            m_localAudioTimer->setInterval(20);
            connect(m_localAudioTimer, &QTimer::timeout, this, [this]() {
                if (!m_localOpusEnc || !m_localAudioSource) return;
                if (!m_localAudioInput) return;
                QByteArray pcm;
                if (!produceOpusFrame(pcm)) return;
                QByteArray opusOut;
                opusOut.resize(4096);
                int nbytes = opus_encode(m_localOpusEnc,
                                         reinterpret_cast<const opus_int16*>(pcm.constData()),
                                         m_localOpusFrameSize,
                                         reinterpret_cast<unsigned char*>(opusOut.data()),
                                         opusOut.size());
                if (nbytes < 0) return;
                opusOut.resize(nbytes);
                QString viewerIdCopy;
                QString targetIdCopy;
                {
                    QMutexLocker locker(&m_mutex);
                    viewerIdCopy = m_lastViewerId;
                    targetIdCopy = m_lastTargetId;
                }
                QJsonObject message;
                message["type"] = "viewer_audio_opus";
                message["sample_rate"] = m_localOpusSampleRate;
                message["channels"] = 1;
                message["frame_samples"] = m_localOpusFrameSize;
                message["timestamp"] = QDateTime::currentMSecsSinceEpoch();
                message["data_base64"] = QString::fromUtf8(opusOut.toBase64());
                if (!viewerIdCopy.isEmpty()) message["viewer_id"] = viewerIdCopy;
                if (!targetIdCopy.isEmpty()) message["target_id"] = targetIdCopy;
                QJsonDocument doc(message);
                m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
            });
        }
        if (m_localAudioSource->state() != QAudio::ActiveState) {
            m_localAudioInput = m_localAudioSource->start();
            m_localAudioSource->setVolume(m_localMicGainPercent / 100.0);
        }
        if (!m_localAudioTimer->isActive()) m_localAudioTimer->start();
    } else {
        m_talkActive = false;
        if (m_localAudioTimer && m_localAudioTimer->isActive()) m_localAudioTimer->stop();
        if (m_localAudioSource) m_localAudioSource->stop();
        m_localAudioInput = nullptr;
        if (m_localOpusEnc) { opus_encoder_destroy(m_localOpusEnc); m_localOpusEnc = nullptr; }
    }
}

void WebSocketReceiver::setLocalInputDeviceFollowSystem()
{
    m_followSystemInput = true;
    m_localInputDeviceId.clear();
    if (m_localAudioSource) {
        bool active = m_localAudioSource->state() == QAudio::ActiveState;
        m_localAudioSource->stop();
        delete m_localAudioSource;
        m_localAudioSource = nullptr;
        if (active) { setTalkEnabled(true); }
    }
}

void WebSocketReceiver::setLocalInputDeviceById(const QString &id)
{
    m_followSystemInput = false;
    m_localInputDeviceId = id;
    if (m_localAudioSource) {
        bool active = m_localAudioSource->state() == QAudio::ActiveState;
        m_localAudioSource->stop();
        delete m_localAudioSource;
        m_localAudioSource = nullptr;
        if (active) { setTalkEnabled(true); }
    }
}

void WebSocketReceiver::recreateLocalAudioSource()
{
    if (m_localAudioSource) {
        bool active = m_localAudioSource->state() == QAudio::ActiveState;
        m_localAudioSource->stop();
        delete m_localAudioSource;
        m_localAudioSource = nullptr;
        QAudioDevice inDev = QMediaDevices::defaultAudioInput();
        if (!m_followSystemInput && !m_localInputDeviceId.isEmpty()) {
            const auto devs = QMediaDevices::audioInputs();
            for (const auto &d : devs) { if (d.id() == m_localInputDeviceId) { inDev = d; break; } }
        }
        QAudioFormat fmt;
        fmt.setSampleRate(m_localOpusSampleRate);
        fmt.setChannelCount(1);
        fmt.setSampleFormat(QAudioFormat::Int16);
        if (!inDev.isFormatSupported(fmt)) {
            QAudioFormat pf = inDev.preferredFormat();
            m_localInputFormat = pf;
        } else {
            m_localInputFormat = fmt;
        }
        m_localAudioSource = new QAudioSource(inDev, m_localInputFormat, this);
        QObject::connect(m_localAudioSource, &QAudioSource::stateChanged, this, [this](QAudio::State st){ handleLocalAudioState(st); });
        m_currentInputDeviceId = inDev.id();
        if (active) {
            m_localAudioInput = m_localAudioSource->start();
            m_localAudioSource->setVolume(m_localMicGainPercent / 100.0);
        }
    }
}

void WebSocketReceiver::handleLocalAudioState(QAudio::State st)
{
    if (!m_localAudioSource) return;
    if (st == QAudio::StoppedState && m_localAudioSource->error() != QAudio::NoError) {
        recreateLocalAudioSource();
    }
}

int WebSocketReceiver::bytesPerSample(QAudioFormat::SampleFormat f) const
{
    if (f == QAudioFormat::UInt8) return 1;
    if (f == QAudioFormat::Int16) return 2;
    if (f == QAudioFormat::Int32 || f == QAudioFormat::Float) return 4;
    return 2;
}

bool WebSocketReceiver::produceOpusFrame(QByteArray &out)
{
    if (!m_localAudioInput) return false;
    int srcRate = m_localInputFormat.sampleRate();
    int srcCh = m_localInputFormat.channelCount();
    QAudioFormat::SampleFormat sf = m_localInputFormat.sampleFormat();
    int bps = bytesPerSample(sf);
    if (srcRate == m_localOpusSampleRate && srcCh == 1 && sf == QAudioFormat::Int16) {
        int needBytes = m_localOpusFrameSize * bps;
        QByteArray buf;
        buf.resize(needBytes);
        qint64 readBytes = m_localAudioInput->read(buf.data(), buf.size());
        if (readBytes < buf.size()) return false;
        out = buf;
        return true;
    }
    int needSrcSamples = (m_localOpusFrameSize * srcRate + m_localOpusSampleRate - 1) / m_localOpusSampleRate;
    int needBytes = needSrcSamples * srcCh * bps;
    QByteArray tmp;
    tmp.resize(needBytes);
    qint64 readBytes = m_localAudioInput->read(tmp.data(), tmp.size());
    if (readBytes < tmp.size()) return false;
    int samples = needSrcSamples;
    QVector<float> mono(samples);
    if (sf == QAudioFormat::Int16) {
        const qint16 *p = reinterpret_cast<const qint16*>(tmp.constData());
        for (int i = 0; i < samples; ++i) {
            int idx = i * srcCh;
            float v = 0.f;
            for (int c = 0; c < srcCh; ++c) v += p[idx + c];
            v /= srcCh;
            mono[i] = v / 32768.f;
        }
    } else if (sf == QAudioFormat::Float) {
        const float *p = reinterpret_cast<const float*>(tmp.constData());
        for (int i = 0; i < samples; ++i) {
            int idx = i * srcCh;
            float v = 0.f;
            for (int c = 0; c < srcCh; ++c) v += p[idx + c];
            v /= srcCh;
            mono[i] = v;
        }
    } else {
        return false;
    }
    out.resize(m_localOpusFrameSize * 2);
    qint16 *dst = reinterpret_cast<qint16*>(out.data());
    for (int i = 0; i < m_localOpusFrameSize; ++i) {
        double pos = (double)i * srcRate / (double)m_localOpusSampleRate;
        int j = (int)pos;
        double frac = pos - j;
        float s0 = mono[qBound(0, j, samples - 1)];
        float s1 = mono[qBound(0, j + 1, samples - 1)];
        float v = s0 + (s1 - s0) * (float)frac;
        int iv = (int)std::round(qBound(-1.0f, v, 1.0f) * 32767.0f);
        dst[i] = (qint16)iv;
    }
    return true;
}

// 瓦片消息处理方法实现已移除
