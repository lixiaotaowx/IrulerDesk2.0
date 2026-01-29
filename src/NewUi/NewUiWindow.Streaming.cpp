#include "NewUiWindow.h"

#include "../common/AppConfig.h"

#include <QAbstractButton>
#include <QApplication>
#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QLayoutItem>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static void updateKeepAwakeRequested(bool shouldKeepAwake)
{
#ifdef _WIN32
    if (shouldKeepAwake) {
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    } else {
        SetThreadExecutionState(ES_CONTINUOUS);
    }
#else
    Q_UNUSED(shouldKeepAwake);
#endif
}

void NewUiWindow::startSelfPreviewFast()
{
    if (!m_selfPreviewFastTimer) {
        return;
    }
    if (!m_selfPreviewFastTimer->isActive()) {
        m_selfPreviewFastTimer->start();
    }
}

void NewUiWindow::stopSelfPreviewFast()
{
    if (!m_selfPreviewFastTimer) {
        return;
    }
    if (m_selfPreviewFastTimer->isActive()) {
        m_selfPreviewFastTimer->stop();
    }
}

void NewUiWindow::buildLocalPreviewFrameFast(QPixmap &previewPix)
{
    previewPix = QPixmap();

    const auto screens = QGuiApplication::screens();
    QScreen *preferred = nullptr;
    if (m_captureScreenIndex >= 0 && m_captureScreenIndex < screens.size()) {
        preferred = screens[m_captureScreenIndex];
    }
    QScreen *primary = QGuiApplication::primaryScreen();

    QList<QScreen*> candidates;
    if (preferred) {
        candidates.append(preferred);
    }
    if (primary && primary != preferred) {
        candidates.append(primary);
    }
    for (QScreen *s : screens) {
        if (s && s != preferred && s != primary) {
            candidates.append(s);
        }
    }

    QPixmap originalPixmap;
    for (QScreen *s : candidates) {
        // [Fix] QScreen::grabWindow(0) 自动处理该屏幕的几何区域
        // 之前传递 s->geometry().x() 导致在副屏上坐标双重偏移（越界），从而导致黑屏
        // 使用无参数版本以自动匹配该屏幕区域
        originalPixmap = s->grabWindow(0);
        
        if (!originalPixmap.isNull()) {
            // 简单的黑屏检测：如果图片全是黑色，可能是抓取失败（例如受版权保护的内容或系统限制）
            // 只有当抓取的是目标屏幕时才进行此检查，避免误判
            if (s == preferred) {
                QImage img = originalPixmap.toImage();
                if (img.width() > 0 && img.height() > 0) {
                    // 检查中心点像素
                    if (img.pixelColor(img.width()/2, img.height()/2) != Qt::black) {
                        break;
                    }
                    // 进一步检查：如果全黑，可能需要尝试下一个候选（例如主屏）
                    // 但这里我们假设只要 grabWindow 返回了非空图片就是成功的，
                    // 除非用户明确遇到全黑问题。既然用户遇到了，我们信任 grabWindow(0) 的修复。
                    // 坐标修复应该是主要解决方案。
                }
            }
            break;
        }
    }
    if (originalPixmap.isNull()) {
        return;
    }

    QPixmap scaledPix = originalPixmap.scaled(m_imgWidth, m_imgHeight, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = (m_imgWidth - scaledPix.width()) / 2;
    const int y = (m_imgHeight - scaledPix.height()) / 2;

    QPixmap out(m_imgWidth, m_imgHeight);
    out.setDevicePixelRatio(1.0);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(0, 0, m_imgWidth, m_imgHeight, 8, 8);
    p.setClipPath(path);
    p.drawPixmap(x, y, scaledPix);
    p.end();

    previewPix = out;
}

void NewUiWindow::onTimerTimeout()
{
    // [Fix] Remove ApplicationActive check to allow background streaming
    // if (QApplication::applicationState() != Qt::ApplicationActive) {
    //     return;
    // }
    publishLocalScreenFrameTriggered(QStringLiteral("timer"), false, true);
}

void NewUiWindow::publishLocalScreenFrame(bool force)
{
    publishLocalScreenFrameTriggered(QStringLiteral("legacy"), force, true);
}

void NewUiWindow::publishLocalScreenFrameTriggered(const QString &reason, bool forceSend, bool allowCapture)
{
    if (!m_videoLabel) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    // Enforce rate limit for timer-based updates (Default Stream)
    // This ensures that unselected stream is definitely slow (~10s)
    if (reason == QStringLiteral("timer")) {
        if (m_lastPreviewCaptureAtMs > 0 && (nowMs - m_lastPreviewCaptureAtMs) < 9000) {
            return;
        }
    }

    const bool requestLike = reason.contains(QStringLiteral("request"), Qt::CaseInsensitive);
    if (requestLike) {
        const qint64 minResendMs = 900;
        if (m_lastPreviewResendAtMs > 0 && (nowMs - m_lastPreviewResendAtMs) < minResendMs) {
            return;
        }
    }

    const bool shouldCapture = (reason == QStringLiteral("timer"));
    const bool hasCache = (!m_lastPreviewFramePixmap.isNull() && !m_lastPreviewSendPixmap.isNull());
    const bool allowCaptureNow = (allowCapture || (!hasCache && forceSend));
    if ((shouldCapture || !hasCache) && allowCaptureNow) {
        QPixmap previewPix;
        QPixmap sendPix;
        buildLocalScreenFrame(previewPix, sendPix);
        if (!previewPix.isNull() && !sendPix.isNull()) {
            m_lastPreviewFramePixmap = previewPix;
            m_lastPreviewSendPixmap = sendPix;
            m_lastPreviewCaptureAtMs = nowMs;
        }
    }

    if (m_lastPreviewFramePixmap.isNull() || m_lastPreviewSendPixmap.isNull()) {
        return;
    }

    m_videoLabel->setPixmap(m_lastPreviewFramePixmap);

    bool sentAny = false;
    if (m_streamClient && m_streamClient->isConnected()) {
        m_streamClient->sendFrame(m_lastPreviewSendPixmap, forceSend);
        sentAny = true;
    }
    if (m_streamClientLan && m_streamClientLan->isConnected()) {
        m_streamClientLan->sendFrame(m_lastPreviewSendPixmap, forceSend);
        sentAny = true;
    }

    if (sentAny && requestLike) {
        m_lastPreviewResendAtMs = nowMs;
    }

    if (m_lastPreviewLogAtMs == 0 || (nowMs - m_lastPreviewLogAtMs) >= 5000) {
        m_lastPreviewLogAtMs = nowMs;
        qInfo().noquote() << "[PreviewPub]"
                          << " reason=" << reason
                          << " force=" << forceSend
                          << " captured=" << ((shouldCapture || !hasCache) && allowCaptureNow)
                          << " age_ms=" << (m_lastPreviewCaptureAtMs > 0 ? (nowMs - m_lastPreviewCaptureAtMs) : -1)
                          << " sent=" << (sentAny ? "true" : "false");
    }
}

void NewUiWindow::buildLocalScreenFrame(QPixmap &previewPix, QPixmap &sendPix)
{
    previewPix = QPixmap();
    sendPix = QPixmap();

    const auto screens = QGuiApplication::screens();
    QScreen *preferred = nullptr;
    if (m_captureScreenIndex >= 0 && m_captureScreenIndex < screens.size()) {
        preferred = screens[m_captureScreenIndex];
    }
    QScreen *primary = QGuiApplication::primaryScreen();

    QList<QScreen*> candidates;
    if (preferred) {
        candidates.append(preferred);
    }
    if (primary && primary != preferred) {
        candidates.append(primary);
    }
    for (QScreen *s : screens) {
        if (s && s != preferred && s != primary) {
            candidates.append(s);
        }
    }

    QPixmap originalPixmap;
    for (QScreen *s : candidates) {
        // [Fix] QScreen::grabWindow(0) 自动处理该屏幕的几何区域
        // 之前传递 s->geometry().x() 导致在副屏上坐标双重偏移（越界），从而导致黑屏
        // 使用无参数版本以自动匹配该屏幕区域
        originalPixmap = s->grabWindow(0);
        
        if (!originalPixmap.isNull()) {
            // 简单的黑屏检测：如果图片全是黑色，可能是抓取失败（例如受版权保护的内容或系统限制）
            // 只有当抓取的是目标屏幕时才进行此检查，避免误判
            if (s == preferred) {
                QImage img = originalPixmap.toImage();
                if (img.width() > 0 && img.height() > 0) {
                    // 检查中心点像素
                    if (img.pixelColor(img.width()/2, img.height()/2) != Qt::black) {
                        break;
                    }
                    // 进一步检查：如果全黑，可能需要尝试下一个候选（例如主屏）
                    // 但这里我们假设只要 grabWindow 返回了非空图片就是成功的，
                    // 除非用户明确遇到全黑问题。既然用户遇到了，我们信任 grabWindow(0) 的修复。
                    // 坐标修复应该是主要解决方案。
                }
            }
            break;
        }
    }
    if (originalPixmap.isNull()) {
        return;
    }

    // [Privacy Mode] 隐私模式处理
    if (m_isPrivacyMode) {
        // 创建一个全黑的图片，并绘制“隐私中”文字
        QPixmap privacyPix(m_imgWidth, m_imgHeight);
        privacyPix.fill(Qt::black);
        
        QPainter p(&privacyPix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::white);
        QFont font = p.font();
        font.setPixelSize(30);
        font.setBold(true);
        p.setFont(font);
        
        // 绘制文字居中
        p.drawText(privacyPix.rect(), Qt::AlignCenter, QStringLiteral("隐私中"));
        p.end();
        
        // 替换原始图片为隐私图片，用于本地显示和网络发送
        // 注意：这里我们直接替换 previewPix 和 sendPix，
        // 这样本地看到的和发送出去的都是这个带文字的黑屏。
        // 这符合“传输图片时添加一个记号”的变通实现——直接把记号画在图上。
        // 这样接收端无需任何代码修改即可看到效果。
        
        previewPix = privacyPix;
        sendPix = privacyPix;
        return;
    }

    // [Camera Mode] 摄像头模式处理
    if (m_isCameraMode) {
        QImage camImg;
        if (m_videoSink) {
            QVideoFrame frame = m_videoSink->videoFrame();
            if (frame.isValid()) {
                camImg = frame.toImage();
            }
        }
        
        if (!camImg.isNull()) {
            QPixmap srcPix = QPixmap::fromImage(camImg);
            // Process image (Round corners, scale)
            QPixmap pixmap(m_imgWidth, m_imgHeight);
            pixmap.setDevicePixelRatio(1.0);
            pixmap.fill(Qt::transparent);

            QPainter p(&pixmap);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);

            QPixmap scaledPix = srcPix.scaled(m_imgWidth, m_imgHeight, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            int x = (m_imgWidth - scaledPix.width()) / 2;
            int y = (m_imgHeight - scaledPix.height()) / 2;

            QPainterPath path;
            path.addRoundedRect(0, 0, m_imgWidth, m_imgHeight, 8, 8);
            p.setClipPath(path);
            p.drawPixmap(x, y, scaledPix);
            p.end();

            previewPix = pixmap;
            sendPix = pixmap;
            
            // Compression logic for sendPix
            if (sendPix.width() > 200) {
                sendPix = sendPix.scaledToWidth(200, Qt::SmoothTransformation);
            }
            // Keep previewPix high quality
            return;
        }
        
        QPixmap loadingPix(m_imgWidth, m_imgHeight);
        loadingPix.fill(Qt::black);
        QPainter p(&loadingPix);
        p.setPen(Qt::white);
        p.drawText(loadingPix.rect(), Qt::AlignCenter, QStringLiteral("摄像头启动中..."));
        p.end();
        previewPix = loadingPix;
        sendPix = loadingPix;
        return;
    }

    QPixmap srcPix = originalPixmap.scaledToWidth(m_cardBaseWidth, Qt::SmoothTransformation);

    QPixmap pixmap(m_imgWidth, m_imgHeight);
    pixmap.setDevicePixelRatio(1.0);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    QPixmap scaledPix = srcPix.scaled(m_imgWidth, m_imgHeight, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

    int x = (m_imgWidth - scaledPix.width()) / 2;
    int y = (m_imgHeight - scaledPix.height()) / 2;

    QPainterPath path;
    path.addRoundedRect(0, 0, m_imgWidth, m_imgHeight, 8, 8);
    p.setClipPath(path);
    p.drawPixmap(x, y, scaledPix);
    p.end();

    previewPix = pixmap;
    sendPix = pixmap;
    if (sendPix.width() > 200) {
        sendPix = sendPix.scaledToWidth(200, Qt::SmoothTransformation);
    }

    const int previewJpegQuality = 30;
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!sendPix.save(&buffer, "JPG", previewJpegQuality)) {
        return;
    }

    QImage decoded;
    if (!decoded.loadFromData(bytes, "JPG") || decoded.isNull()) {
        return;
    }

    QPixmap decodedPix = QPixmap::fromImage(decoded);
    QPixmap scaledDecoded = decodedPix.scaled(m_imgWidth, m_imgHeight, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap finalPreview(m_imgWidth, m_imgHeight);
    finalPreview.setDevicePixelRatio(1.0);
    finalPreview.fill(Qt::black);
    QPainter pp(&finalPreview);
    pp.setRenderHint(QPainter::Antialiasing);
    pp.setRenderHint(QPainter::SmoothPixmapTransform);
    const int dx = (m_imgWidth - scaledDecoded.width()) / 2;
    const int dy = (m_imgHeight - scaledDecoded.height()) / 2;
    pp.drawPixmap(dx, dy, scaledDecoded);
    pp.end();
    previewPix = finalPreview;
}

QString NewUiWindow::extractUserId(QObject *obj) const
{
    QObject *cur = obj;
    while (cur) {
        const QVariant v = cur->property("userId");
        if (v.isValid()) {
            const QString id = v.toString();
            if (!id.isEmpty()) {
                return id;
            }
        }
        cur = cur->parent();
    }
    return QString();
}

QString NewUiWindow::makeHoverChannelId(const QString &targetUserId) const
{
    if (targetUserId.isEmpty()) {
        return QString();
    }
    // Shared channel for all viewers watching 'targetUserId'
    return QStringLiteral("hfps_%1").arg(targetUserId);
}

void NewUiWindow::scheduleHoverHiFps(const QString &userId, const QPoint &globalPos)
{
    if (!m_hoverCandidateTimer) {
        return;
    }
    if (m_hiFpsActiveUserId == userId) {
        return;
    }
    m_hoverCandidateUserId = userId;
    m_hoverCandidatePos = globalPos;
    m_hoverCandidateTimer->start(1000);
}

void NewUiWindow::cancelHoverHiFps()
{
    if (m_hoverCandidateTimer) {
        m_hoverCandidateTimer->stop();
    }
    m_hoverCandidateUserId.clear();
    stopHiFpsForUser();
}

void NewUiWindow::startHiFpsForUser(const QString &userId)
{
    if (userId.isEmpty() || userId == m_myStreamId) {
        return;
    }
    if (m_hiFpsActiveUserId == userId) {
        if (m_hiFpsSubscriber && !m_hiFpsActiveChannelId.isEmpty()) {
            m_hiFpsLastFrameAtMs = QDateTime::currentMSecsSinceEpoch();
            if (m_hiFpsWatchdogTimer) {
                m_hiFpsWatchdogTimer->start();
            }
            m_hiFpsSubscriber->setProperty("firstFrameReceived", false);
            QJsonObject start;
            start["type"] = "start_streaming";
            m_hiFpsSubscriber->sendTextMessage(QJsonDocument(start).toJson(QJsonDocument::Compact));
            sendHiFpsControl(userId, m_hiFpsActiveChannelId, 10, true);

            QPointer<StreamClient> c = m_hiFpsSubscriber;
            const QString channelId = m_hiFpsActiveChannelId;
            QTimer::singleShot(250, this, [this, c, userId, channelId]() {
                if (!c) return;
                if (m_hiFpsActiveUserId != userId || m_hiFpsActiveChannelId != channelId) return;
                if (c->property("firstFrameReceived").toBool()) return;
                QJsonObject start;
                start["type"] = "start_streaming";
                c->sendTextMessage(QJsonDocument(start).toJson(QJsonDocument::Compact));
                sendHiFpsControl(userId, channelId, 10, true);
            });
            QTimer::singleShot(1200, this, [this, c, userId, channelId]() {
                if (!c) return;
                if (m_hiFpsActiveUserId != userId || m_hiFpsActiveChannelId != channelId) return;
                if (c->property("firstFrameReceived").toBool()) return;
                QJsonObject start;
                start["type"] = "start_streaming";
                c->sendTextMessage(QJsonDocument(start).toJson(QJsonDocument::Compact));
                sendHiFpsControl(userId, channelId, 10, true);
            });
        }
        return;
    }
    stopHiFpsForUser();

    const QString channelId = makeHoverChannelId(userId);
    if (channelId.isEmpty()) {
        return;
    }

    StreamClient *client = new StreamClient(this);
    const QString subscribeUrl = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), channelId);
    m_hiFpsActiveUserId = userId;
    m_hiFpsActiveChannelId = channelId;
    m_hiFpsSubscriber = client;
    m_hiFpsLastFrameAtMs = QDateTime::currentMSecsSinceEpoch();
    if (m_hiFpsWatchdogTimer) {
        m_hiFpsWatchdogTimer->start();
    }

    client->setProperty("firstFrameReceived", false);
    connect(client, &StreamClient::frameReceived, this, [this, userId, client](const QPixmap &frame) {
        client->setProperty("firstFrameReceived", true);

        // [Fix] Receiving/Displaying should be handled by focus
        if (QApplication::applicationState() != Qt::ApplicationActive) {
            return;
        }

        if (m_hiFpsSubscriber == client && m_hiFpsActiveUserId == userId) {
            m_hiFpsLastFrameAtMs = QDateTime::currentMSecsSinceEpoch();
            m_hiFpsLastRecoveryAtMs = 0;
        }
        QLabel *label = m_userLabels.value(userId, nullptr);
        if (!label || frame.isNull()) {
            return;
        }
        QPixmap pixmap(m_imgWidth, m_imgHeight);
        pixmap.setDevicePixelRatio(1.0);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPixmap scaledPix = frame.scaled(m_imgWidth, m_imgHeight, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        int x = (m_imgWidth - scaledPix.width()) / 2;
        int y = (m_imgHeight - scaledPix.height()) / 2;
        QPainterPath path;
        path.addRoundedRect(0, 0, m_imgWidth, m_imgHeight, 8, 8);
        p.setClipPath(path);
        p.drawPixmap(x, y, scaledPix);
        p.end();
        label->setPixmap(pixmap);
        label->update();
    });

    connect(client, &StreamClient::connected, this, [this, userId, channelId, client]() {
        if (m_hiFpsSubscriber != client || m_hiFpsActiveUserId != userId) {
            return;
        }
        QJsonObject start;
        start["type"] = "start_streaming";
        client->sendTextMessage(QJsonDocument(start).toJson(QJsonDocument::Compact));
        sendHiFpsControl(userId, channelId, 10, true);
    });

    connect(client, &StreamClient::connected, this, [this, userId, channelId, client]() {
        if (m_hiFpsSubscriber != client || m_hiFpsActiveUserId != userId || m_hiFpsActiveChannelId != channelId) {
            return;
        }
        QPointer<StreamClient> c = client;
        QTimer::singleShot(250, this, [this, c, userId, channelId]() {
            if (!c) return;
            if (m_hiFpsActiveUserId != userId || m_hiFpsActiveChannelId != channelId) return;
            if (c->property("firstFrameReceived").toBool()) return;
            QJsonObject start;
            start["type"] = "start_streaming";
            c->sendTextMessage(QJsonDocument(start).toJson(QJsonDocument::Compact));
            sendHiFpsControl(userId, channelId, 10, true);
        });
        QTimer::singleShot(1200, this, [this, c, userId, channelId]() {
            if (!c) return;
            if (m_hiFpsActiveUserId != userId || m_hiFpsActiveChannelId != channelId) return;
            if (c->property("firstFrameReceived").toBool()) return;
            QJsonObject start;
            start["type"] = "start_streaming";
            c->sendTextMessage(QJsonDocument(start).toJson(QJsonDocument::Compact));
            sendHiFpsControl(userId, channelId, 10, true);
        });
    });

    sendHiFpsControl(userId, channelId, 10, true);
    client->connectToServer(QUrl(subscribeUrl));
}

void NewUiWindow::stopHiFpsForUser()
{
    if (!m_hiFpsActiveUserId.isEmpty() && !m_hiFpsActiveChannelId.isEmpty()) {
        sendHiFpsControl(m_hiFpsActiveUserId, m_hiFpsActiveChannelId, 10, false);
    }
    m_hiFpsActiveUserId.clear();
    m_hiFpsActiveChannelId.clear();
    m_hiFpsLastFrameAtMs = 0;
    m_hiFpsLastRecoveryAtMs = 0;
    if (m_hiFpsWatchdogTimer) {
        m_hiFpsWatchdogTimer->stop();
    }

    if (m_hiFpsSubscriber) {
        m_hiFpsSubscriber->disconnectFromServer();
        m_hiFpsSubscriber->deleteLater();
        m_hiFpsSubscriber = nullptr;
    }
}

void NewUiWindow::sendHiFpsControl(const QString &targetUserId, const QString &channelId, int fps, bool enabled)
{
    QJsonObject obj;
    obj["type"] = "hover_stream";
    obj["channel_id"] = channelId;
    obj["fps"] = fps;
    obj["enabled"] = enabled;
    obj["target_id"] = targetUserId;
    obj["sender_id"] = m_myStreamId;
    const QString payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    StreamClient *subSock = m_remoteStreams.value(targetUserId, nullptr);
    StreamClient *pubSock = m_streamClient;
    StreamClient *pubSockLan = m_streamClientLan;
    bool sentAny = false;

    if (m_hiFpsSubscriber && m_hiFpsSubscriber->isConnected() && channelId == m_hiFpsActiveChannelId) {
        const qint64 sent = m_hiFpsSubscriber->sendTextMessage(payload);
        qInfo().noquote() << "[HiFps] control sent"
                          << " bytes=" << sent
                          << " via=hfps_subscribe_socket"
                          << " payload=" << payload;
        sentAny = true;
    }

    if (subSock && subSock->isConnected()) {
        const qint64 sent = subSock->sendTextMessage(payload);
        qInfo().noquote() << "[HiFps] control sent"
                          << " bytes=" << sent
                          << " via=subscribe_socket"
                          << " payload=" << payload;
        sentAny = true;
    } else if (subSock) {
        connect(subSock, &StreamClient::connected, this, [this, targetUserId, channelId, fps, enabled]() {
            if (enabled) {
                if (m_hiFpsActiveUserId != targetUserId || m_hiFpsActiveChannelId != channelId) {
                    return;
                }
            }
            sendHiFpsControl(targetUserId, channelId, fps, enabled);
        }, Qt::SingleShotConnection);
    }

    if (pubSock && pubSock->isConnected()) {
        const qint64 sent = pubSock->sendTextMessage(payload);
        qInfo().noquote() << "[HiFps] control sent"
                          << " bytes=" << sent
                          << " via=publish_socket"
                          << " payload=" << payload;
        sentAny = true;
    } else if (pubSock) {
        connect(pubSock, &StreamClient::connected, this, [this, targetUserId, channelId, fps, enabled]() {
            if (enabled) {
                if (m_hiFpsActiveUserId != targetUserId || m_hiFpsActiveChannelId != channelId) {
                    return;
                }
            }
            sendHiFpsControl(targetUserId, channelId, fps, enabled);
        }, Qt::SingleShotConnection);
    }

    if (pubSockLan && pubSockLan->isConnected()) {
        const qint64 sent = pubSockLan->sendTextMessage(payload);
        qInfo().noquote() << "[HiFps] control sent"
                          << " bytes=" << sent
                          << " via=publish_lan_socket"
                          << " payload=" << payload;
        sentAny = true;
    } else if (pubSockLan) {
        connect(pubSockLan, &StreamClient::connected, this, [this, targetUserId, channelId, fps, enabled]() {
            if (enabled) {
                if (m_hiFpsActiveUserId != targetUserId || m_hiFpsActiveChannelId != channelId) {
                    return;
                }
            }
            sendHiFpsControl(targetUserId, channelId, fps, enabled);
        }, Qt::SingleShotConnection);
    }

    if (!sentAny) {
        qInfo().noquote() << "[HiFps] control not sent: no connected socket"
                          << " target_id=" << targetUserId
                          << " channel_id=" << channelId
                          << " enabled=" << enabled
                          << " my_id=" << m_myStreamId;
    }
}

void NewUiWindow::startHiFpsPublishing(const QString &channelId, int fps)
{
    if (channelId.isEmpty()) {
        return;
    }
    qInfo().noquote() << "[HiFpsPub] startHiFpsPublishing channel=" << channelId << " requested_fps=" << fps;
    const QString pubUrl = QString("%1/publish/%2").arg(AppConfig::wsBaseUrl(), channelId);
    StreamClient *pub = m_hiFpsPublishers.value(channelId, nullptr);
    if (!pub) {
        pub = new StreamClient(this);
        pub->setJpegQuality(50);
        m_hiFpsPublishers.insert(channelId, pub);
        connect(pub, &StreamClient::connected, this, [this, channelId]() {
            qInfo().noquote() << "[HiFpsPub] publisher connected channel_id=" << channelId;
        });
        connect(pub, &StreamClient::disconnected, this, [this, channelId]() {
            qInfo().noquote() << "[HiFpsPub] publisher disconnected channel_id=" << channelId;
        });
    }
    if (!pub->isConnected()) {
        pub->connectToServer(QUrl(pubUrl));
    }

    StreamClient *pubLan = nullptr;
    if (AppConfig::lanWsEnabled()) {
        pubLan = m_hiFpsPublishersLan.value(channelId, nullptr);
        if (!pubLan) {
            pubLan = new StreamClient(this);
            pubLan->setJpegQuality(50);
            m_hiFpsPublishersLan.insert(channelId, pubLan);
            connect(pubLan, &StreamClient::connected, this, [this, channelId]() {
                qInfo().noquote() << "[HiFpsPub] lan publisher connected channel_id=" << channelId;
            });
            connect(pubLan, &StreamClient::disconnected, this, [this, channelId]() {
                qInfo().noquote() << "[HiFpsPub] lan publisher disconnected channel_id=" << channelId;
            });
        }
        if (!pubLan->isConnected()) {
            QUrl u(QStringLiteral("ws://127.0.0.1:%1").arg(AppConfig::lanWsPort()));
            u.setPath(QStringLiteral("/publish/%1").arg(channelId));
            pubLan->connectToServer(u);
        }
    }

    const int safeFps = qMax(1, qMin(30, fps));
    QTimer *t = m_hiFpsPublisherTimers.value(channelId, nullptr);
    if (!t) {
        t = new QTimer(this);
        m_hiFpsPublisherTimers.insert(channelId, t);
        connect(t, &QTimer::timeout, this, [this, channelId]() {
            StreamClient *p = m_hiFpsPublishers.value(channelId, nullptr);
            StreamClient *pl = m_hiFpsPublishersLan.value(channelId, nullptr);
            const bool cloudOk = (p && p->isConnected());
            const bool lanOk = (pl && pl->isConnected());
            if (!cloudOk && !lanOk) {
                return;
            }
            QPixmap previewPix;
            QPixmap sendPix;
            buildLocalScreenFrame(previewPix, sendPix);
            if (sendPix.isNull()) {
                return;
            }
            if (sendPix.width() != 300) {
                sendPix = sendPix.scaledToWidth(300, Qt::SmoothTransformation);
            }
            if (cloudOk) {
                p->sendFrame(sendPix, true);
            }
            if (lanOk) {
                pl->sendFrame(sendPix, true);
            }
        });
    }
    t->setInterval(qMax(1, 1000 / safeFps));
    if (!t->isActive()) {
        t->start();
        qInfo().noquote() << "[HiFpsPub] timer started channel_id=" << channelId << " fps=" << safeFps;
    } else {
        qInfo().noquote() << "[HiFpsPub] timer updated channel_id=" << channelId << " fps=" << safeFps;
    }

    if (!m_keepAwakeRequested) {
        m_keepAwakeRequested = true;
        updateKeepAwakeRequested(true);
    }
}

void NewUiWindow::stopHiFpsPublishing(const QString &channelId)
{
    if (channelId.isEmpty()) {
        return;
    }
    if (QTimer *t = m_hiFpsPublisherTimers.take(channelId)) {
        t->stop();
        t->deleteLater();
    }
    if (StreamClient *p = m_hiFpsPublishers.take(channelId)) {
        p->disconnectFromServer();
        p->deleteLater();
    }
    if (StreamClient *p = m_hiFpsPublishersLan.take(channelId)) {
        p->disconnectFromServer();
        p->deleteLater();
    }

    bool anyActive = false;
    const auto timers = m_hiFpsPublisherTimers.values();
    for (QTimer *t : timers) {
        if (t && t->isActive()) {
            anyActive = true;
            break;
        }
    }
    if (!anyActive && m_keepAwakeRequested) {
        m_keepAwakeRequested = false;
        updateKeepAwakeRequested(false);
    }
}

void NewUiWindow::resetSelectionAutoPause(const QString &userId)
{
    if (!m_autoPausedUserId.isEmpty() && m_autoPausedUserId != userId) {
        if (QLabel *ov = m_reselectOverlays.value(m_autoPausedUserId, nullptr)) {
            ov->setVisible(false);
        }
        m_autoPausedUserId.clear();
    }

    m_selectionAutoPauseUserId = userId;
    if (m_selectionAutoPauseTimer) {
        m_selectionAutoPauseTimer->stop();
    }
    if (userId.isEmpty() || userId == m_myStreamId) {
        return;
    }
    if (QApplication::applicationState() != Qt::ApplicationActive) {
        return;
    }
    if (userId == m_autoPausedUserId) {
        return;
    }
    if (m_selectionAutoPauseTimer) {
        m_selectionAutoPauseTimer->start(60 * 1000);
    }
}


void NewUiWindow::pauseSelectedStreamForUser(const QString &userId)
{
    if (userId.isEmpty()) {
        return;
    }

    // Only stop if it's the currently active high-FPS user
    if (userId == m_hiFpsActiveUserId) {
        stopHiFpsForUser();
    }

    m_autoPausedUserId = userId;
    if (QLabel *ov = m_reselectOverlays.value(userId, nullptr)) {
        ov->setVisible(true);
        ov->raise();
    }
}

void NewUiWindow::resumeSelectedStreamForUser(const QString &userId)
{
    if (userId.isEmpty()) {
        return;
    }
    if (m_autoPausedUserId == userId) {
        m_autoPausedUserId.clear();
    }
    if (QLabel *ov = m_reselectOverlays.value(userId, nullptr)) {
        ov->setVisible(false);
    }
}

void NewUiWindow::addUser(const QString &userId, const QString &userName)
{
    addUser(userId, userName, 0);
}

void NewUiWindow::addUser(const QString &userId, const QString &userName, int iconId)
{
    Q_UNUSED(iconId);

    if (userId == m_myStreamId) return;
    if (m_userItems.contains(userId)) return;

    QString appDir = QCoreApplication::applicationDirPath();
    const QString displayName = userName.isEmpty() ? userId : userName;

    QListWidgetItem *item = new QListWidgetItem(m_listWidget);
    item->setData(Qt::UserRole + 1, displayName);
    item->setSizeHint(QSize(m_totalItemWidth, m_totalItemHeight));
    item->setData(Qt::UserRole, userId);

    QWidget *itemWidget = new QWidget();
    itemWidget->setAttribute(Qt::WA_TranslucentBackground);
    QVBoxLayout *itemLayout = new QVBoxLayout(itemWidget);
    itemLayout->setContentsMargins(m_shadowSize, m_shadowSize, m_shadowSize, m_shadowSize);
    itemLayout->setSpacing(0);

    QFrame *card = new QFrame();
    card->setObjectName("CardFrame");
    card->setProperty("userId", userId);
    card->installEventFilter(this);

    card->setStyleSheet(
        "#CardFrame {"
        "   background-color: rgba(32, 32, 36, 175);"
        "   border: 1px solid rgba(255, 255, 255, 22);"
        "   border-radius: 15px;"
        "}"
        "#CardFrame:hover {"
        "   background-color: rgba(40, 40, 45, 190);"
        "}"
    );

    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect();
    shadow->setBlurRadius(18);
    shadow->setColor(QColor(0, 0, 0, 140));
    shadow->setOffset(0, 6);
    card->setGraphicsEffect(shadow);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(m_marginX, m_marginTop, m_marginX, 0);
    cardLayout->setSpacing(0);

    QLabel *imgLabel = new QLabel();
    imgLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    imgLabel->setFixedSize(m_imgWidth, m_imgHeight);
    imgLabel->setAlignment(Qt::AlignCenter);
    imgLabel->setText("Loading...");
    imgLabel->setStyleSheet("color: #888; font-size: 10px;");

    QWidget *imageContainer = new QWidget();
    imageContainer->setProperty("userId", userId);
    imageContainer->setFixedSize(m_imgWidth, m_imgHeight);
    imageContainer->installEventFilter(this);
    imgLabel->setParent(imageContainer);
    imgLabel->move(0, 0);

    QLabel *talkOverlay = new QLabel(imageContainer);
    talkOverlay->setText(QString());
    talkOverlay->setAlignment(Qt::AlignCenter);
    talkOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    talkOverlay->setGeometry(0, 0, m_imgWidth, m_imgHeight);
    talkOverlay->setStyleSheet("color: rgba(255, 255, 255, 235); font-size: 34px; font-weight: bold; background-color: rgba(0, 200, 83, 90); border-radius: 8px;");
    talkOverlay->setVisible(false);
    if (userId != m_myStreamId) {
        m_talkOverlays.insert(userId, talkOverlay);
    }

    QLabel *reselectOverlay = new QLabel(imageContainer);
    reselectOverlay->setText(QStringLiteral("请重新点击观看"));
    reselectOverlay->setAlignment(Qt::AlignCenter);
    reselectOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    reselectOverlay->setGeometry(0, 0, m_imgWidth, m_imgHeight);
    reselectOverlay->setStyleSheet("color: rgba(255, 255, 255, 235); font-size: 22px; font-weight: bold; background-color: rgba(0, 0, 0, 110); border-radius: 8px;");
    reselectOverlay->setVisible(false);
    m_reselectOverlays.insert(userId, reselectOverlay);

    QLabel *avatarLabel = new QLabel(imageContainer);
    avatarLabel->setFixedSize(30, 30);
    avatarLabel->move(6, 6);
    avatarLabel->setAlignment(Qt::AlignCenter);
    avatarLabel->setStyleSheet(
        "QLabel {"
        "   background: transparent;"
        "   border: none;"
        "}"
    );
    QPixmap avatarPix = buildHeadAvatarPixmap(30);
    if (!avatarPix.isNull()) {
        avatarLabel->setPixmap(avatarPix);
    }

    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(0, 0, 0, 5);
    bottomLayout->setSpacing(5);

    QPushButton *tabBtn = nullptr;
    if (userId != m_myStreamId) {
        tabBtn = new QPushButton();
        tabBtn->setFixedSize(14, 14);
        tabBtn->setCursor(Qt::PointingHandCursor);
        tabBtn->setFlat(true);
        tabBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
        tabBtn->setIcon(QIcon(appDir + "/maps/logo/in.png"));
        tabBtn->setIconSize(QSize(14, 14));

        connect(tabBtn, &QPushButton::clicked, this, [this, userId, displayName]() {
            emit startWatchingRequested(userId, displayName);
        });
    }

    QLabel *txtLabel = new QLabel(displayName);
    txtLabel->setObjectName("UserNameLabel");
    txtLabel->setStyleSheet("color: #e0e0e0; font-size: 12px; border: none; background: transparent;");
    txtLabel->setAlignment(Qt::AlignCenter);

    QPushButton *micBtn = nullptr;
    if (userId != m_myStreamId) {
        micBtn = new QPushButton();
        micBtn->setFixedSize(14, 14);
        micBtn->setCursor(Qt::PointingHandCursor);
        micBtn->setProperty("isOn", false);
        micBtn->setProperty("remoteActive", false);
        micBtn->setFlat(true);
        micBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
        micBtn->setIcon(QIcon(appDir + "/maps/logo/get.png"));
        micBtn->setIconSize(QSize(14, 14));

        m_talkButtons.insert(userId, micBtn);
        updateTalkButtonsAvailability();
        connect(micBtn, &QPushButton::clicked, [this, micBtn, appDir, userId]() {
            if (!m_audioCallPeerId.isEmpty() && userId != m_audioCallPeerId) {
                return;
            }
            const bool remoteActive = micBtn->property("remoteActive").toBool();
            bool isOn = micBtn->property("isOn").toBool();
            if (remoteActive) {
                setTalkRemoteActive(userId, false);
                if (userId != m_myStreamId) {
                    setTalkConnected(userId, false);
                    emit talkToggleRequested(userId, false);
                    return;
                }
            }
            if (isOn) return; // Only dial, no hangup
            isOn = true;
            micBtn->setProperty("isOn", isOn);
            if (isOn) {
                const QStringList keys = m_talkButtons.keys();
                for (const QString &otherId : keys) {
                    if (otherId == userId) continue;
                    setTalkConnected(otherId, false);
                    emit talkToggleRequested(otherId, false);
                }
            }
            if (isOn) {
                setTalkPending(userId, true);
            } else {
                setTalkConnected(userId, false);
            }
            emit talkToggleRequested(userId, isOn);
        });
    }

    if (tabBtn) {
        bottomLayout->addWidget(tabBtn);
    } else {
        bottomLayout->addStretch();
    }
    bottomLayout->addWidget(txtLabel);
    if (micBtn) {
        bottomLayout->addWidget(micBtn);
    } else {
        bottomLayout->addStretch();
    }

    cardLayout->addWidget(imageContainer);
    cardLayout->addLayout(bottomLayout);

    itemLayout->addWidget(card);
    m_listWidget->setItemWidget(item, itemWidget);

    m_userItems.insert(userId, item);
    m_userLabels.insert(userId, imgLabel);
    m_userAvatarLabels.insert(userId, avatarLabel);
    updateTalkOverlay(userId);

    StreamClient *client = new StreamClient(this);
    const QString previewChannelId = QStringLiteral("preview_%1").arg(userId);
    QString subscribeUrl = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), previewChannelId);

    client->setProperty("firstFrameReceived", false);
    connect(client, &StreamClient::frameReceived, this, [this, userId, client](const QPixmap &frame) {
        client->setProperty("firstFrameReceived", true);

        // [Fix] Receiving/Displaying should be handled by focus
        if (QApplication::applicationState() != Qt::ApplicationActive) {
            return;
        }

        if (m_userLabels.contains(userId)) {
             QLabel *label = m_userLabels[userId];

             QPixmap pixmap(m_imgWidth, m_imgHeight);
             pixmap.setDevicePixelRatio(1.0);
             pixmap.fill(Qt::transparent);
             QPainter p(&pixmap);
             p.setRenderHint(QPainter::Antialiasing);
             p.setRenderHint(QPainter::SmoothPixmapTransform);

             QPixmap scaledPix = frame.scaled(m_imgWidth, m_imgHeight, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
             int x = (m_imgWidth - scaledPix.width()) / 2;
             int y = (m_imgHeight - scaledPix.height()) / 2;

             QPainterPath path;
             path.addRoundedRect(0, 0, m_imgWidth, m_imgHeight, 8, 8);
             p.setClipPath(path);
             p.drawPixmap(x, y, scaledPix);
             p.end();

             label->setPixmap(pixmap);
             label->update();
        }
    });
    connect(client, &StreamClient::connected, this, [client]() {
        QJsonObject start;
        start["type"] = "start_streaming";
        client->sendTextMessage(QJsonDocument(start).toJson(QJsonDocument::Compact));
    });
    connect(client, &StreamClient::connected, this, [this, client]() {
        QPointer<StreamClient> c = client;
        QTimer::singleShot(250, this, [c]() {
            if (!c || c->property("firstFrameReceived").toBool()) return;
            QJsonObject start;
            start["type"] = "start_streaming";
            c->sendTextMessage(QJsonDocument(start).toJson(QJsonDocument::Compact));
        });
        QTimer::singleShot(1200, this, [c]() {
            if (!c || c->property("firstFrameReceived").toBool()) return;
            QJsonObject start;
            start["type"] = "start_streaming";
            c->sendTextMessage(QJsonDocument(start).toJson(QJsonDocument::Compact));
        });
    });

    client->connectToServer(QUrl(subscribeUrl));
    m_remoteStreams.insert(userId, client);

    ensureAvatarSubscription(userId);
    publishLocalAvatarHint();
}

void NewUiWindow::removeUser(const QString &userId)
{
    if (userId == m_hiFpsActiveUserId || userId == m_hoverCandidateUserId) {
        cancelHoverHiFps();
    }
    if (userId == m_selectionAutoPauseUserId) {
        resetSelectionAutoPause(QString());
    }
    if (userId == m_autoPausedUserId) {
        m_autoPausedUserId.clear();
    }

    m_talkButtons.remove(userId);
    m_talkOverlays.remove(userId);
    m_talkSpinnerAngles.remove(userId);
    m_reselectOverlays.remove(userId);

    if (m_remoteStreams.contains(userId)) {
        StreamClient *client = m_remoteStreams.take(userId);
        client->disconnectFromServer();
        client->deleteLater();
    }

    m_userLabels.remove(userId);
    m_userAvatarLabels.remove(userId);

    if (m_avatarSubscribers.contains(userId)) {
        StreamClient *client = m_avatarSubscribers.take(userId);
        client->disconnectFromServer();
        client->deleteLater();
    }

    if (m_userItems.contains(userId)) {
        QListWidgetItem *item = m_userItems.take(userId);
        int row = m_listWidget->row(item);
        if (row >= 0) {
            delete m_listWidget->takeItem(row);
        }
    }
}

void NewUiWindow::updateResizeGrips()
{
    const int t = 10;
    const QRect r = rect();
    if (!m_resizeGripLeft) return;

    m_resizeGripLeft->setGeometry(0, t, t, r.height() - 2 * t);
    m_resizeGripRight->setGeometry(r.width() - t, t, t, r.height() - 2 * t);
    m_resizeGripTop->setGeometry(t, 0, r.width() - 2 * t, t);
    m_resizeGripBottom->setGeometry(t, r.height() - t, r.width() - 2 * t, t);

    m_resizeGripTopLeft->setGeometry(0, 0, t, t);
    m_resizeGripTopRight->setGeometry(r.width() - t, 0, t, t);
    m_resizeGripBottomLeft->setGeometry(0, r.height() - t, t, t);
    m_resizeGripBottomRight->setGeometry(r.width() - t, r.height() - t, t, t);

    m_resizeGripLeft->raise();
    m_resizeGripRight->raise();
    m_resizeGripTop->raise();
    m_resizeGripBottom->raise();
    m_resizeGripTopLeft->raise();
    m_resizeGripTopRight->raise();
    m_resizeGripBottomLeft->raise();
    m_resizeGripBottomRight->raise();
}

void NewUiWindow::setResizeGripsVisible(bool visible)
{
    const QList<QWidget*> grips = {
        m_resizeGripLeft, m_resizeGripRight, m_resizeGripTop, m_resizeGripBottom,
        m_resizeGripTopLeft, m_resizeGripTopRight, m_resizeGripBottomLeft, m_resizeGripBottomRight
    };
    for (QWidget *g : grips) {
        if (g) g->setVisible(visible);
    }
}

void NewUiWindow::clearUserList()
{
    QList<QString> keys = m_remoteStreams.keys();
    for (const QString &id : keys) {
        removeUser(id);
    }
}

void NewUiWindow::restartUserStreamSubscription(const QString &userId)
{
    if (userId.isEmpty()) {
        return;
    }
    StreamClient *client = m_remoteStreams.value(userId, nullptr);
    if (!client) {
        return;
    }
    QString selectedUserId;
    if (m_listWidget) {
        QListWidgetItem *current = m_listWidget->currentItem();
        if (current) {
            selectedUserId = current->data(Qt::UserRole).toString();
            if (selectedUserId.isEmpty()) {
                if (QWidget *iw = m_listWidget->itemWidget(current)) {
                    if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                        selectedUserId = card->property("userId").toString();
                    } else {
                        selectedUserId = iw->property("userId").toString();
                    }
                }
            }
        }
    }
    const bool shouldForceHiFps = (!selectedUserId.isEmpty() && selectedUserId == userId && userId != m_myStreamId);
    const bool shouldRestoreHiFps = (m_hiFpsActiveUserId == userId && !m_hiFpsActiveChannelId.isEmpty());
    if (client->isConnected()) {
        QJsonObject start;
        start["type"] = "start_streaming";
        client->sendTextMessage(QJsonDocument(start).toJson(QJsonDocument::Compact));
        if (shouldRestoreHiFps) {
            sendHiFpsControl(userId, m_hiFpsActiveChannelId, 10, true);
        }
        if (shouldForceHiFps) {
            startHiFpsForUser(userId);
            resetSelectionAutoPause(userId);
        }
        return;
    }

    const QString previewChannelId = QStringLiteral("preview_%1").arg(userId);
    const QString subscribeUrl = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), previewChannelId);
    client->disconnectFromServer();
    if (shouldRestoreHiFps) {
        connect(client, &StreamClient::connected, this, [this, userId]() {
            if (m_hiFpsActiveUserId != userId || m_hiFpsActiveChannelId.isEmpty()) {
                return;
            }
            sendHiFpsControl(userId, m_hiFpsActiveChannelId, 10, true);
        }, Qt::SingleShotConnection);
    }
    client->connectToServer(QUrl(subscribeUrl));
    if (shouldForceHiFps) {
        startHiFpsForUser(userId);
        resetSelectionAutoPause(userId);
    }
}

void NewUiWindow::onVideoReceivingStopped(const QString &targetId)
{
    if (targetId.isEmpty() || targetId == m_myStreamId) {
        return;
    }
    if (targetId == m_watchingTargetId) {
        setWatchingTarget(QString());
    }
    if (QApplication::applicationState() != Qt::ApplicationActive) {
        return;
    }

    QString selectedUserId;
    if (m_listWidget) {
        QListWidgetItem *current = m_listWidget->currentItem();
        if (current) {
            selectedUserId = current->data(Qt::UserRole).toString();
            if (selectedUserId.isEmpty()) {
                if (QWidget *iw = m_listWidget->itemWidget(current)) {
                    if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                        selectedUserId = card->property("userId").toString();
                    } else {
                        selectedUserId = iw->property("userId").toString();
                    }
                }
            }
        }
    }

    const bool isSelected = (!selectedUserId.isEmpty() && selectedUserId == targetId);
    const bool isActiveHiFps = (m_hiFpsActiveUserId == targetId && !m_hiFpsActiveChannelId.isEmpty());
    if (!isSelected && !isActiveHiFps) {
        return;
    }

    if (targetId == m_autoPausedUserId) {
        resumeSelectedStreamForUser(targetId);
    }
    startHiFpsForUser(targetId);
    resetSelectionAutoPause(targetId);
}

void NewUiWindow::updateUserAvatar(const QString &userId, int iconId)
{
    Q_UNUSED(iconId);

    QLabel *label = m_userAvatarLabels.value(userId, nullptr);
    if (!label) {
        return;
    }

    ensureAvatarCacheDir();
    QPixmap cached(avatarCacheFilePath(userId));
    if (!cached.isNull()) {
        setAvatarLabelPixmap(label, cached);
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (!label->pixmap(Qt::ReturnByValue).isNull()) {
        return;
    }
#endif

    const int s = qMin(label->width(), label->height());
    QPixmap avatarPix = buildHeadAvatarPixmap(s);
    if (!avatarPix.isNull()) {
        label->setPixmap(avatarPix);
    }
}

QString NewUiWindow::getCurrentUserId() const
{
    return m_myStreamId;
}
