#include "StreamClient.h"
#include <QBuffer>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "../common/AppConfig.h"

static QString wsRoleTagFromUrl(const QUrl &url)
{
    const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return QStringLiteral("unknown");
    if (parts.value(0) == QStringLiteral("subscribe")) return QStringLiteral("subscribe");
    if (parts.value(0) == QStringLiteral("publish")) return QStringLiteral("publish");
    return parts.value(0);
}

static QString wsRouteTagFromUrl(const QUrl &url)
{
    if (AppConfig::lanWsEnabled() && url.port() == AppConfig::lanWsPort()) {
        return QStringLiteral("LAN");
    }
    return QStringLiteral("CLOUD");
}

static QString wsPortTag(const QUrl &url)
{
    const int p = url.port();
    if (p <= 0) return QStringLiteral("-");
    return QString::number(p);
}

static QString scTag(const StreamClient *self)
{
    return QStringLiteral("[KickDiag][StreamClient][0x%1] ").arg(QString::number(reinterpret_cast<quintptr>(self), 16));
}

static QVector<quint32> localIpv4sForLanPick()
{
    QVector<quint32> out;
    const QStringList bases = AppConfig::localLanBaseUrls();
    out.reserve(bases.size());
    for (const QString &b : bases) {
        const QUrl u(b);
        QHostAddress ha(u.host());
        if (ha.protocol() != QAbstractSocket::IPv4Protocol) continue;
        const quint32 ip = ha.toIPv4Address();
        if (ip == 0) continue;
        out.push_back(ip);
    }
    return out;
}

static int ipv4LanScore(quint32 ip, const QVector<quint32> &locals)
{
    int score = 0;
    if ((ip & 0xFFFF0000u) == 0xC0A80000u) score += 300;
    else if ((ip & 0xFF000000u) == 0x0A000000u) score += 200;
    else if ((ip & 0xFFF00000u) == 0xAC100000u) score += 100;

    for (quint32 local : locals) {
        if (local == 0) continue;
        if (ip == local) {
            return -100000;
        }
        if ((ip & 0xFFFFFF00u) == (local & 0xFFFFFF00u)) {
            score += 1000;
        }
    }
    return score;
}

static QUrl pickBestLanSubscribeUrl(const QStringList &bases, const QString &channelId, const QUrl &currentUrl)
{
    const QVector<quint32> locals = localIpv4sForLanPick();

    QUrl best;
    int bestScore = -1;
    for (const QString &base : bases) {
        if (base.isEmpty()) continue;
        QUrl u(base);
        if (!u.isValid() || u.isEmpty()) continue;
        if (u.scheme() != QStringLiteral("ws") && u.scheme() != QStringLiteral("wss")) continue;
        QHostAddress ha(u.host());
        int s = -1;
        if (ha.protocol() == QAbstractSocket::IPv4Protocol) {
            const quint32 ip = ha.toIPv4Address();
            s = ipv4LanScore(ip, locals);
        } else {
            s = 0;
        }
        u.setPath(QStringLiteral("/subscribe/%1").arg(channelId));
        if (u == currentUrl) continue;
        if (s > bestScore) {
            bestScore = s;
            best = u;
        }
    }
    if (bestScore < 1000) {
        return QUrl();
    }
    return best;
}

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

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &StreamClient::attemptReconnect);

    m_lanFallbackTimer = new QTimer(this);
    m_lanFallbackTimer->setSingleShot(true);
    connect(m_lanFallbackTimer, &QTimer::timeout, this, [this]() {
        if (!m_lanSwitchInProgress) {
            return;
        }
        if (m_isConnected) {
            emit logMessage(scTag(this) + QStringLiteral("lan_fallback_timer ignored: already connected url=%1 route=%2")
                                .arg(m_lastUrl.toString(), wsRouteTagFromUrl(m_lastUrl)));
            m_lanSwitchInProgress = false;
            m_cloudFallbackUrl = QUrl();
            return;
        }
        if (!m_cloudFallbackUrl.isValid() || m_cloudFallbackUrl.isEmpty()) {
            emit logMessage(scTag(this) + QStringLiteral("lan_fallback_timer stop: invalid cloud_fallback url=%1")
                                .arg(m_lastUrl.toString()));
            m_lanSwitchInProgress = false;
            return;
        }
        const QUrl fallback = m_cloudFallbackUrl;
        m_lanSwitchInProgress = false;
        m_cloudFallbackUrl = QUrl();
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        m_lanSwitchDisabledUntilMs = nowMs + 60000;
        emit logMessage(scTag(this) + QStringLiteral("lan_fallback_timer fire: fallback_to_cloud url=%1")
                            .arg(fallback.toString()));
        connectToServer(fallback);
    });

    m_lanFirstFrameTimer = new QTimer(this);
    m_lanFirstFrameTimer->setSingleShot(true);
    connect(m_lanFirstFrameTimer, &QTimer::timeout, this, [this]() {
        if (!m_lanAwaitingFirstFrame) {
            return;
        }
        if (!isSubscribeUrl(m_lastUrl) || m_lastUrl.port() != AppConfig::lanWsPort()) {
            m_lanAwaitingFirstFrame = false;
            return;
        }
        if (!m_cloudFallbackUrl.isValid() || m_cloudFallbackUrl.isEmpty()) {
            m_lanAwaitingFirstFrame = false;
            m_lanSwitchInProgress = false;
            return;
        }
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        m_lanSwitchDisabledUntilMs = nowMs + 60000;
        const QUrl fallback = m_cloudFallbackUrl;
        m_cloudFallbackUrl = QUrl();
        m_lanAwaitingFirstFrame = false;
        m_lanSwitchInProgress = false;
        emit logMessage(scTag(this) + QStringLiteral("lan_first_frame_timeout fallback_to_cloud url=%1").arg(fallback.toString()));
        connectToServer(fallback);
    });

    m_lanOfferRetryTimer = new QTimer(this);
    m_lanOfferRetryTimer->setSingleShot(true);
    connect(m_lanOfferRetryTimer, &QTimer::timeout, this, [this]() {
        if (!m_isConnected) {
            return;
        }
        if (!AppConfig::lanWsEnabled()) {
            return;
        }
        if (!isSubscribeUrl(m_lastUrl)) {
            return;
        }
        if (m_lastUrl.port() == AppConfig::lanWsPort()) {
            return;
        }
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (m_lanSwitchDisabledUntilMs > 0 && nowMs < m_lanSwitchDisabledUntilMs) {
            return;
        }
        if (m_lanSwitchInProgress) {
            return;
        }
        if (m_lanOfferRetryCount >= 6) {
            return;
        }
        if (m_lastLanOfferRequestAtMs == 0 || (nowMs - m_lastLanOfferRequestAtMs) >= 2500) {
            requestLanOfferIfNeeded();
            m_lastLanOfferRequestAtMs = nowMs;
            m_lanOfferRetryCount++;
        }
        if (m_lanOfferRetryCount < 6) {
            m_lanOfferRetryTimer->start(2500);
        }
    });
}

StreamClient::~StreamClient()
{
    if (m_webSocket) {
        m_shouldReconnect = false;
        if (m_reconnectTimer) {
            m_reconnectTimer->stop();
        }
        m_webSocket->close();
    }
}

void StreamClient::connectToServer(const QUrl &url)
{
    if (!m_webSocket) {
        return;
    }

    const auto st = m_webSocket->state();
    if (!url.isEmpty() &&
        !m_lastUrl.isEmpty() &&
        url == m_lastUrl &&
        (st == QAbstractSocket::ConnectedState || st == QAbstractSocket::ConnectingState)) {
        emit logMessage(scTag(this) + QStringLiteral("connect ignored: same url state=%1 url=%2")
                            .arg(static_cast<int>(st))
                            .arg(url.toString()));
        return;
    }

    m_lastUrl = url;
    m_shouldReconnect = true;
    m_reconnectAttempts = 0;
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }
    if (m_lanOfferRetryTimer && m_lanOfferRetryTimer->isActive()) {
        m_lanOfferRetryTimer->stop();
    }
    m_lanOfferRetryCount = 0;
    m_lastLanOfferRequestAtMs = 0;
    stopLanFallbackTimer();
    if (url.port() != AppConfig::lanWsPort()) {
        if (m_lanFirstFrameTimer && m_lanFirstFrameTimer->isActive()) {
            m_lanFirstFrameTimer->stop();
        }
        m_lanAwaitingFirstFrame = false;
    }
    emit logMessage(scTag(this) + QStringLiteral("connect role=%1 route=%2 host=%3 port=%4 channel=%5 switching=%6 cloud_fallback=%7 url=%8")
                        .arg(wsRoleTagFromUrl(url))
                        .arg(wsRouteTagFromUrl(url))
                        .arg(url.host())
                        .arg(wsPortTag(url))
                        .arg(roomIdFromUrl(url))
                        .arg(m_lanSwitchInProgress ? QStringLiteral("true") : QStringLiteral("false"))
                        .arg(m_cloudFallbackUrl.isValid() ? m_cloudFallbackUrl.toString() : QStringLiteral("-"))
                        .arg(url.toString()));
    emit logMessage(QStringLiteral("[StreamClient] connect %1").arg(url.toString()));

    m_pendingOpenUrl = QUrl();
    m_hasPendingOpen = false;
    m_manualSwitchClose = false;

    if (st != QAbstractSocket::UnconnectedState) {
        m_pendingOpenUrl = url;
        m_hasPendingOpen = true;
        m_manualSwitchClose = true;
        emit logMessage(scTag(this) + QStringLiteral("connect queued: closing current state=%1 pending_url=%2")
                            .arg(static_cast<int>(st))
                            .arg(url.toString()));
        m_webSocket->close();
        return;
    }

    m_webSocket->open(url);
}

void StreamClient::disconnectFromServer()
{
    m_shouldReconnect = false;
    m_reconnectAttempts = 0;
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }
    if (m_lanOfferRetryTimer && m_lanOfferRetryTimer->isActive()) {
        m_lanOfferRetryTimer->stop();
    }
    m_lanOfferRetryCount = 0;
    m_lastLanOfferRequestAtMs = 0;
    stopLanFallbackTimer();
    if (m_lanFirstFrameTimer && m_lanFirstFrameTimer->isActive()) {
        m_lanFirstFrameTimer->stop();
    }
    m_lanAwaitingFirstFrame = false;
    m_cloudFallbackUrl = QUrl();
    m_lanSwitchInProgress = false;
    m_pendingOpenUrl = QUrl();
    m_hasPendingOpen = false;
    m_manualSwitchClose = false;
    emit logMessage(scTag(this) + QStringLiteral("disconnect last_url=%1").arg(m_lastUrl.toString()));
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
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - m_lastDecodeFailLogAtMs > 5000) {
            m_lastDecodeFailLogAtMs = nowMs;
            emit logMessage(scTag(this) + QStringLiteral("tx_jpg encode_failed quality=%1 url=%2")
                                .arg(m_jpegQuality)
                                .arg(m_lastUrl.toString()));
        }
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 resendSameFrameIntervalMs = 20000;
    if (!force && !m_lastSentBytes.isEmpty() && bytes == m_lastSentBytes && (nowMs - m_lastSentAtMs) < resendSameFrameIntervalMs) {
        if (nowMs - m_lastTxSkipLogAtMs > 5000) {
            m_lastTxSkipLogAtMs = nowMs;
            emit logMessage(scTag(this) + QStringLiteral("tx_jpg skipped_same bytes=%1 age_ms=%2 url=%3 route=%4")
                                .arg(bytes.size())
                                .arg(nowMs - m_lastSentAtMs)
                                .arg(m_lastUrl.toString())
                                .arg(wsRouteTagFromUrl(m_lastUrl)));
        }
        return;
    }

    const qint64 sent = m_webSocket->sendBinaryMessage(bytes);
    if (sent > 0) {
        m_lastSentBytes = bytes;
        m_lastSentAtMs = nowMs;
        m_txFrames++;
        if (m_lastTxLogAtMs == 0 || (nowMs - m_lastTxLogAtMs) > 2000) {
            m_lastTxLogAtMs = nowMs;
            emit logMessage(scTag(this) + QStringLiteral("tx_jpg ok frames=%1 bytes=%2 quality=%3 route=%4 channel=%5 url=%6")
                                .arg(QString::number(m_txFrames))
                                .arg(QString::number(bytes.size()))
                                .arg(QString::number(m_jpegQuality))
                                .arg(wsRouteTagFromUrl(m_lastUrl))
                                .arg(roomIdFromUrl(m_lastUrl))
                                .arg(m_lastUrl.toString()));
        }
    } else {
        emit logMessage(scTag(this) + QStringLiteral("tx_jpg failed bytes=%1 ws_state=%2 err=%3 url=%4")
                            .arg(bytes.size())
                            .arg(static_cast<int>(m_webSocket ? m_webSocket->state() : QAbstractSocket::UnconnectedState))
                            .arg(m_webSocket ? m_webSocket->errorString() : QStringLiteral("-"))
                            .arg(m_lastUrl.toString()));
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
    m_txFrames = 0;
    m_rxFrames = 0;
    m_lastTxLogAtMs = 0;
    m_lastRxLogAtMs = 0;
    stopLanFallbackTimer();
    if (isSubscribeUrl(m_lastUrl) &&
        m_lastUrl.port() == AppConfig::lanWsPort() &&
        m_lanSwitchInProgress &&
        m_cloudFallbackUrl.isValid() &&
        !m_cloudFallbackUrl.isEmpty()) {
        m_lanAwaitingFirstFrame = true;
        if (m_lanFirstFrameTimer && !m_lanFirstFrameTimer->isActive()) {
            m_lanFirstFrameTimer->start(4500);
        }
    } else {
        if (m_lanFirstFrameTimer && m_lanFirstFrameTimer->isActive()) {
            m_lanFirstFrameTimer->stop();
        }
        m_lanAwaitingFirstFrame = false;
        m_lanSwitchInProgress = false;
        m_cloudFallbackUrl = QUrl();
    }
    emit logMessage(scTag(this) + QStringLiteral("connected role=%1 route=%2 host=%3 port=%4 channel=%5 url=%6")
                        .arg(wsRoleTagFromUrl(m_lastUrl))
                        .arg(wsRouteTagFromUrl(m_lastUrl))
                        .arg(m_lastUrl.host())
                        .arg(wsPortTag(m_lastUrl))
                        .arg(roomIdFromUrl(m_lastUrl))
                        .arg(m_lastUrl.toString()));
    emit logMessage(QStringLiteral("[StreamClient] connected"));
    emit connected();
    if (AppConfig::lanWsEnabled() &&
        isSubscribeUrl(m_lastUrl) &&
        m_lastUrl.port() != AppConfig::lanWsPort() &&
        !m_lanSwitchInProgress) {
        const QString roomId = roomIdFromUrl(m_lastUrl);
        const QStringList bases = AppConfig::lanBaseUrlsForTarget(roomId);
        const QUrl u = pickBestLanSubscribeUrl(bases, roomId, m_lastUrl);
        if (u.isValid() && !u.isEmpty()) {
            m_cloudFallbackUrl = m_lastUrl;
            m_lanSwitchInProgress = true;
            emit logMessage(scTag(this) + QStringLiteral("switch_to_lan_cached room=%1 base_urls=%2 from=%3 to=%4")
                                .arg(roomId)
                                .arg(bases.join(QStringLiteral(",")))
                                .arg(m_cloudFallbackUrl.toString())
                                .arg(u.toString()));
            connectToServer(u);
            startLanFallbackTimer();
            return;
        }
    }
    requestLanOfferIfNeeded();
    if (m_lanOfferRetryTimer &&
        AppConfig::lanWsEnabled() &&
        isSubscribeUrl(m_lastUrl) &&
        m_lastUrl.port() != AppConfig::lanWsPort()) {
        m_lanOfferRetryCount = 0;
        m_lastLanOfferRequestAtMs = QDateTime::currentMSecsSinceEpoch();
        m_lanOfferRetryTimer->start(2500);
    }
}

void StreamClient::onDisconnected()
{
    m_isConnected = false;
    emit logMessage(scTag(this) + QStringLiteral("disconnected role=%1 route=%2 url=%3")
                        .arg(wsRoleTagFromUrl(m_lastUrl))
                        .arg(wsRouteTagFromUrl(m_lastUrl))
                        .arg(m_lastUrl.toString()));
    emit logMessage(QStringLiteral("[StreamClient] disconnected"));
    emit disconnected();

    if (m_manualSwitchClose && m_hasPendingOpen && m_pendingOpenUrl.isValid() && !m_pendingOpenUrl.isEmpty()) {
        const QUrl target = m_pendingOpenUrl;
        m_pendingOpenUrl = QUrl();
        m_hasPendingOpen = false;
        m_manualSwitchClose = false;
        emit logMessage(scTag(this) + QStringLiteral("connect dequeued: opening pending_url=%1").arg(target.toString()));
        if (m_webSocket) {
            m_webSocket->open(target);
        }
        return;
    }

    scheduleReconnect();
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
    } else if (type == QStringLiteral("lan_offer_request")) {
        if (!isPublishUrl(m_lastUrl)) {
            return;
        }
        if (!AppConfig::lanWsEnabled()) {
            return;
        }
        const QString roomId = roomIdFromUrl(m_lastUrl);
        if (roomId.isEmpty()) {
            return;
        }
        const QStringList bases = AppConfig::localLanBaseUrls();
        if (bases.isEmpty()) {
            return;
        }
        emit logMessage(scTag(this) + QStringLiteral("tx lan_offer room=%1 base_urls=%2 url=%3")
                            .arg(roomId)
                            .arg(bases.join(QStringLiteral(",")))
                            .arg(m_lastUrl.toString()));
        QJsonArray arr;
        for (const QString &b : bases) {
            arr.append(b);
        }
        QJsonObject offer;
        offer["type"] = "lan_offer";
        offer["channel_id"] = roomId;
        offer["base_urls"] = arr;
        m_webSocket->sendTextMessage(QJsonDocument(offer).toJson(QJsonDocument::Compact));
    } else if (type == QStringLiteral("lan_offer")) {
        if (!isSubscribeUrl(m_lastUrl)) {
            return;
        }
        handleLanOfferMessage(obj);
    }
}

void StreamClient::onBinaryMessageReceived(const QByteArray &message)
{
    // Handle received binary data (e.g. video frames from other streams)
    if (!m_lastReceivedBytes.isEmpty() && message == m_lastReceivedBytes) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - m_lastRxSkipLogAtMs > 5000) {
            m_lastRxSkipLogAtMs = nowMs;
            emit logMessage(scTag(this) + QStringLiteral("rx_jpg skipped_same bytes=%1 url=%2 route=%3")
                                .arg(message.size())
                                .arg(m_lastUrl.toString())
                                .arg(wsRouteTagFromUrl(m_lastUrl)));
        }
        return;
    }
    QPixmap pixmap;
    if (pixmap.loadFromData(message)) {
        pixmap.setDevicePixelRatio(1.0);
        m_lastReceivedBytes = message;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        m_rxFrames++;
        if (m_lanAwaitingFirstFrame && isSubscribeUrl(m_lastUrl) && m_lastUrl.port() == AppConfig::lanWsPort()) {
            m_lanAwaitingFirstFrame = false;
            if (m_lanFirstFrameTimer && m_lanFirstFrameTimer->isActive()) {
                m_lanFirstFrameTimer->stop();
            }
            m_lanSwitchInProgress = false;
            m_cloudFallbackUrl = QUrl();
            emit logMessage(scTag(this) + QStringLiteral("lan_first_frame_ok url=%1").arg(m_lastUrl.toString()));
        }
        if (m_lastRxLogAtMs == 0 || (nowMs - m_lastRxLogAtMs) > 2000) {
            m_lastRxLogAtMs = nowMs;
            emit logMessage(scTag(this) + QStringLiteral("rx_jpg ok frames=%1 bytes=%2 size=%3x%4 route=%5 channel=%6 url=%7")
                                .arg(QString::number(m_rxFrames))
                                .arg(QString::number(message.size()))
                                .arg(QString::number(pixmap.width()))
                                .arg(QString::number(pixmap.height()))
                                .arg(wsRouteTagFromUrl(m_lastUrl))
                                .arg(roomIdFromUrl(m_lastUrl))
                                .arg(m_lastUrl.toString()));
        }
        emit frameReceived(pixmap);
    } else {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - m_lastDecodeFailLogAtMs > 5000) {
            m_lastDecodeFailLogAtMs = nowMs;
            emit logMessage(scTag(this) + QStringLiteral("rx decode_failed bytes=%1 route=%2 url=%3")
                                .arg(message.size())
                                .arg(wsRouteTagFromUrl(m_lastUrl))
                                .arg(m_lastUrl.toString()));
        }
    }
}

void StreamClient::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    emit logMessage(QStringLiteral("[StreamClient] error %1").arg(m_webSocket->errorString()));
    emit logMessage(scTag(this) + QStringLiteral("error route=%1 ws_state=%2 err=%3 url=%4")
                        .arg(wsRouteTagFromUrl(m_lastUrl))
                        .arg(static_cast<int>(m_webSocket ? m_webSocket->state() : QAbstractSocket::UnconnectedState))
                        .arg(m_webSocket ? m_webSocket->errorString() : QStringLiteral("-"))
                        .arg(m_lastUrl.toString()));
    emit errorOccurred(m_webSocket->errorString());

    if (AppConfig::lanWsEnabled() &&
        isSubscribeUrl(m_lastUrl) &&
        m_lastUrl.port() != AppConfig::lanWsPort() &&
        !m_lanSwitchInProgress) {
        const QString roomId = roomIdFromUrl(m_lastUrl);
        if (!roomId.isEmpty()) {
            const QStringList bases = AppConfig::lanBaseUrlsForTarget(roomId);
            const QUrl u = pickBestLanSubscribeUrl(bases, roomId, m_lastUrl);
            if (u.isValid() && !u.isEmpty()) {
                m_cloudFallbackUrl = m_lastUrl;
                m_lanSwitchInProgress = true;
                emit logMessage(scTag(this) + QStringLiteral("switch_to_lan room=%1 base_urls=%2 from=%3 to=%4")
                                    .arg(roomId)
                                    .arg(bases.join(QStringLiteral(",")))
                                    .arg(m_cloudFallbackUrl.toString())
                                    .arg(u.toString()));
                connectToServer(u);
                startLanFallbackTimer();
                return;
            }
        }
    }

    startLanFallbackTimer();
    scheduleReconnect();
}

void StreamClient::scheduleReconnect()
{
    if (!m_shouldReconnect) {
        return;
    }
    if (!m_reconnectTimer || !m_webSocket) {
        return;
    }
    if (!m_lastUrl.isValid() || m_lastUrl.isEmpty()) {
        return;
    }
    const auto st = m_webSocket->state();
    if (st == QAbstractSocket::ConnectedState || st == QAbstractSocket::ConnectingState) {
        return;
    }
    if (m_reconnectTimer->isActive()) {
        return;
    }

    const int attempt = qMax(0, m_reconnectAttempts);
    const int delayMs = qMin(8000, 600 + attempt * 600);
    m_reconnectAttempts = attempt + 1;
    emit logMessage(QStringLiteral("[StreamClient] reconnect scheduled in %1ms url=%2")
                        .arg(delayMs)
                        .arg(m_lastUrl.toString()));
    m_reconnectTimer->start(delayMs);
}

void StreamClient::attemptReconnect()
{
    if (!m_shouldReconnect || !m_webSocket) {
        return;
    }
    if (!m_lastUrl.isValid() || m_lastUrl.isEmpty()) {
        return;
    }
    const auto st = m_webSocket->state();
    if (st == QAbstractSocket::ConnectedState || st == QAbstractSocket::ConnectingState) {
        return;
    }
    emit logMessage(QStringLiteral("[StreamClient] reconnect %1").arg(m_lastUrl.toString()));
    m_webSocket->open(m_lastUrl);
}

void StreamClient::requestLanOfferIfNeeded()
{
    if (!AppConfig::lanWsEnabled()) {
        return;
    }
    if (!isSubscribeUrl(m_lastUrl)) {
        return;
    }
    if (!m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    if (m_lastUrl.port() == AppConfig::lanWsPort()) {
        return;
    }
    const QString roomId = roomIdFromUrl(m_lastUrl);
    if (roomId.isEmpty()) {
        return;
    }
    emit logMessage(scTag(this) + QStringLiteral("tx lan_offer_request room=%1 url=%2")
                        .arg(roomId)
                        .arg(m_lastUrl.toString()));
    QJsonObject req;
    req["type"] = "lan_offer_request";
    req["channel_id"] = roomId;
    m_webSocket->sendTextMessage(QJsonDocument(req).toJson(QJsonDocument::Compact));
}

void StreamClient::handleLanOfferMessage(const QJsonObject &obj)
{
    if (!AppConfig::lanWsEnabled()) {
        return;
    }
    if (m_lastUrl.port() == AppConfig::lanWsPort()) {
        return;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lanSwitchDisabledUntilMs > 0 && nowMs < m_lanSwitchDisabledUntilMs) {
        emit logMessage(scTag(this) + QStringLiteral("rx lan_offer ignored: lan_switch_cooldown left_ms=%1 url=%2")
                            .arg(QString::number(m_lanSwitchDisabledUntilMs - nowMs))
                            .arg(m_lastUrl.toString()));
        return;
    }

    const QString channelId = obj.value("channel_id").toString(roomIdFromUrl(m_lastUrl));
    const QJsonValue v = obj.value("base_urls");
    if (!v.isArray()) {
        return;
    }
    const QJsonArray arr = v.toArray();
    if (arr.isEmpty()) {
        return;
    }

    QStringList bases;
    bases.reserve(arr.size());
    const QVector<quint32> locals = localIpv4sForLanPick();
    auto isPrivateLanBase = [](const QString &base) -> bool {
        const QUrl u(base);
        if (!u.isValid() || u.isEmpty()) return false;
        if (u.scheme() != QStringLiteral("ws") && u.scheme() != QStringLiteral("wss")) return false;
        if (u.port() != AppConfig::lanWsPort()) return false;
        QHostAddress ha(u.host());
        if (ha.protocol() != QAbstractSocket::IPv4Protocol) return false;
        const quint32 ip = ha.toIPv4Address();
        if (ip == 0) return false;
        if ((ip & 0xFFFF0000u) == 0xA9FE0000u) return false;
        if ((ip & 0xFFFF0000u) == 0xC0A80000u) return true;
        if ((ip & 0xFF000000u) == 0x0A000000u) return true;
        if ((ip & 0xFFF00000u) == 0xAC100000u) return true;
        return false;
    };
    for (const QJsonValue &it : arr) {
        const QString base = it.toString();
        if (base.isEmpty()) {
            continue;
        }
        const QUrl u(base);
        QHostAddress ha(u.host());
        if (ha.protocol() == QAbstractSocket::IPv4Protocol) {
            const quint32 ip = ha.toIPv4Address();
            if (locals.contains(ip)) {
                continue;
            }
        }
        if (isPrivateLanBase(base)) {
            bases.append(base);
        }
    }
    if (!bases.isEmpty()) {
        AppConfig::setLanBaseUrlsForTarget(channelId, bases);
    }

    const QUrl best = pickBestLanSubscribeUrl(bases, channelId, m_lastUrl);
    if (!best.isValid() || best.isEmpty()) {
        return;
    }

    if (best == m_lastUrl) {
        return;
    }

    m_cloudFallbackUrl = m_lastUrl;
    m_lanSwitchInProgress = true;
    emit logMessage(scTag(this) + QStringLiteral("rx lan_offer room=%1 base_urls=%2 chosen=%3 from=%4")
                        .arg(channelId)
                        .arg(bases.join(QStringLiteral(",")))
                        .arg(best.toString())
                        .arg(m_lastUrl.toString()));
    if (m_lanOfferRetryTimer && m_lanOfferRetryTimer->isActive()) {
        m_lanOfferRetryTimer->stop();
    }
    connectToServer(best);
    startLanFallbackTimer();
}

QString StreamClient::roomIdFromUrl(const QUrl &url) const
{
    const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
    if (parts.size() < 2) return QString();
    return parts.value(1);
}

bool StreamClient::isSubscribeUrl(const QUrl &url) const
{
    const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return false;
    return parts.value(0) == QStringLiteral("subscribe");
}

bool StreamClient::isPublishUrl(const QUrl &url) const
{
    const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return false;
    return parts.value(0) == QStringLiteral("publish");
}

void StreamClient::startLanFallbackTimer()
{
    if (!m_lanSwitchInProgress) {
        return;
    }
    if (!m_cloudFallbackUrl.isValid() || m_cloudFallbackUrl.isEmpty()) {
        return;
    }
    if (!m_lanFallbackTimer) {
        return;
    }
    if (m_isConnected) {
        stopLanFallbackTimer();
        return;
    }
    if (!m_lanFallbackTimer->isActive()) {
        m_lanFallbackTimer->start(1800);
    }
}

void StreamClient::stopLanFallbackTimer()
{
    if (m_lanFallbackTimer && m_lanFallbackTimer->isActive()) {
        m_lanFallbackTimer->stop();
    }
}
