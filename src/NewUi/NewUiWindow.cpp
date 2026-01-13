#include "NewUiWindow.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QScrollArea>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QSaveFile>
#include <QStringList>
#include "../common/AppConfig.h"
#include <QDesktopServices>
#include <QPointer>
#include <QCursor>
#include <QFrame>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QGraphicsDropShadowEffect>
#include <QStylePainter>
#include <QStyleOptionButton>
#include <QMenu>
#include <QAction>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QBuffer>
#include <QImage>
#include <QStackedWidget>
#include <QUrl>
#include <QClipboard>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QSignalBlocker>
#include <QDialog>
#include <QLayout>
#include <QSizePolicy>
#include <QtGlobal>
#include <climits>
#include <QAbstractButton>
#include "../video_components/VideoDisplayWidget.h"
#include "../ui/AnnotationToolbar.h"
#include "../ui/SnippetOverlay.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "../ui/BroadcastNoticeDialog.h"

// [Standard Approach] Custom Button for High-Performance Visual Feedback
// Overrides paintEvent to scale icon when pressed, ensuring instant response.
class ResponsiveButton : public QPushButton {
public:
    using QPushButton::QPushButton; // Use base constructors

protected:
    void paintEvent(QPaintEvent *event) override {
        QStylePainter p(this);
        QStyleOptionButton option;
        initStyleOption(&option);

        if (isDown()) {
            // Scale down icon size by 15% when pressed
            QSize originalSize = option.iconSize;
            option.iconSize = originalSize * 0.85; 
        }

        p.drawControl(QStyle::CE_PushButton, option);
    }
};

class StoryboardWebPage : public QWebEnginePage {
public:
    explicit StoryboardWebPage(QObject *parent, QWebEngineView *view)
        : QWebEnginePage(parent), m_view(view)
    {
    }

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override
    {
        Q_UNUSED(type);
        Q_UNUSED(isMainFrame);
        if (url.scheme().compare(QStringLiteral("iruler"), Qt::CaseInsensitive) == 0) {
            return false;
        }
        return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
    }

private:
    QPointer<QWebEngineView> m_view;
};

NewUiWindow::NewUiWindow(QWidget *parent)
    : QWidget(parent)
{
    // --- GLOBAL SIZE CONTROL (ONE VALUE TO RULE THEM ALL) ---
    // [User Setting] 只要修改这个数值，所有尺寸自动计算
    m_cardBaseWidth = 300; // 卡片可见区域的宽度 (Changed to 300 as requested)
    
    // [Advanced Setting] 底部按钮区域的高度
    m_bottomAreaHeight = 45; 
    
    // --- Automatic Calculations (Do not modify) ---
    m_shadowSize = 5; // Shadow margin
    m_aspectRatio = 16.0 / 9.0;
    
    // 1. Visible Card Height = (Width / 1.77) + Bottom Area
    m_cardBaseHeight = (int)(m_cardBaseWidth / m_aspectRatio) + m_bottomAreaHeight;
    
    // 2. Total Item Size (including shadow)
    m_totalItemWidth = m_cardBaseWidth + (2 * m_shadowSize);
    m_totalItemHeight = m_cardBaseHeight + (2 * m_shadowSize);
    
    // 3. Image Dimensions
    // Width = Card Width * 0.94 (3% margin on each side)
    m_imgWidth = (int)(m_cardBaseWidth * 0.94);
    // Height = Width / 1.77
    m_imgHeight = (int)(m_imgWidth / m_aspectRatio);
    
    // 4. Internal Margins (To center the image and create the border)
    m_marginX = (m_cardBaseWidth - m_imgWidth) / 2;
    // Vertical centering in the top area: (TopAreaHeight - ImageHeight) / 2
    m_topAreaHeight = m_cardBaseHeight - m_bottomAreaHeight;
    m_marginTop = (m_topAreaHeight - m_imgHeight) / 2;

    setWindowFlags(Qt::FramelessWindowHint | Qt::Window | Qt::WindowSystemMenuHint | Qt::WindowMinimizeButtonHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    resize(m_totalItemWidth + 20, 800); // Adjust width to fit cards, height arbitrary for now
    
    setupUi();

    // Timer for screenshot
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &NewUiWindow::onTimerTimeout);
    m_timer->start(60 * 1000);
    QTimer::singleShot(0, this, &NewUiWindow::onTimerTimeout);

    m_selfPreviewFastTimer = new QTimer(this);
    m_selfPreviewFastTimer->setInterval(100);
    connect(m_selfPreviewFastTimer, &QTimer::timeout, this, [this]() {
        if (!m_videoLabel) return;
        if (QApplication::applicationState() != Qt::ApplicationActive) return;
        if (!m_listWidget) return;
        QListWidgetItem *current = m_listWidget->currentItem();
        QString userId;
        if (current) {
            userId = current->data(Qt::UserRole).toString();
            if (userId.isEmpty()) {
                if (QWidget *iw = m_listWidget->itemWidget(current)) {
                    if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                        userId = card->property("userId").toString();
                    } else {
                        userId = iw->property("userId").toString();
                    }
                }
            }
        }
        if (userId.isEmpty() || userId != m_myStreamId) {
            stopSelfPreviewFast();
            return;
        }
        QPixmap preview;
        buildLocalPreviewFrameFast(preview);
        if (!preview.isNull()) {
            m_videoLabel->setPixmap(preview);
        }
    });

    m_talkSpinnerTimer = new QTimer(this);
    connect(m_talkSpinnerTimer, &QTimer::timeout, this, &NewUiWindow::onTalkSpinnerTimeout);

    // Setup StreamClient
    m_streamClient = new StreamClient(this);
    connect(m_streamClient, &StreamClient::logMessage, this, &NewUiWindow::onStreamLog);
    connect(m_streamClient, &StreamClient::connected, this, [this]() {
        publishLocalScreenFrameTriggered(QStringLiteral("cloud_connected"), true, true);
    });
    connect(m_streamClient, &StreamClient::startStreamingRequested, this, [this]() {
        publishLocalScreenFrameTriggered(QStringLiteral("cloud_start_request"), true, false);
    });

    if (AppConfig::lanWsEnabled()) {
        m_streamClientLan = new StreamClient(this);
        connect(m_streamClientLan, &StreamClient::logMessage, this, &NewUiWindow::onStreamLog);
        connect(m_streamClientLan, &StreamClient::connected, this, [this]() {
            publishLocalScreenFrameTriggered(QStringLiteral("lan_connected"), true, true);
        });
        connect(m_streamClientLan, &StreamClient::startStreamingRequested, this, [this]() {
            publishLocalScreenFrameTriggered(QStringLiteral("lan_start_request"), true, false);
        });
    }

    auto onHoverStream = [this](const QString &targetId, const QString &channelId, int fps, bool enabled) {
        bool accept = true;
        if (!targetId.isEmpty() && !m_myStreamId.isEmpty() && targetId != m_myStreamId) {
            accept = false;
        }
        if (!accept && !m_myStreamId.isEmpty() && !channelId.isEmpty()) {
            const QString prefix = QStringLiteral("hfps_%1").arg(m_myStreamId);
            if (channelId == prefix || channelId.startsWith(prefix + QStringLiteral("_"))) {
                accept = true;
            }
        }
        if (!accept) {
            qInfo().noquote() << "[HiFpsPub] hover_stream ignored"
                              << " my_id=" << m_myStreamId
                              << " target_id=" << targetId
                              << " channel_id=" << channelId
                              << " fps=" << fps
                              << " enabled=" << enabled;
            return;
        }

        qInfo().noquote() << "[HiFpsPub] hover_stream accepted"
                          << " my_id=" << m_myStreamId
                          << " target_id=" << targetId
                          << " channel_id=" << channelId
                          << " fps=" << fps
                          << " enabled=" << enabled;

        if (enabled) {
            startHiFpsPublishing(channelId, fps);
        } else {
            stopHiFpsPublishing(channelId);
        }
    };
    connect(m_streamClient, &StreamClient::hoverStreamRequested, this, onHoverStream);
    if (m_streamClientLan) {
        connect(m_streamClientLan, &StreamClient::hoverStreamRequested, this, onHoverStream);
    }

    m_avatarPublisher = new StreamClient(this);
    connect(m_avatarPublisher, &StreamClient::connected, this, &NewUiWindow::publishLocalAvatarOnce);
    connect(m_avatarPublisher, &StreamClient::startStreamingRequested, this, &NewUiWindow::publishLocalAvatarOnce);

    if (AppConfig::lanWsEnabled()) {
        m_avatarPublisherLan = new StreamClient(this);
        connect(m_avatarPublisherLan, &StreamClient::connected, this, &NewUiWindow::publishLocalAvatarOnce);
        connect(m_avatarPublisherLan, &StreamClient::startStreamingRequested, this, &NewUiWindow::publishLocalAvatarOnce);
    }

    m_hoverCandidateTimer = new QTimer(this);
    m_hoverCandidateTimer->setSingleShot(true);
    connect(m_hoverCandidateTimer, &QTimer::timeout, this, [this]() {
        if (m_hoverCandidateUserId.isEmpty() || m_hoverCandidateUserId == m_myStreamId) {
            return;
        }
        if (QCursor::pos() != m_hoverCandidatePos) {
            return;
        }
        QWidget *under = QApplication::widgetAt(m_hoverCandidatePos);
        const QString currentId = extractUserId(under);
        if (currentId != m_hoverCandidateUserId) {
            return;
        }
        startHiFpsForUser(m_hoverCandidateUserId);
    });

    m_selectionAutoPauseTimer = new QTimer(this);
    m_selectionAutoPauseTimer->setSingleShot(true);
    connect(m_selectionAutoPauseTimer, &QTimer::timeout, this, [this]() {
        if (m_selectionAutoPauseUserId.isEmpty() || m_selectionAutoPauseUserId == m_myStreamId) {
            return;
        }
        if (QApplication::applicationState() != Qt::ApplicationActive || !m_listWidget) {
            return;
        }
        QListWidgetItem *current = m_listWidget->currentItem();
        if (!current) {
            return;
        }
        QString currentId = current->data(Qt::UserRole).toString();
        if (currentId.isEmpty()) {
            if (QWidget *iw = m_listWidget->itemWidget(current)) {
                if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                    currentId = card->property("userId").toString();
                } else {
                    currentId = iw->property("userId").toString();
                }
            }
        }
        if (currentId != m_selectionAutoPauseUserId) {
            return;
        }
        if (m_autoPausedUserId == currentId) {
            return;
        }
        pauseSelectedStreamForUser(currentId);
    });

    m_avatarPublishTimer = new QTimer(this);
    connect(m_avatarPublishTimer, &QTimer::timeout, this, &NewUiWindow::publishLocalAvatarOnce);
    m_avatarPublishTimer->start(60 * 60 * 1000);
    
    // 2. Connect Login Client (User List & Discovery)
    // DISABLED: Main Window controls the user list now.
    /*
    m_loginClient = new LoginClient(this);
    connect(m_loginClient, &LoginClient::logMessage, this, &NewUiWindow::onStreamLog);
    connect(m_loginClient, &LoginClient::userListUpdated, this, &NewUiWindow::onUserListUpdated);
    connect(m_loginClient, &LoginClient::connected, this, &NewUiWindow::onLoginConnected);
    */

    resize(1160, 800);
    if (QScreen *screen = QGuiApplication::screenAt(QCursor::pos())) {
        const QRect avail = screen->availableGeometry();
        const QSize sz = size();
        const QPoint topLeft(avail.x() + (avail.width() - sz.width()) / 2,
                             avail.y() + (avail.height() - sz.height()) / 2);
        move(topLeft);
    }

    m_resizeGripLeft = new QWidget(this);
    m_resizeGripRight = new QWidget(this);
    m_resizeGripTop = new QWidget(this);
    m_resizeGripBottom = new QWidget(this);
    m_resizeGripTopLeft = new QWidget(this);
    m_resizeGripTopRight = new QWidget(this);
    m_resizeGripBottomLeft = new QWidget(this);
    m_resizeGripBottomRight = new QWidget(this);

    const QList<QWidget*> grips = {
        m_resizeGripLeft, m_resizeGripRight, m_resizeGripTop, m_resizeGripBottom,
        m_resizeGripTopLeft, m_resizeGripTopRight, m_resizeGripBottomLeft, m_resizeGripBottomRight
    };
    for (QWidget *g : grips) {
        g->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        g->setMouseTracking(true);
        g->installEventFilter(this);
        g->raise();
    }
    m_resizeGripLeft->setCursor(Qt::SizeHorCursor);
    m_resizeGripRight->setCursor(Qt::SizeHorCursor);
    m_resizeGripTop->setCursor(Qt::SizeVerCursor);
    m_resizeGripBottom->setCursor(Qt::SizeVerCursor);
    m_resizeGripTopLeft->setCursor(Qt::SizeFDiagCursor);
    m_resizeGripBottomRight->setCursor(Qt::SizeFDiagCursor);
    m_resizeGripTopRight->setCursor(Qt::SizeBDiagCursor);
    m_resizeGripBottomLeft->setCursor(Qt::SizeBDiagCursor);
    updateResizeGrips();
    setResizeGripsVisible(!(windowState() & Qt::WindowMaximized));
}

void NewUiWindow::setMyStreamId(const QString &id, const QString &name)
{
    const QString oldId = m_myStreamId;
    m_myStreamId = id;
    m_myUserName = name;
    updateLocalWatchedOverlay();

    // Update local user label if it exists
    if (m_localNameLabel) {
        QString displayName = m_myUserName.isEmpty() ? m_myStreamId : m_myUserName;
        QString fullText = displayName; // Only display name
        m_localNameLabel->setText(fullText);
    }
    if (m_videoLabel) {
        m_videoLabel->installEventFilter(this);
        if (auto *pw = m_videoLabel->parentWidget()) {
            pw->installEventFilter(this);
        }
    }

    // [Interaction Fix] Update local card userId property for event filter
    if (m_localCard) {
        m_localCard->setProperty("userId", m_myStreamId);
    }

    if (m_localAvatarLabel) {
        if (!oldId.isEmpty()) {
            m_userAvatarLabels.remove(oldId);
        }
        if (!m_myStreamId.isEmpty()) {
            m_userAvatarLabels.insert(m_myStreamId, m_localAvatarLabel);
        }
    }

    if (!oldId.isEmpty() && oldId == m_myStreamId) {
        return;
    }

    // 1. Connect Stream Client (Push)
    const QString previewChannelId = QStringLiteral("preview_%1").arg(m_myStreamId);
    QString serverUrl = QString("%1/publish/%2").arg(AppConfig::wsBaseUrl(), previewChannelId);
    
    if (m_streamClient) {
        m_streamClient->connectToServer(QUrl(serverUrl));
    }

    if (m_streamClientLan) {
        QUrl u(QStringLiteral("ws://127.0.0.1:%1").arg(AppConfig::lanWsPort()));
        u.setPath(QStringLiteral("/publish/%1").arg(previewChannelId));
        m_streamClientLan->connectToServer(u);
    }

    if (!m_myStreamId.isEmpty()) {
        ensureAvatarCacheDir();
        const QString cacheFile = avatarCacheFilePath(m_myStreamId);
        if (!QFileInfo::exists(cacheFile)) {
            QPixmap savePix = buildTestAvatarPixmap(128);
            if (savePix.isNull()) {
                savePix = QPixmap(128, 128);
                savePix.fill(QColor(90, 90, 90));
            }
            QSaveFile f(cacheFile);
            if (f.open(QIODevice::WriteOnly)) {
                savePix.save(&f, "PNG");
                f.commit();
            }
        }

        QPixmap cached(cacheFile);
        if (!cached.isNull()) {
            m_localAvatarPublishPixmap = cached.scaled(128, 128, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        }

        if (m_avatarPublisher) {
            const QString channelId = QString("avatar_%1").arg(m_myStreamId);
            const QString pubUrl = QString("%1/publish/%2").arg(AppConfig::wsBaseUrl(), channelId);
            m_avatarPublisher->connectToServer(QUrl(pubUrl));
        }

        if (m_avatarPublisherLan) {
            const QString channelId = QString("avatar_%1").arg(m_myStreamId);
            QUrl u(QStringLiteral("ws://127.0.0.1:%1").arg(AppConfig::lanWsPort()));
            u.setPath(QStringLiteral("/publish/%1").arg(channelId));
            m_avatarPublisherLan->connectToServer(u);
        }

        ensureAvatarSubscription(m_myStreamId);
        refreshLocalAvatarFromCache();
    }

    if (!m_myStreamId.isEmpty()) {
        m_janusDesiredRoomOwnerId = m_myStreamId;
        ensureJanusAudioLoaded();
        applyJanusAudioState();
    }

    // 2. Connect Login Client
    // DISABLED: Main Window controls login.
    /*
    QString loginUrl = QString("%1/login").arg(AppConfig::wsBaseUrl());
    if (m_loginClient) {
        m_loginClient->disconnectFromServer();
        m_loginClient->connectToServer(QUrl(loginUrl));
    }
    */
}

void NewUiWindow::setCaptureScreenIndex(int index)
{
    m_captureScreenIndex = index;
    if (m_videoLabel) {
        onTimerTimeout();
    }
}

NewUiWindow::~NewUiWindow()
{
    if (m_timer && m_timer->isActive()) {
        m_timer->stop();
    }
    if (m_talkSpinnerTimer && m_talkSpinnerTimer->isActive()) {
        m_talkSpinnerTimer->stop();
    }
    if (m_avatarPublishTimer && m_avatarPublishTimer->isActive()) {
        m_avatarPublishTimer->stop();
    }
    stopHiFpsForUser();
    const QStringList chs = m_hiFpsPublishers.keys();
    for (const QString &ch : chs) {
        stopHiFpsPublishing(ch);
    }

    if (m_streamClient) {
        m_streamClient->disconnectFromServer();
    }
    if (m_avatarPublisher) {
        m_avatarPublisher->disconnectFromServer();
    }
    const QStringList avatarKeys = m_avatarSubscribers.keys();
    for (const QString &k : avatarKeys) {
        if (StreamClient *c = m_avatarSubscribers.value(k, nullptr)) {
            c->disconnectFromServer();
        }
    }
    const QStringList remoteKeys = m_remoteStreams.keys();
    for (const QString &k : remoteKeys) {
        if (StreamClient *c = m_remoteStreams.value(k, nullptr)) {
            c->disconnectFromServer();
        }
    }
}

void NewUiWindow::onLoginConnected()
{
    // Auto-login after connection
    // DISABLED: Main Window handles login
    // m_loginClient->login(m_myStreamId, m_myUserName);
}

QIcon NewUiWindow::buildSpinnerIcon(int size, int angleDeg) const
{
    const int s = qMax(8, size);
    QPixmap pix(s, s);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(QColor(230, 230, 230, 230));
    pen.setWidthF(qMax(1.5, s / 10.0));
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);

    const qreal pad = pen.widthF() + 1.0;
    QRectF r(pad, pad, s - 2 * pad, s - 2 * pad);
    const int start = (90 - angleDeg) * 16;
    const int span = 120 * 16;
    p.drawArc(r, start, span);

    return QIcon(pix);
}

QPixmap NewUiWindow::buildTestAvatarPixmap(int size) const
{
    const int s = qMax(8, size);
    const QString weChatDirPath = QStringLiteral("C:/Users/Administrator/Documents/WeChat Files/All Users");
    QString avatarPath;
    {
        QDir dir(weChatDirPath);
        if (dir.exists()) {
            QStringList filters;
            filters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.webp";
            const QFileInfoList files = dir.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::NoSort);
            QFileInfo best;
            for (const QFileInfo &fi : files) {
                if (!best.exists() || fi.lastModified() > best.lastModified()) {
                    best = fi;
                }
            }
            if (best.exists()) {
                avatarPath = best.absoluteFilePath();
            }
        }
    }

    if (avatarPath.isEmpty()) {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString candidate1 = QDir(appDir).filePath("maps/logo/head.png");
        const QString candidate2 = QDir::current().filePath("src/maps/logo/head.png");
        avatarPath = QFileInfo::exists(candidate1) ? candidate1 : candidate2;
    }

    QPixmap src(avatarPath);
    if (src.isNull()) {
        return QPixmap();
    }
    return makeCircularPixmap(src, s);
}

QPixmap NewUiWindow::buildHeadAvatarPixmap(int size) const
{
    const int s = qMax(8, size);
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString candidate1 = QDir(appDir).filePath("maps/logo/head.png");
    const QString candidate2 = QDir::current().filePath("src/maps/logo/head.png");
    const QString avatarPath = QFileInfo::exists(candidate1) ? candidate1 : candidate2;

    QPixmap src(avatarPath);
    if (src.isNull()) {
        return QPixmap();
    }
    return makeCircularPixmap(src, s);
}

void NewUiWindow::pickAndApplyLocalAvatar()
{
    if (m_myStreamId.isEmpty()) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择头像"),
        QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All Files (*)")
    );
    if (path.isEmpty()) {
        return;
    }

    QPixmap src(path);
    if (src.isNull()) {
        return;
    }

    ensureAvatarCacheDir();
    const QPixmap savePix = makeCircularPixmap(src, 256);
    if (savePix.isNull()) {
        return;
    }

    QSaveFile f(avatarCacheFilePath(m_myStreamId));
    if (f.open(QIODevice::WriteOnly)) {
        savePix.save(&f, "PNG");
        f.commit();
    }

    m_localAvatarPublishPixmap = makeCircularPixmap(savePix, 128);
    refreshLocalAvatarFromCache();
    publishLocalAvatarOnce();
}

QString NewUiWindow::avatarCacheDirPath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return QDir(appDir).filePath("avatars");
}

QString NewUiWindow::avatarCacheFilePath(const QString &userId) const
{
    return QDir(avatarCacheDirPath()).filePath(userId + ".png");
}

void NewUiWindow::ensureAvatarCacheDir()
{
    QDir dir(avatarCacheDirPath());
    if (!dir.exists()) {
        QDir().mkpath(dir.path());
    }
}

QPixmap NewUiWindow::makeCircularPixmap(const QPixmap &src, int size) const
{
    const int s = qMax(8, size);
    if (src.isNull()) {
        return QPixmap();
    }

    QPixmap scaled = src.scaled(s, s, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (scaled.width() == s && scaled.height() == s) {
        return scaled;
    }
    const int x = qMax(0, (scaled.width() - s) / 2);
    const int y = qMax(0, (scaled.height() - s) / 2);
    return scaled.copy(x, y, s, s);
}

void NewUiWindow::setAvatarLabelPixmap(QLabel *label, const QPixmap &src)
{
    if (!label) {
        return;
    }
    const int s = qMin(label->width(), label->height());
    QPixmap out = makeCircularPixmap(src, s);
    if (!out.isNull()) {
        label->setPixmap(out);
    }
}

void NewUiWindow::publishLocalAvatarOnce()
{
    if (!m_avatarPublisher || !m_avatarPublisher->isConnected()) {
    } else if (!m_localAvatarPublishPixmap.isNull()) {
        m_avatarPublisher->sendFrame(m_localAvatarPublishPixmap, true);
    }
    if (m_localAvatarPublishPixmap.isNull()) {
    } else if (m_avatarPublisherLan && m_avatarPublisherLan->isConnected()) {
        m_avatarPublisherLan->sendFrame(m_localAvatarPublishPixmap, true);
    }
}

void NewUiWindow::publishLocalAvatarHint()
{
    if (!m_avatarPublisher || !m_avatarPublisher->isConnected()) {
    } else if (!m_localAvatarPublishPixmap.isNull()) {
        m_avatarPublisher->sendFrame(m_localAvatarPublishPixmap, false);
    }
    if (m_localAvatarPublishPixmap.isNull()) {
    } else if (m_avatarPublisherLan && m_avatarPublisherLan->isConnected()) {
        m_avatarPublisherLan->sendFrame(m_localAvatarPublishPixmap, false);
    }
}

void NewUiWindow::refreshLocalAvatarFromCache()
{
    if (m_myStreamId.isEmpty()) {
        return;
    }

    ensureAvatarCacheDir();
    QPixmap cached(avatarCacheFilePath(m_myStreamId));
    if (!cached.isNull()) {
        setAvatarLabelPixmap(m_localAvatarLabel, cached);
        setAvatarLabelPixmap(m_toolbarAvatarLabel, cached);
        emit avatarPixmapUpdated(m_myStreamId, cached);
    }
}

void NewUiWindow::ensureAvatarSubscription(const QString &userId)
{
    if (userId.isEmpty()) {
        return;
    }
    if (m_avatarSubscribers.contains(userId)) {
        return;
    }

    ensureAvatarCacheDir();
    const QString cachedPath = avatarCacheFilePath(userId);
    QPixmap cached(cachedPath);
    QLabel *label = m_userAvatarLabels.value(userId, nullptr);
    if (!cached.isNull()) {
        setAvatarLabelPixmap(label, cached);
        if (userId == m_myStreamId) {
            setAvatarLabelPixmap(m_localAvatarLabel, cached);
            setAvatarLabelPixmap(m_toolbarAvatarLabel, cached);
        }
        emit avatarPixmapUpdated(userId, cached);
    }

    StreamClient *client = new StreamClient(this);
    m_avatarSubscribers.insert(userId, client);
    connect(client, &StreamClient::frameReceived, this, [this, userId](const QPixmap &frame) {
        if (frame.isNull()) {
            return;
        }

        QLabel *avatarLabel = m_userAvatarLabels.value(userId, nullptr);
        setAvatarLabelPixmap(avatarLabel, frame);
        if (userId == m_myStreamId) {
            setAvatarLabelPixmap(m_localAvatarLabel, frame);
            setAvatarLabelPixmap(m_toolbarAvatarLabel, frame);
        }

        ensureAvatarCacheDir();
        QPixmap savePix = frame.scaled(128, 128, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        QSaveFile f(avatarCacheFilePath(userId));
        if (f.open(QIODevice::WriteOnly)) {
            savePix.save(&f, "PNG");
            f.commit();
        }

        emit avatarPixmapUpdated(userId, frame);
    });

    const QString channelId = QString("avatar_%1").arg(userId);
    const QString subscribeUrl = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), channelId);
    client->connectToServer(QUrl(subscribeUrl));
}

void NewUiWindow::setTalkPending(const QString &userId, bool pending)
{
    QPushButton *btn = m_talkButtons.value(userId, nullptr);
    if (!btn) {
        return;
    }

    btn->setProperty("isPending", pending);

    if (pending) {
        if (!m_talkSpinnerAngles.contains(userId)) {
            m_talkSpinnerAngles.insert(userId, 0);
        }
        if (m_talkSpinnerTimer && !m_talkSpinnerTimer->isActive()) {
            m_talkSpinnerTimer->start(60);
        }
        onTalkSpinnerTimeout();
        updateTalkOverlay(userId);
        return;
    }

    m_talkSpinnerAngles.remove(userId);
    if (m_talkSpinnerAngles.isEmpty() && m_talkSpinnerTimer && m_talkSpinnerTimer->isActive()) {
        m_talkSpinnerTimer->stop();
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const bool isOn = btn->property("isOn").toBool();
    const QString iconName = isOn ? "end.png" : "get.png";
    btn->setIcon(QIcon(appDir + "/maps/logo/" + iconName));
    updateTalkOverlay(userId);
}

void NewUiWindow::setTalkConnected(const QString &userId, bool connected)
{
    QPushButton *btn = m_talkButtons.value(userId, nullptr);
    if (!btn) {
        return;
    }

    btn->setProperty("isOn", connected);
    setTalkPending(userId, false);
    updateTalkOverlay(userId);
    if (connected) {
        showAudioCallUi(userId);
    } else if (m_audioCallPeerId == userId) {
        hideAudioCallUi();
    }
}

void NewUiWindow::setTalkRemoteActive(const QString &userId, bool active)
{
    QPushButton *btn = m_talkButtons.value(userId, nullptr);
    if (!btn) {
        return;
    }

    btn->setProperty("remoteActive", active);
    btn->setProperty("isOn", active);
    setTalkPending(userId, false);
    updateTalkOverlay(userId);
    if (active) {
        showAudioCallUi(userId);
    } else if (m_audioCallPeerId == userId) {
        hideAudioCallUi();
    }
}

void NewUiWindow::updateTalkOverlay(const QString &userId)
{
    QLabel *overlay = m_talkOverlays.value(userId, nullptr);
    if (!overlay) {
        return;
    }
    overlay->setWordWrap(true);
    overlay->setAlignment(Qt::AlignCenter);

    auto applyOverlayTextAutoFit = [](QLabel *lbl, const QString &text) {
        if (!lbl) {
            return;
        }
        lbl->setText(text);
        const int w = qMax(1, lbl->width() - 12);
        const int h = qMax(1, lbl->height() - 12);

        QFont f = lbl->font();
        f.setBold(true);

        const int maxPx = qBound(12, lbl->height() / 4, 22);
        const int minPx = 10;

        bool ok = false;
        for (int px = maxPx; px >= minPx; --px) {
            f.setPixelSize(px);
            QFontMetrics fm(f);
            const QRect br = fm.boundingRect(QRect(0, 0, w, h), Qt::TextWordWrap | Qt::AlignCenter, text);
            if (br.width() <= w && br.height() <= h) {
                lbl->setFont(f);
                ok = true;
                break;
            }
        }
        if (!ok) {
            f.setPixelSize(minPx);
            lbl->setFont(f);
            QFontMetrics fm(f);
            lbl->setWordWrap(false);
            lbl->setText(fm.elidedText(text, Qt::ElideRight, w));
            lbl->setWordWrap(true);
        }
    };

    const bool watching = (!m_watchingTargetId.isEmpty() && userId == m_watchingTargetId);
    const bool beingWatchedBy = isInMyRoomViewerList(userId);
    QString myName = m_myUserName.isEmpty() ? m_myStreamId : m_myUserName;
    if (myName.isEmpty()) {
        myName = QStringLiteral("我");
    }
    if (watching) {
        QString targetName = userId;
        if (QListWidgetItem *it = m_userItems.value(userId, nullptr)) {
            const QString n = it->data(Qt::UserRole + 1).toString();
            if (!n.isEmpty()) {
                targetName = n;
            }
        }
        applyOverlayTextAutoFit(overlay, QStringLiteral("%1在观看%2").arg(myName, targetName));
        overlay->setStyleSheet("color: rgba(255, 255, 255, 235); font-weight: bold; background-color: rgba(0, 200, 83, 90); border-radius: 8px;");
        overlay->setVisible(true);
    } else if (beingWatchedBy) {
        QString viewerName = userId;
        if (QListWidgetItem *it = m_userItems.value(userId, nullptr)) {
            const QString n = it->data(Qt::UserRole + 1).toString();
            if (!n.isEmpty()) {
                viewerName = n;
            }
        }
        applyOverlayTextAutoFit(overlay, QStringLiteral("%1在观看%2").arg(viewerName, myName));
        overlay->setStyleSheet("color: rgba(255, 255, 255, 235); font-weight: bold; background-color: rgba(0, 120, 212, 95); border-radius: 8px;");
        overlay->setVisible(true);
    } else {
        overlay->setVisible(false);
    }

    if (m_listWidget) {
        QListWidgetItem *item = m_userItems.value(userId, nullptr);
        QWidget *w = item ? m_listWidget->itemWidget(item) : nullptr;
        QFrame *card = w ? w->findChild<QFrame*>("CardFrame") : nullptr;
        if (card) {
            const bool selected = item && item->isSelected();
            if (selected) {
                card->setStyleSheet(
                    "#CardFrame {"
                    "   background-color: rgba(255, 102, 0, 40);"
                    "   border: 1px solid #FF6600;"
                    "   border-radius: 15px;"
                    "}"
                );
            } else if (watching) {
                card->setStyleSheet(
                    "#CardFrame {"
                    "   background-color: rgba(0, 200, 83, 55);"
                    "   border: 1px solid #00C853;"
                    "   border-radius: 15px;"
                    "}"
                    "#CardFrame:hover {"
                    "   background-color: rgba(0, 200, 83, 70);"
                    "}"
                );
            } else if (beingWatchedBy) {
                card->setStyleSheet(
                    "#CardFrame {"
                    "   background-color: rgba(0, 120, 212, 45);"
                    "   border: 1px solid #0078D4;"
                    "   border-radius: 15px;"
                    "}"
                    "#CardFrame:hover {"
                    "   background-color: rgba(0, 120, 212, 60);"
                    "}"
                );
            } else {
                card->setStyleSheet(
                    "#CardFrame {"
                    "   background-color: #3b3b3b;"
                    "   border-radius: 15px;"
                    "   border: none;"
                    "}"
                    "#CardFrame:hover {"
                    "   background-color: #444;"
                    "}"
                );
            }
        }
    }
}

void NewUiWindow::setWatchingTarget(const QString &targetId)
{
    if (m_watchingTargetId == targetId) {
        return;
    }
    m_watchingTargetId = targetId;
    const QStringList userIds = m_userItems.keys();
    for (const QString &userId : userIds) {
        updateTalkOverlay(userId);
    }
}

void NewUiWindow::onTalkSpinnerTimeout()
{
    if (m_talkSpinnerAngles.isEmpty()) {
        if (m_talkSpinnerTimer && m_talkSpinnerTimer->isActive()) {
            m_talkSpinnerTimer->stop();
        }
        return;
    }

    const QStringList ids = m_talkSpinnerAngles.keys();
    for (const QString &id : ids) {
        QPushButton *btn = m_talkButtons.value(id, nullptr);
        if (!btn || !btn->property("isPending").toBool()) {
            m_talkSpinnerAngles.remove(id);
            continue;
        }

        int angle = m_talkSpinnerAngles.value(id, 0);
        angle = (angle + 30) % 360;
        m_talkSpinnerAngles[id] = angle;

        const int size = qMin(btn->width(), btn->height());
        btn->setIcon(buildSpinnerIcon(size, angle));
        btn->setIconSize(QSize(size, size));
    }

    if (m_talkSpinnerAngles.isEmpty() && m_talkSpinnerTimer && m_talkSpinnerTimer->isActive()) {
        m_talkSpinnerTimer->stop();
    }
}

void NewUiWindow::onUserListUpdated(const QJsonArray &users)
{
    // DISABLED: Main Window handles user list updates
    // updateListWidget(users);
}

void NewUiWindow::updateListWidget(const QJsonArray &users)
{
    if (!m_listWidget) return;

    // Collect current remote users from the list
    QSet<QString> currentRemoteUsers;
    for (const QJsonValue &val : users) {
        QJsonObject user = val.toObject();
        QString id = user["id"].toString();
        if (id != m_myStreamId) {
            currentRemoteUsers.insert(id);
        }
    }

    // 1. Identify users to REMOVE
    // Iterate over our tracking map
    QList<QString> usersToRemove;
    for (auto it = m_remoteStreams.begin(); it != m_remoteStreams.end(); ++it) {
        if (!currentRemoteUsers.contains(it.key())) {
            usersToRemove.append(it.key());
        }
    }

    for (const QString &id : usersToRemove) {
        // Stop stream
        StreamClient *client = m_remoteStreams.take(id);
        if (client) {
            client->disconnectFromServer();
            client->deleteLater();
        }

        // Remove label reference
        m_userLabels.remove(id);
        m_talkButtons.remove(id);
        m_talkOverlays.remove(id);
        m_talkSpinnerAngles.remove(id);

        // Remove list item
        QListWidgetItem *item = m_userItems.take(id);
        if (item) {
            int row = m_listWidget->row(item);
            if (row >= 0) {
                delete m_listWidget->takeItem(row);
            }
            // item is deleted by takeItem if we manage it correctly, or we delete it manually
            // QListWidget::takeItem returns the item, ownership is transferred to caller.
        }
    }

    // 2. Identify users to ADD or UPDATE
    QString appDir = QCoreApplication::applicationDirPath();

    for (const QJsonValue &val : users) {
        QJsonObject user = val.toObject();
        QString id = user["id"].toString();
        QString name = user["name"].toString();

        if (id == m_myStreamId) continue;

        if (m_remoteStreams.contains(id)) {
            QLabel *imgLabel = m_userLabels.value(id, nullptr);
            if (imgLabel) {
            }
            QListWidgetItem *existingItem = m_userItems.value(id, nullptr);
            if (existingItem && m_listWidget) {
                QWidget *existingWidget = m_listWidget->itemWidget(existingItem);
                if (existingWidget) {
                    QLabel *existingNameLabel = existingWidget->findChild<QLabel*>("UserNameLabel");
                    if (existingNameLabel) {
                        existingNameLabel->setText(name.isEmpty() ? id : name);
                    }
                }
            }
            continue;
        }

        // NEW USER -> Add to List & Subscribe
        QListWidgetItem *item = new QListWidgetItem(m_listWidget);
        item->setSizeHint(QSize(m_totalItemWidth, m_totalItemHeight));

        // Create the Item Widget (Container for the card)
        QWidget *itemWidget = new QWidget();
        itemWidget->setAttribute(Qt::WA_TranslucentBackground);
        QVBoxLayout *itemLayout = new QVBoxLayout(itemWidget);
        // Margins for shadow
        itemLayout->setContentsMargins(m_shadowSize, m_shadowSize, m_shadowSize, m_shadowSize);
        itemLayout->setSpacing(0);

        // The Card Frame (Visible Part)
        QFrame *card = new QFrame();
        card->setObjectName("CardFrame");
        // [Interaction Fix] Install event filter on local card to allow double-click testing
        card->installEventFilter(this);
        card->setProperty("userId", id);
        card->setProperty("userName", name.isEmpty() ? id : name);
        // m_localCard = card; // REMOVED: Incorrectly assigning remote card to local pointer

        card->setStyleSheet(
            "#CardFrame {"
            "   background-color: #3b3b3b;"
            "   border-radius: 12px;"
            "}"
            "#CardFrame:hover {"
            "   background-color: #444;"
            "}"
        );
        
        // Shadow Effect
        QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect();
        shadow->setBlurRadius(10); 
        shadow->setColor(QColor(0, 0, 0, 80));
        shadow->setOffset(0, 2);
        card->setGraphicsEffect(shadow);

        QVBoxLayout *cardLayout = new QVBoxLayout(card);
        // Padding creates the visible border around the image
        // Top: Centering margin, Sides: Centering margin, Bottom: 0 (Controls area handles its own padding)
        cardLayout->setContentsMargins(m_marginX, m_marginTop, m_marginX, 0);
        cardLayout->setSpacing(0); 

        // Image Label
        QLabel *imgLabel = new QLabel();
        imgLabel->setObjectName("StreamImageLabel");
        imgLabel->setProperty("userId", id);
        imgLabel->setFixedSize(m_imgWidth, m_imgHeight); 
        imgLabel->setAlignment(Qt::AlignCenter);
        imgLabel->setText("Loading Stream...");
        imgLabel->setStyleSheet("color: #888; font-size: 10px;");
        imgLabel->installEventFilter(this);

    QLabel *talkOverlay = new QLabel(imgLabel);
        talkOverlay->setText(QString());
        talkOverlay->setAlignment(Qt::AlignCenter);
        talkOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
        talkOverlay->setGeometry(0, 0, m_imgWidth, m_imgHeight);
        talkOverlay->setStyleSheet("color: rgba(255, 255, 255, 235); font-size: 34px; font-weight: bold; background-color: rgba(0, 200, 83, 90); border-radius: 8px;");
        talkOverlay->setVisible(false);
        if (id != m_myStreamId) {
            m_talkOverlays.insert(id, talkOverlay);
        }

        // Bottom Controls Layout
        QHBoxLayout *bottomLayout = new QHBoxLayout();
        // Zero side margins because parent cardLayout already provides MARGIN_X
        // But we might want buttons to extend a bit wider? No, keep alignment.
        // Add a bit of bottom padding
        bottomLayout->setContentsMargins(0, 0, 0, 5); 
        bottomLayout->setSpacing(5);

        // Left Button (tab1.png)
        QPushButton *tabBtn = nullptr;
        if (id != m_myStreamId) {
            tabBtn = new QPushButton();
            tabBtn->setFixedSize(14, 14);
            tabBtn->setCursor(Qt::PointingHandCursor);
            tabBtn->setFlat(true);
            tabBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
            tabBtn->setIcon(QIcon(appDir + "/maps/logo/in.png"));
            tabBtn->setIconSize(QSize(14, 14));

            connect(tabBtn, &QPushButton::clicked, this, [this, id, name]() {
                emit startWatchingRequested(id, name);
            });
        }
        
        // Text Label (Middle)
        QLabel *txtLabel = new QLabel(name.isEmpty() ? id : name);
        txtLabel->setObjectName("UserNameLabel");
        txtLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        txtLabel->setStyleSheet("color: #e0e0e0; font-size: 12px; border: none; background: transparent;");
        txtLabel->setAlignment(Qt::AlignCenter);

        // Mic Toggle
        QPushButton *micBtn = nullptr;
        if (id != m_myStreamId) {
            micBtn = new QPushButton();
            micBtn->setFixedSize(14, 14);
            micBtn->setCursor(Qt::PointingHandCursor);
            micBtn->setProperty("isOn", false);
            micBtn->setProperty("remoteActive", false);
            micBtn->setFlat(true);
            micBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
            micBtn->setIcon(QIcon(appDir + "/maps/logo/end.png"));
            micBtn->setIconSize(QSize(14, 14));

            m_talkButtons.insert(id, micBtn);
            updateTalkButtonsAvailability();
            connect(micBtn, &QPushButton::clicked, [this, micBtn, appDir, id]() {
                if (!m_audioCallPeerId.isEmpty() && id != m_audioCallPeerId) {
                    return;
                }
                const bool remoteActive = micBtn->property("remoteActive").toBool();
                bool isOn = micBtn->property("isOn").toBool();
                if (remoteActive) {
                    setTalkRemoteActive(id, false);
                    if (id != m_myStreamId) {
                        setTalkConnected(id, false);
                        emit talkToggleRequested(id, false);
                        return;
                    }
                }
                isOn = !isOn;
                micBtn->setProperty("isOn", isOn);
                if (isOn) {
                    const QStringList keys = m_talkButtons.keys();
                    for (const QString &otherId : keys) {
                        if (otherId == id) continue;
                        setTalkConnected(otherId, false);
                        emit talkToggleRequested(otherId, false);
                    }
                }
                if (isOn) {
                    setTalkPending(id, true);
                } else {
                    setTalkConnected(id, false);
                }
                emit talkToggleRequested(id, isOn);
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

        cardLayout->addWidget(imgLabel);
        cardLayout->addLayout(bottomLayout);

        itemLayout->addWidget(card);

        m_listWidget->setItemWidget(item, itemWidget);

        // --- Track & Subscribe ---
        m_userItems.insert(id, item);
        m_userLabels.insert(id, imgLabel);

        // Create Client
        StreamClient *client = new StreamClient(this);
        m_remoteStreams.insert(id, client);

        // Connect signals
        connect(client, &StreamClient::frameReceived, this, [this, id](const QPixmap &frame) {
            if (m_userLabels.contains(id)) {
                QLabel *lbl = m_userLabels[id];
                if (lbl) {
                    // Apply rounded corners and scaling exactly like local user
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
                    
                    lbl->setPixmap(pixmap);
                }
            }
        });
        
        // Connect to Subscribe URL
        QString subUrl = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), id);
        client->connectToServer(QUrl(subUrl));
    }
}

void NewUiWindow::onStreamLog(const QString &msg)
{
    qInfo().noquote() << msg;
}

VideoDisplayWidget* NewUiWindow::embeddedVideoWidget() const
{
    return m_embeddedVideoWidget;
}

bool NewUiWindow::isEmbeddedWatching() const
{
    return !m_embeddedTargetId.isEmpty();
}

bool NewUiWindow::isEmbeddedWatchingTarget(const QString &targetId) const
{
    if (targetId.isEmpty()) {
        return false;
    }
    return m_embeddedTargetId == targetId;
}

void NewUiWindow::enterEmbeddedWatchingUi(const QString &targetId, const QString &targetName)
{
    Q_UNUSED(targetName);
    m_embeddedTargetId = targetId;
    if (m_rightContentStack && m_videoContentPage) {
        m_rightContentStack->setCurrentWidget(m_videoContentPage);
    }
    if (m_embeddedVideoWidget) {
        m_embeddedVideoWidget->showSwitchingIndicator(QStringLiteral("切换中..."));
    }
}

void NewUiWindow::startEmbeddedReceiving(const QString &viewerId,
                                        const QString &targetId,
                                        const QString &viewerName,
                                        const QString &serverUrl,
                                        int initialColorId)
{
    m_embeddedTargetId = targetId;
    setWatchingTarget(targetId);
    if (m_rightContentStack && m_videoContentPage) {
        m_rightContentStack->setCurrentWidget(m_videoContentPage);
    }
    if (!m_embeddedVideoWidget) {
        return;
    }
    m_embeddedVideoWidget->setAnnotationColorId(initialColorId);
    m_embeddedVideoWidget->setViewerName(viewerName);
    m_embeddedVideoWidget->setAudioOnlySession(false);
    m_embeddedVideoWidget->setSessionInfo(viewerId, targetId);
    m_embeddedVideoWidget->startReceiving(serverUrl);
    m_embeddedVideoWidget->setSpeakerEnabled(false);
    m_embeddedVideoWidget->setMicSendEnabled(false);
    m_embeddedVideoWidget->setTalkEnabled(false);
}

void NewUiWindow::stopEmbeddedWatching()
{
    const QString targetId = m_embeddedTargetId;
    m_embeddedTargetId.clear();
    setWatchingTarget(QString());
    if (m_embeddedVideoWidget && m_embeddedVideoWidget->isReceiving()) {
        m_embeddedVideoWidget->stopReceiving(false);
    }
    showHomeContent();
    if (!targetId.isEmpty()) {
        emit stopWatchingRequested(targetId);
    }
}

void NewUiWindow::setupUi()
{
    QString appDir = QCoreApplication::applicationDirPath();

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10); // Margins for shadow if needed
    mainLayout->setSpacing(20); // The "hollow" gap

    // --- Left Panel ---
    QWidget *leftPanel = new QWidget(this);
    leftPanel->setObjectName("LeftPanel");
    leftPanel->setFixedWidth(80);
    leftPanel->installEventFilter(this);
    // Use QSS for styling
    leftPanel->setStyleSheet(
        "QWidget#LeftPanel {"
        "   background-color: #2b2b2b;"
        "   border-radius: 20px;"
        "}"
        "QPushButton {"
        "   background-color: #444;"
        "   border: none;"
        "   border-radius: 20px;"
        "   margin: 5px;"
        "}"
        "QPushButton:hover { background-color: #555; }"
        "QPushButton:checked { background-color: #666; }"
    );

    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 20, 0, 20);
    leftLayout->setSpacing(10);
    leftLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    // App Logo
    m_logoLabel = new QLabel();
    m_logoLabel->setFixedSize(40, 40);
    m_logoLabel->setPixmap(QPixmap(appDir + "/maps/logo/iruler.ico").scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_logoLabel->setAlignment(Qt::AlignCenter);
    m_logoLabel->setCursor(Qt::PointingHandCursor);
    m_logoLabel->setToolTip("打开官网：http://www.iruler.cn");
    m_logoLabel->installEventFilter(this);
    leftLayout->addWidget(m_logoLabel);
    
    // Spacing between logo and buttons
    leftLayout->addSpacing(20);

    auto playIconBling = [](QPushButton *btn) {
        const QSize normal = btn->iconSize().isValid() ? btn->iconSize() : QSize(28, 28);
        const QSize down(qRound(normal.width() * 0.7857142857), qRound(normal.height() * 0.7857142857));
        const QSize up(qRound(normal.width() * 1.2142857143), qRound(normal.height() * 1.2142857143));

        QSequentialAnimationGroup *group = new QSequentialAnimationGroup(btn);

        QPropertyAnimation *anim1 = new QPropertyAnimation(btn, "iconSize");
        anim1->setDuration(100);
        anim1->setStartValue(normal);
        anim1->setEndValue(down);
        anim1->setEasingCurve(QEasingCurve::OutQuad);

        QPropertyAnimation *anim2 = new QPropertyAnimation(btn, "iconSize");
        anim2->setDuration(100);
        anim2->setStartValue(down);
        anim2->setEndValue(up);
        anim2->setEasingCurve(QEasingCurve::OutQuad);

        QPropertyAnimation *anim3 = new QPropertyAnimation(btn, "iconSize");
        anim3->setDuration(100);
        anim3->setStartValue(up);
        anim3->setEndValue(normal);
        anim3->setEasingCurve(QEasingCurve::OutElastic);

        group->addAnimation(anim1);
        group->addAnimation(anim2);
        group->addAnimation(anim3);

        connect(group, &QAbstractAnimation::finished, group, &QObject::deleteLater);
        group->start();
    };

    // Add vertical buttons to left panel
    for (int i = 0; i < 4; ++i) {
        QPushButton *btn = new QPushButton();
        btn->setFixedSize(40, 40);
        btn->setCursor(Qt::PointingHandCursor);
        if (i == 0) btn->setToolTip("主页");
        else if (i == 1) btn->setToolTip("故事板");
        else if (i == 2) btn->setToolTip("功能 2");
        else if (i == 3) btn->setToolTip("功能 3");
        btn->installEventFilter(this);
        
        if (i == 0) {
            btn->setObjectName("HomeButton");
            btn->setIcon(QIcon(appDir + "/maps/logo/tab1.png"));
            btn->setIconSize(QSize(28, 28));
            // Transparent background for the first button, no hover background
            btn->setStyleSheet(
                "QPushButton#HomeButton {"
                "   background-color: transparent;"
                "   border: none;"
                "   border-radius: 20px;"
                "}"
                "QPushButton#HomeButton:hover { background-color: transparent; }"
                "QPushButton#HomeButton:pressed { background-color: transparent; }"
            );
            
            // Add click animation (Bling effect: Scale down -> Scale up -> Restore)
            connect(btn, &QPushButton::clicked, [this, btn, playIconBling]() {
                playIconBling(btn);
                showHomeContent();
            });
        }
        else if (i == 1) {
            btn->setObjectName("Function1Button");
            btn->setIcon(QIcon(appDir + "/maps/logo/Storyboard.png"));
            btn->setIconSize(QSize(28, 28));
            btn->setStyleSheet(
                "QPushButton#Function1Button {"
                "   background-color: transparent;"
                "   border: none;"
                "   border-radius: 20px;"
                "}"
                "QPushButton#Function1Button:hover { background-color: transparent; }"
                "QPushButton#Function1Button:pressed { background-color: transparent; }"
            );

            connect(btn, &QPushButton::clicked, [this, btn, playIconBling]() {
                playIconBling(btn);
                showFunction1Browser();
            });
        }
        else if (i == 2) {
            btn->setObjectName("Function2Button");
            btn->setStyleSheet(
                "QPushButton#Function2Button {"
                "   background-color: transparent;"
                "   border: none;"
                "   border-radius: 20px;"
                "}"
                "QPushButton#Function2Button:hover { background-color: transparent; }"
                "QPushButton#Function2Button:pressed { background-color: transparent; }"
            );

            connect(btn, &QPushButton::clicked, [this, btn, playIconBling]() {
                playIconBling(btn);
                if (m_function1WebView) {
                    QString v = AppConfig::readConfigValue(QStringLiteral("function2_url")).trimmed();
                    if (v.isEmpty()) {
                        v = QStringLiteral("about:blank");
                    }
                    m_function1WebView->load(QUrl::fromUserInput(v));
                }
                if (m_rightContentStack && m_function1BrowserPage) {
                    m_rightContentStack->setCurrentWidget(m_function1BrowserPage);
                }
            });
        }
        
        leftLayout->addWidget(btn);
    }
    
    leftLayout->addStretch();
    
    // Bottom setting button
    QPushButton *settingBtn = new QPushButton();
    settingBtn->setObjectName("SettingButton");
    settingBtn->setFixedSize(40, 40);
    settingBtn->setCursor(Qt::PointingHandCursor);
    settingBtn->setToolTip("设置");
    settingBtn->setIcon(QIcon(appDir + "/maps/logo/menu.png"));
    settingBtn->setIconSize(QSize(28, 28));
    settingBtn->setStyleSheet(
        "QPushButton#SettingButton {"
        "   background-color: transparent;"
        "   border: none;"
        "   border-radius: 20px;"
        "}"
        "QPushButton#SettingButton:hover { background-color: transparent; }"
        "QPushButton#SettingButton:pressed { background-color: transparent; }"
    );
    settingBtn->installEventFilter(this);
    connect(settingBtn, &QPushButton::clicked, this, &NewUiWindow::systemSettingsRequested);
    leftLayout->addWidget(settingBtn);

    // --- Right Panel ---
    QWidget *rightPanel = new QWidget(this);
    rightPanel->setObjectName("RightPanel");
    rightPanel->setStyleSheet(
        "QWidget#RightPanel {"
        "   background-color: #2b2b2b;"
        "   border-radius: 20px;"
        "}"
    );

    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(10, 10, 10, 10); // Reduced margins (was 40, 10, 40, 40) to expand content
    rightLayout->setSpacing(10);

    // Title Bar Area
    QWidget *titleBar = new QWidget(rightPanel);
    titleBar->setFixedHeight(50); // Increase height to accommodate larger buttons
    titleBar->setStyleSheet("background-color: transparent;");
    m_titleBar = titleBar;
    titleBar->installEventFilter(this);
    
    QHBoxLayout *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(0, 0, 0, 0);

    QFrame *toolsContainer = new QFrame(titleBar);
    toolsContainer->setObjectName("ToolsContainer");
    toolsContainer->setFixedSize(160, 40);
    toolsContainer->setFrameShape(QFrame::NoFrame);
    toolsContainer->installEventFilter(this);
    toolsContainer->setStyleSheet(
        "#ToolsContainer {"
        "   background-color: #3b3b3b;"
        "   border-radius: 20px;"
        "}"
        "QPushButton {"
        "   background-color: transparent;"
        "   border: none;"
        "   margin: 3px;"
        "}"
        "QPushButton:hover {"
        "   background-color: rgba(255, 255, 255, 30);"
        "   border-radius: 17px;"
        "}"
        "QPushButton:pressed {"
        "   background-color: rgba(255, 255, 255, 40);"
        "}"
    );

    QHBoxLayout *toolsLayout = new QHBoxLayout(toolsContainer);
    toolsLayout->setContentsMargins(10, 0, 10, 0);
    toolsLayout->setSpacing(5);
    toolsLayout->setAlignment(Qt::AlignCenter);

    ResponsiveButton *toolBtn1 = new ResponsiveButton();
    toolBtn1->setFixedSize(40, 40);
    toolBtn1->setIcon(QIcon(appDir + "/maps/logo/d.png"));
    toolBtn1->setIconSize(QSize(24, 24));
    toolBtn1->setCursor(Qt::PointingHandCursor);
    toolBtn1->setToolTip("灵动岛");
    toolBtn1->installEventFilter(this);
    connect(toolBtn1, &QPushButton::clicked, this, &NewUiWindow::toggleStreamingIslandRequested);

    ResponsiveButton *toolBtn2 = new ResponsiveButton();
    toolBtn2->setFixedSize(40, 40);
    toolBtn2->setIcon(QIcon(appDir + "/maps/logo/log.png"));
    toolBtn2->setIconSize(QSize(24, 24));
    toolBtn2->setCursor(Qt::PointingHandCursor);
    toolBtn2->setToolTip("日志");
    toolBtn2->installEventFilter(this);
    connect(toolBtn2, &QPushButton::clicked, this, &NewUiWindow::onBroadcastBtnClicked);

    ResponsiveButton *toolBtn3 = new ResponsiveButton();
    toolBtn3->setFixedSize(40, 40);
    toolBtn3->setIcon(QIcon(appDir + "/maps/logo/clearn.png"));
    toolBtn3->setIconSize(QSize(24, 24));
    toolBtn3->setCursor(Qt::PointingHandCursor);
    toolBtn3->setToolTip("清空标注");
    toolBtn3->installEventFilter(this);
    connect(toolBtn3, &QPushButton::clicked, this, &NewUiWindow::clearMarksRequested);

    toolsLayout->addWidget(toolBtn1);
    toolsLayout->addWidget(toolBtn2);
    toolsLayout->addWidget(toolBtn3);

    titleLayout->addSpacing(8);
    titleLayout->addWidget(toolsContainer);
    titleLayout->addStretch();

    titleLayout->addStretch();

    QPushButton *callRestoreBtn = new QPushButton(titleBar);
    callRestoreBtn->setText(QStringLiteral("通话"));
    callRestoreBtn->setFixedHeight(26);
    callRestoreBtn->setCursor(Qt::PointingHandCursor);
    callRestoreBtn->setVisible(false);
    callRestoreBtn->setStyleSheet(QStringLiteral(
        "QPushButton{"
        " background: rgba(0, 92, 54, 180);"
        " color: rgba(240,240,240,230);"
        " border: 1px solid rgba(0,0,0,60);"
        " border-radius: 10px;"
        " padding: 0 12px;"
        "}"
        "QPushButton:hover{ background: rgba(0, 92, 54, 210); }"
        "QPushButton:pressed{ background: rgba(0, 92, 54, 235); }"));
    connect(callRestoreBtn, &QPushButton::clicked, this, [this]() {
        setAudioCallMiniHidden(false);
        hideAudioCallMiniBar();
        if (m_audioCallDialog && !m_audioCallPeerId.isEmpty()) {
            m_audioCallDialog->show();
            m_audioCallDialog->raise();
        }
    });
    m_audioCallTitleRestoreBtn = callRestoreBtn;
    titleLayout->addWidget(callRestoreBtn, 0, Qt::AlignCenter);
    titleLayout->addStretch();

    QWidget *controlContainer = new QWidget(titleBar);
    // Size adjustment:
    // Buttons: 48x48 (Double size)
    // Container width: 48*5 = 240. Height: 48.
    controlContainer->setFixedSize(144, 48); 
    // Important: Ensure the widget itself doesn't paint a background, only the stylesheet image
    controlContainer->setAttribute(Qt::WA_TranslucentBackground);
    controlContainer->setObjectName("TitleControlContainer");
    controlContainer->installEventFilter(this);
    controlContainer->setStyleSheet(
        "QPushButton {"
        "   background-color: transparent;"
        "   border: none;"
        "}"
        "QPushButton:hover {"
        "   background-color: transparent;"
        "}"
        "QPushButton:pressed {"
        "   background-color: transparent;"
        "}"
    );
    
    // appDir is already defined at top of function

    QHBoxLayout *controlLayout = new QHBoxLayout(controlContainer);
    controlLayout->setContentsMargins(0, 0, 0, 0); // No margins
    controlLayout->setSpacing(0); // No spacing
    controlLayout->setAlignment(Qt::AlignCenter);

    // Minimize Button
    ResponsiveButton *minBtn = new ResponsiveButton();
    minBtn->setFixedSize(48, 48); 
    minBtn->setIcon(QIcon(appDir + "/maps/logo/mini.png"));
    minBtn->setIconSize(QSize(32, 32)); 
    minBtn->setCursor(Qt::PointingHandCursor);
    minBtn->setToolTip("最小化");
    minBtn->installEventFilter(this);
    connect(minBtn, &QPushButton::clicked, this, &QWidget::showMinimized);

    ResponsiveButton *maxBtn = new ResponsiveButton();
    maxBtn->setFixedSize(48, 48);
    maxBtn->setIconSize(QSize(32, 32));
    maxBtn->setCursor(Qt::PointingHandCursor);
    maxBtn->installEventFilter(this);
    connect(maxBtn, &QPushButton::clicked, this, &NewUiWindow::toggleFunction1Maximize);
    m_titleMaximizeBtn = maxBtn;
    updateTitleMaximizeButton();

    // Close Button
    ResponsiveButton *closeBtn = new ResponsiveButton();
    closeBtn->setFixedSize(48, 48); 
    closeBtn->setIcon(QIcon(appDir + "/maps/logo/close.png"));
    closeBtn->setIconSize(QSize(32, 32)); 
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setToolTip("关闭");
    closeBtn->installEventFilter(this);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

    controlLayout->addWidget(minBtn);
    controlLayout->addWidget(maxBtn);
    controlLayout->addWidget(closeBtn);

    titleLayout->addWidget(controlContainer);
    titleLayout->addSpacing(20);

    m_rightContentStack = new QStackedWidget(rightPanel);
    m_rightContentStack->setObjectName("RightContentStack");

    // Content Area (Image Matrix)
    // Wrap QListWidget in a container to handle rounded corners + scrollbar issue
    QFrame *listContainer = new QFrame(rightPanel);
    listContainer->setObjectName("ListContainer");
    listContainer->setStyleSheet(
        "#ListContainer {"
        "   background-color: #404040;" // Lighter than #333333
        "   border-radius: 20px;"
        "}"
    );
    QVBoxLayout *listContainerLayout = new QVBoxLayout(listContainer);
    listContainerLayout->setContentsMargins(15, 15, 5, 15); // Right margin smaller for scrollbar, others for spacing
    
    // Constants moved to member variables initialized in constructor

    m_listWidget = new QListWidget(listContainer);
    m_listWidget->setViewMode(QListWidget::IconMode);
    // Adjust icon size to fit the card widget (roughly card size)
    // Use the global TOTAL size calculated above
    m_listWidget->setIconSize(QSize(m_totalItemWidth, m_totalItemHeight)); 
    m_listWidget->setSpacing(15); // Expanded spacing (was 3)
    m_listWidget->setResizeMode(QListWidget::Adjust);
    // [Scroll Settings] Smooth scrolling settings
    m_listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listWidget->verticalScrollBar()->setSingleStep(10); // Scroll 10 pixels at a time
    // Remove default border and background to blend in
    m_listWidget->setFrameShape(QFrame::NoFrame);
    m_listWidget->viewport()->installEventFilter(this);

    // [Context Menu] Right-click menu with rounded corners
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listWidget, &QListWidget::customContextMenuRequested, [this](const QPoint &pos) {
        QListWidgetItem *item = m_listWidget->itemAt(pos);
        if (!item) return; // Only show menu on items
        const int row = m_listWidget->row(item);
        QString itemUserId = item->data(Qt::UserRole).toString();
        if (itemUserId.isEmpty() && row == 0) {
            itemUserId = m_myStreamId;
        }

        QMenu contextMenu(m_listWidget);
        // Enable transparency for rounded corners
        contextMenu.setAttribute(Qt::WA_TranslucentBackground);
        contextMenu.setWindowFlags(contextMenu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        
        contextMenu.setStyleSheet(
            "QMenu {"
            "    background-color: #2b2b2b;"
            "    border: 1px solid #444;"
            "    border-radius: 12px;"
            "    padding: 6px;"
            "    color: #e0e0e0;"
            "    font-size: 13px;"
            "}"
            "QMenu::item {"
            "    background-color: transparent;"
            "    padding: 8px 24px;"
            "    margin: 2px 4px;"
            "    border-radius: 6px;"
            "}"
            "QMenu::item:selected {"
            "    background-color: #0078d4;" // Windows blue style
            "    color: white;"
            "}"
            "QMenu::separator {"
            "    height: 1px;"
            "    background: #444;"
            "    margin: 4px 10px;"
            "}"
        );

        if (!itemUserId.isEmpty() && itemUserId == m_myStreamId && row == 0) {
            const QStringList viewerIds = getViewerIds();
            QMenu *kickMenu = contextMenu.addMenu(QStringLiteral("踢出观看者"));
            kickMenu->setStyleSheet(contextMenu.styleSheet());
            if (viewerIds.isEmpty()) {
                QAction *none = kickMenu->addAction(QStringLiteral("暂无观看者"));
                none->setEnabled(false);
            } else {
                QAction *kickAll = contextMenu.addAction(QStringLiteral("踢出全部观看者"));
                connect(kickAll, &QAction::triggered, this, [this, viewerIds]() {
                    for (const QString &viewerId : viewerIds) {
                        emit kickViewerRequested(viewerId);
                    }
                });
                contextMenu.addSeparator();
                for (const QString &viewerId : viewerIds) {
                    QString display = viewerId;
                    if (QListWidgetItem *vit = m_viewerItems.value(viewerId, nullptr)) {
                        if (QWidget *vw = m_viewerList ? m_viewerList->itemWidget(vit) : nullptr) {
                            const QList<QLabel*> labels = vw->findChildren<QLabel*>();
                            if (!labels.isEmpty() && labels.first() && !labels.first()->text().isEmpty()) {
                                display = labels.first()->text();
                            }
                        }
                    }
                    QAction *a = kickMenu->addAction(display);
                    connect(a, &QAction::triggered, this, [this, viewerId]() {
                        emit kickViewerRequested(viewerId);
                    });
                }
            }
        } else {
            if (!itemUserId.isEmpty() && isInMyRoomViewerList(itemUserId)) {
                QAction *kickOne = contextMenu.addAction(QStringLiteral("踢出"));
                connect(kickOne, &QAction::triggered, this, [this, itemUserId]() {
                    emit kickViewerRequested(itemUserId);
                });
            } else {
                return;
            }
        }

        contextMenu.exec(m_listWidget->mapToGlobal(pos));
    });
    m_listWidget->setStyleSheet(
        "QListWidget {"
        "   background-color: transparent;"
        "   outline: none;"
        "   border: none;"
        "}"
        "QListWidget::item {"
        "   background-color: transparent;" // Items handle their own background
        "   padding: 0px;" 
        "}"
        "QListWidget::item:selected {"
        "   background-color: transparent;" // Disable default selection rect
        "}"
        "QListWidget::item:hover {"
        "   background-color: transparent;"
        "}"
    );
    
    // Vertical ScrollBar Styling
    m_listWidget->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical {"
        "    border: none;"
        "    background: transparent;" // Transparent track
        "    width: 8px;"
        "    margin: 0px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: #666;"
        "    min-height: 20px;"
        "    border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "    background: #888;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "    border: none;"
        "    background: none;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "    background: none;"
        "}"
    );

    // ScrollBar styling ends here

    /*
    // Add dummy items
    QString imgPath = appDir + "/maps/t.png";
    QPixmap srcPix(imgPath);
    // If loading fails, create a fallback
    if (srcPix.isNull()) {
        srcPix = QPixmap(m_imgWidth, m_imgHeight); // Use calculated size
        srcPix.fill(Qt::darkGray);
    }
    */

    // Connect double click to watch request
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        QString userId = item->data(Qt::UserRole).toString();
        // Ignore if it's local user (empty or self ID) or invalid
        if (!userId.isEmpty() && userId != m_myStreamId) {
             QString name = item->data(Qt::UserRole + 1).toString();
             // Fallback to widget property if data not set
             if (name.isEmpty()) {
                 if (QWidget *iw = m_listWidget->itemWidget(item)) {
                     if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                         name = card->property("userName").toString();
                     }
                 }
             }
             if (name.isEmpty()) {
                 name = userId; // Fallback to ID if name is missing
             }
             emit startWatchingRequested(userId, name);
        }
    });

    m_hiFpsWatchdogTimer = new QTimer(this);
    m_hiFpsWatchdogTimer->setInterval(1500);
    connect(m_hiFpsWatchdogTimer, &QTimer::timeout, this, [this]() {
        if (m_hiFpsActiveUserId.isEmpty() || m_hiFpsActiveChannelId.isEmpty()) {
            return;
        }
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const bool connected = (m_hiFpsSubscriber && m_hiFpsSubscriber->isConnected());
        const bool shouldNudge = (m_hiFpsLastFrameAtMs > 0 && (nowMs - m_hiFpsLastFrameAtMs) >= 2500);
        if (connected && shouldNudge) {
            QJsonObject start;
            start["type"] = "start_streaming";
            m_hiFpsSubscriber->sendTextMessage(QJsonDocument(start).toJson(QJsonDocument::Compact));
            sendHiFpsControl(m_hiFpsActiveUserId, m_hiFpsActiveChannelId, 10, true);
        } else if (!connected && shouldNudge) {
            sendHiFpsControl(m_hiFpsActiveUserId, m_hiFpsActiveChannelId, 10, true);
        }

        if (m_hiFpsLastFrameAtMs > 0 && (nowMs - m_hiFpsLastFrameAtMs) >= 8000) {
            if (m_hiFpsLastRecoveryAtMs == 0 || (nowMs - m_hiFpsLastRecoveryAtMs) >= 8000) {
                m_hiFpsLastRecoveryAtMs = nowMs;
                const QString userId = m_hiFpsActiveUserId;
                stopHiFpsForUser();
                restartUserStreamSubscription(userId);
                startHiFpsForUser(userId);
            }
        }
    });

    connect(m_listWidget, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current, QListWidgetItem *previous) {
        Q_UNUSED(previous);
        QString userId;
        if (current) {
            userId = current->data(Qt::UserRole).toString();
            if (userId.isEmpty()) {
                if (QWidget *iw = m_listWidget->itemWidget(current)) {
                    if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                        userId = card->property("userId").toString();
                    } else {
                        userId = iw->property("userId").toString();
                    }
                }
            }
        }
        if (userId.isEmpty()) {
            stopSelfPreviewFast();
            cancelHoverHiFps();
            resetSelectionAutoPause(QString());
            return;
        }
        if (userId == m_myStreamId) {
            cancelHoverHiFps();
            resetSelectionAutoPause(QString());
            startSelfPreviewFast();
            return;
        }
        stopSelfPreviewFast();
        if (QApplication::applicationState() != Qt::ApplicationActive) {
            cancelHoverHiFps();
            resetSelectionAutoPause(QString());
            return;
        }
        if (userId == m_autoPausedUserId) {
            resumeSelectedStreamForUser(userId);
        }
        startHiFpsForUser(userId);
        resetSelectionAutoPause(userId);
    });

    connect(m_listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        QString userId = item->data(Qt::UserRole).toString();
        if (userId.isEmpty()) {
            if (QWidget *iw = m_listWidget->itemWidget(item)) {
                if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                    userId = card->property("userId").toString();
                } else {
                    userId = iw->property("userId").toString();
                }
            }
        }
        if (userId.isEmpty() || userId == m_myStreamId) {
            return;
        }
        if (QApplication::applicationState() != Qt::ApplicationActive) {
            return;
        }
        if (userId == m_autoPausedUserId) {
            resumeSelectedStreamForUser(userId);
        }
        startHiFpsForUser(userId);
        resetSelectionAutoPause(userId);
    });

    connect(m_listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!m_farRightPanel || !m_farRightPanel->isVisible() || !item) {
            return;
        }

        QString userId = item->data(Qt::UserRole).toString();
        if (userId.isEmpty()) {
            if (QWidget *iw = m_listWidget->itemWidget(item)) {
                if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                    userId = card->property("userId").toString();
                } else {
                    userId = iw->property("userId").toString();
                }
            }
        }

        if (userId.isEmpty() || userId == m_myStreamId) {
            return;
        }
        m_farRightPanel->setVisible(false);
    });

    // Create Local User Item (Index 0)
    {
        QListWidgetItem *item = new QListWidgetItem(m_listWidget);
        // Size hint must cover the widget size + shadow margins
        
        // Use the global TOTAL size
        item->setSizeHint(QSize(m_totalItemWidth, m_totalItemHeight)); 
        
        // Create the Item Widget (Container for the card)
        QWidget *itemWidget = new QWidget();
        itemWidget->setAttribute(Qt::WA_TranslucentBackground);
        QVBoxLayout *itemLayout = new QVBoxLayout(itemWidget);
        // Margins for shadow
        itemLayout->setContentsMargins(m_shadowSize, m_shadowSize, m_shadowSize, m_shadowSize);
        itemLayout->setSpacing(0);

        // The Card Frame (Visible Part)
        QFrame *card = new QFrame();
        card->setObjectName("CardFrame");
        
        // [Interaction Fix] Store local card pointer and install event filter
        m_localCard = card;
        card->installEventFilter(this);
        card->setProperty("userId", m_myStreamId); // Might be empty initially, updated in setMyStreamId

        card->setStyleSheet(
            "#CardFrame {"
            "   background-color: #3b3b3b;"
            "   border-radius: 12px;"
            "}"
            "#CardFrame:hover {"
            "   background-color: #444;"
            "}"
        );
        
        // Shadow Effect
        QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect();
        shadow->setBlurRadius(10); 
        shadow->setColor(QColor(0, 0, 0, 80));
        shadow->setOffset(0, 2);
        card->setGraphicsEffect(shadow);

        QVBoxLayout *cardLayout = new QVBoxLayout(card);
        // Padding creates the visible border around the image
        // Top: Centering margin, Sides: Centering margin, Bottom: 0 (Controls area handles its own padding)
        cardLayout->setContentsMargins(m_marginX, m_marginTop, m_marginX, 0);
        cardLayout->setSpacing(0); 

        // Image Label
        QLabel *imgLabel = new QLabel();
        // Width matches the calculated image width
        imgLabel->setFixedSize(m_imgWidth, m_imgHeight); 
        imgLabel->setAlignment(Qt::AlignCenter);

        // Capture for video updates
        m_videoLabel = imgLabel;
        
        // Initial placeholder capture
        QScreen *screen = QGuiApplication::primaryScreen();
        QPixmap srcPix;
        if (screen) {
             QPixmap original = screen->grabWindow(0,
                 screen->geometry().x(), screen->geometry().y(),
                 screen->size().width(), screen->size().height());
             original.setDevicePixelRatio(1.0);
             srcPix = original.scaledToWidth(m_cardBaseWidth, Qt::SmoothTransformation);
        } else {
             srcPix = QPixmap(m_cardBaseWidth, (int)(m_cardBaseWidth/1.77));
             srcPix.fill(Qt::black);
        }

        // Process Image (Rounded Corners)
        QPixmap pixmap(m_imgWidth, m_imgHeight); 
        pixmap.setDevicePixelRatio(1.0);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        
        QPixmap scaledPix = srcPix.scaled(m_imgWidth, m_imgHeight, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        // Center crop
        int x = (m_imgWidth - scaledPix.width()) / 2;
        int y = (m_imgHeight - scaledPix.height()) / 2;
        
        QPainterPath path;
        // All corners rounded to match the inner look
        path.addRoundedRect(0, 0, m_imgWidth, m_imgHeight, 8, 8);
        p.setClipPath(path);
        
        p.drawPixmap(x, y, scaledPix);
        p.end();
        imgLabel->setPixmap(pixmap);

        QWidget *imageContainer = new QWidget();
        imageContainer->setFixedSize(m_imgWidth, m_imgHeight);
        imgLabel->setParent(imageContainer);
        imgLabel->move(0, 0);

        QLabel *avatarLabel = new QLabel(imageContainer);
        avatarLabel->setFixedSize(30, 30);
        avatarLabel->move(6, 6);
        avatarLabel->setAlignment(Qt::AlignCenter);
        avatarLabel->setCursor(Qt::PointingHandCursor);
        avatarLabel->setToolTip(QStringLiteral("更换头像"));
        avatarLabel->installEventFilter(this);
        avatarLabel->setStyleSheet(
            "QLabel {"
            "   background: transparent;"
            "   border: none;"
            "}"
        );
        QPixmap avatarPix = buildTestAvatarPixmap(30);
        if (!avatarPix.isNull()) {
            avatarLabel->setPixmap(avatarPix);
        }
        m_localAvatarLabel = avatarLabel;
        if (!m_myStreamId.isEmpty()) {
            m_userAvatarLabels.insert(m_myStreamId, m_localAvatarLabel);
        }

    QLabel *watchedOverlay = new QLabel(imageContainer);
        watchedOverlay->setText(QString());
        watchedOverlay->setAlignment(Qt::AlignCenter);
        watchedOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
        watchedOverlay->setGeometry(0, 0, m_imgWidth, m_imgHeight);
        watchedOverlay->setStyleSheet("color: rgba(255, 255, 255, 235); font-size: 34px; font-weight: bold; background-color: rgba(0, 120, 212, 95); border-radius: 8px;");
        watchedOverlay->setVisible(false);
        m_localWatchedOverlay = watchedOverlay;

        // Bottom Controls Layout
        QHBoxLayout *bottomLayout = new QHBoxLayout();
        // Zero side margins because parent cardLayout already provides MARGIN_X
        // But we might want buttons to extend a bit wider? No, keep alignment.
        // Add a bit of bottom padding
        bottomLayout->setContentsMargins(0, 0, 0, 5); 
        bottomLayout->setSpacing(5);

        // Text Label (Middle)
    QString displayName = m_myUserName.isEmpty() ? m_myStreamId : m_myUserName;
    // Format: Name only (ID removed as requested)
    QString fullText = displayName;
    QLabel *txtLabel = new QLabel(fullText);
    txtLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_localNameLabel = txtLabel; // Store pointer for updates
    txtLabel->setStyleSheet("color: #e0e0e0; font-size: 12px; border: none; background: transparent;");
    txtLabel->setAlignment(Qt::AlignCenter);
        bottomLayout->addStretch();
        bottomLayout->addWidget(txtLabel);
        bottomLayout->addStretch();

        cardLayout->addWidget(imageContainer);
        cardLayout->addLayout(bottomLayout);

        itemLayout->addWidget(card);
        
        m_listWidget->setItemWidget(item, itemWidget);
    }

    listContainerLayout->addWidget(m_listWidget);
    m_homeContentPage = listContainer;
    m_rightContentStack->addWidget(m_homeContentPage);

    QFrame *browserContainer = new QFrame(rightPanel);
    browserContainer->setObjectName("Function1BrowserContainer");
    browserContainer->setStyleSheet(
        "#Function1BrowserContainer {"
        "   background-color: #404040;"
        "   border-radius: 20px;"
        "}"
    );
    QVBoxLayout *browserLayout = new QVBoxLayout(browserContainer);
    browserLayout->setContentsMargins(0, 0, 0, 0);
    browserLayout->setSpacing(0);

    m_function1WebView = new QWebEngineView(browserContainer);
    auto *storyboardPage = new StoryboardWebPage(this, m_function1WebView);
    m_function1WebView->setPage(storyboardPage);

    ensureJanusAudioLoaded();
    browserLayout->addWidget(m_function1WebView);

    m_function1BrowserPage = browserContainer;
    m_rightContentStack->addWidget(m_function1BrowserPage);

    QFrame *videoContainer = new QFrame(rightPanel);
    videoContainer->setObjectName("VideoContainer");
    videoContainer->setStyleSheet(
        "#VideoContainer {"
        "   background-color: #404040;"
        "   border-radius: 20px;"
        "}"
    );
    QVBoxLayout *videoLayout = new QVBoxLayout(videoContainer);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->setSpacing(0);

    QWidget *videoTopBar = new QWidget(videoContainer);
    videoTopBar->setFixedHeight(50);
    videoTopBar->setStyleSheet("background-color: transparent;");
    QHBoxLayout *videoTopLayout = new QHBoxLayout(videoTopBar);
    videoTopLayout->setContentsMargins(0, 0, 0, 0);
    videoTopLayout->setSpacing(8);

    ResponsiveButton *backBtn = new ResponsiveButton(videoTopBar);
    backBtn->setFixedSize(40, 40);
    backBtn->setText(QStringLiteral("←"));
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setToolTip(QStringLiteral("返回"));
    backBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: transparent;"
        "  border: none;"
        "  color: rgba(240,240,240,230);"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background-color: rgba(255,255,255,30); border-radius: 18px; }"
        "QPushButton:pressed { background-color: rgba(255,255,255,50); border-radius: 18px; }"
    ));
    connect(backBtn, &QPushButton::clicked, this, [this]() { stopEmbeddedWatching(); });
    m_titleBackBtn = backBtn;

    QFrame *annotationContainer = new QFrame(videoTopBar);
    annotationContainer->setObjectName("VideoAnnotationContainer");
    annotationContainer->setFixedHeight(40);
    annotationContainer->setFrameShape(QFrame::NoFrame);
    annotationContainer->setStyleSheet(
        "#VideoAnnotationContainer {"
        "   background-color: #3b3b3b;"
        "   border-radius: 20px;"
        "}"
    );
    QHBoxLayout *annotationLayout = new QHBoxLayout(annotationContainer);
    annotationLayout->setContentsMargins(10, 0, 10, 0);
    annotationLayout->setSpacing(0);
    m_annotationToolbar = new AnnotationToolbar(annotationContainer);
    annotationLayout->addWidget(m_annotationToolbar);

    if (m_annotationToolbar) {
        connect(m_annotationToolbar, &AnnotationToolbar::toolSelected, this, [this](int mode) {
            if (!m_embeddedVideoWidget) return;
            if (mode == 0) {
                m_embeddedVideoWidget->setAnnotationEnabled(false);
                m_embeddedVideoWidget->setToolMode(0);
                return;
            }
            m_embeddedVideoWidget->setAnnotationEnabled(true);
            if (mode == 1) m_embeddedVideoWidget->setToolMode(0);
            else if (mode == 2) m_embeddedVideoWidget->setToolMode(2);
            else if (mode == 3) m_embeddedVideoWidget->setToolMode(3);
            else if (mode == 4) m_embeddedVideoWidget->setToolMode(5);
            else if (mode == 5) m_embeddedVideoWidget->setToolMode(4);
            else if (mode == 6) m_embeddedVideoWidget->setToolMode(1);
        });
        connect(m_annotationToolbar, &AnnotationToolbar::colorChanged, this, [this](int colorId) {
            if (m_embeddedVideoWidget) m_embeddedVideoWidget->setAnnotationColorId(colorId);
        });
        connect(m_annotationToolbar, &AnnotationToolbar::undoRequested, this, [this]() {
            if (m_embeddedVideoWidget) m_embeddedVideoWidget->sendUndo();
        });
        connect(m_annotationToolbar, &AnnotationToolbar::cameraRequested, this, [this]() {
            if (!m_embeddedVideoWidget) return;
            QImage img = m_embeddedVideoWidget->captureToImage();
            if (img.isNull()) return;
            QClipboard *cb = QGuiApplication::clipboard();
            if (cb) cb->setImage(img);
        });
        connect(m_annotationToolbar, &AnnotationToolbar::snippetRequested, this, [this]() {
            QScreen *screen = this->screen();
            if (!screen) return;
            QPixmap fullPix = screen->grabWindow(0);
            SnippetOverlay *overlay = new SnippetOverlay(fullPix);
            overlay->setGeometry(screen->geometry());
            overlay->show();
        });
        connect(m_annotationToolbar, &AnnotationToolbar::clearRequested, this, [this]() {
            if (m_embeddedVideoWidget) m_embeddedVideoWidget->sendClear();
        });
    }

    videoTopLayout->addSpacing(8);
    videoTopLayout->addWidget(backBtn);
    videoTopLayout->addWidget(annotationContainer);
    videoTopLayout->addStretch();

    videoLayout->addWidget(videoTopBar);
    m_embeddedVideoWidget = new VideoDisplayWidget(videoContainer);
    m_embeddedVideoWidget->setShowControls(false);
    m_embeddedVideoWidget->setAutoResize(true);
    m_embeddedVideoWidget->setStyleSheet(
        "VideoDisplayWidget {"
        "    background-color: #000000;"
        "    border: none;"
        "}"
    );
    connect(m_embeddedVideoWidget, &VideoDisplayWidget::receivingStopped, this, [this](const QString &, const QString &targetId) {
        if (!targetId.isEmpty()) {
            onVideoReceivingStopped(targetId);
            emit videoReceivingStopped(targetId);
        }
    });
    videoLayout->addWidget(m_embeddedVideoWidget);
    m_videoContentPage = videoContainer;
    m_rightContentStack->addWidget(m_videoContentPage);
    m_rightContentStack->setCurrentWidget(m_homeContentPage);

    rightLayout->addWidget(titleBar);
    rightLayout->addWidget(m_rightContentStack);

    // Connect selection change to update styles
    connect(m_listWidget, &QListWidget::itemSelectionChanged, [this]() {
        for(int i = 0; i < m_listWidget->count(); ++i) {
            QListWidgetItem *item = m_listWidget->item(i);
            QWidget *w = m_listWidget->itemWidget(item);
            if (w) {
                QFrame *card = w->findChild<QFrame*>("CardFrame");
                if (card) {
                    QString userId = card->property("userId").toString();
                    if (userId.isEmpty()) {
                        userId = item->data(Qt::UserRole).toString();
                    }
                    const bool watching = (!userId.isEmpty() &&
                                           userId != m_myStreamId &&
                                           !m_watchingTargetId.isEmpty() &&
                                           userId == m_watchingTargetId);
                    const bool beingWatchedBy = (!userId.isEmpty() && isInMyRoomViewerList(userId));
                    if (item->isSelected()) {
                        // Tech Orange Selection Style
                        card->setStyleSheet(
                            "#CardFrame {"
                            "   background-color: rgba(255, 102, 0, 40);" // Semi-transparent orange tint
                            "   border: 1px solid #FF6600;" // Tech Orange border, 1px
                            "   border-radius: 15px;"
                            "}"
                        );
                    } else if (watching) {
                        card->setStyleSheet(
                            "#CardFrame {"
                            "   background-color: rgba(0, 200, 83, 55);"
                            "   border: 1px solid #00C853;"
                            "   border-radius: 15px;"
                            "}"
                            "#CardFrame:hover {"
                            "   background-color: rgba(0, 200, 83, 70);"
                            "}"
                        );
                    } else if (beingWatchedBy) {
                        card->setStyleSheet(
                            "#CardFrame {"
                            "   background-color: rgba(0, 120, 212, 45);"
                            "   border: 1px solid #0078D4;"
                            "   border-radius: 15px;"
                            "}"
                            "#CardFrame:hover {"
                            "   background-color: rgba(0, 120, 212, 60);"
                            "}"
                        );
                    } else {
                        // Default Style
                        card->setStyleSheet(
                            "#CardFrame {"
                            "   background-color: #3b3b3b;"
                            "   border-radius: 15px;"
                            "   border: none;"
                            "}"
                            "#CardFrame:hover {"
                            "   background-color: #444;"
                            "}"
                        );
                    }
                }
            }
        }
    });

    // Assemble Main Layout
    mainLayout->addWidget(leftPanel);
    mainLayout->addWidget(rightPanel);
}

void NewUiWindow::showFunction1Browser()
{
    if (!m_rightContentStack || !m_function1BrowserPage) {
        return;
    }
    if (m_function1WebView) {
        QString v = AppConfig::readConfigValue(QStringLiteral("storyboard_url")).trimmed();
        if (v.isEmpty()) {
            v = QStringLiteral("http://124.221.247.99:9001/");
        }
        m_function1WebView->load(QUrl::fromUserInput(v));
    }
    m_rightContentStack->setCurrentWidget(m_function1BrowserPage);
}

void NewUiWindow::showHomeContent()
{
    if (!m_rightContentStack || !m_homeContentPage) {
        return;
    }
    m_rightContentStack->setCurrentWidget(m_homeContentPage);
}

void NewUiWindow::toggleFunction1Maximize()
{
    activateWindow();
    raise();

    Qt::WindowStates state = windowState();
    const bool currentlyMaximized = (state & Qt::WindowMaximized);
    if (currentlyMaximized) {
        setWindowState(state & ~Qt::WindowMaximized);
        showNormal();
    } else {
        setWindowState(state | Qt::WindowMaximized);
        showMaximized();
    }
    updateTitleMaximizeButton();
    setResizeGripsVisible(!(windowState() & Qt::WindowMaximized));
}

void NewUiWindow::updateTitleMaximizeButton()
{
    if (!m_titleMaximizeBtn) {
        return;
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    const bool currentlyMaximized = (windowState() & Qt::WindowMaximized);
    if (currentlyMaximized) {
        m_titleMaximizeBtn->setIcon(QIcon(appDir + "/maps/logo/Restore.png"));
        m_titleMaximizeBtn->setToolTip(QStringLiteral("还原"));
    } else {
        m_titleMaximizeBtn->setIcon(QIcon(appDir + "/maps/logo/maximize.png"));
        m_titleMaximizeBtn->setToolTip(QStringLiteral("最大化"));
    }
}

void NewUiWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event && event->type() == QEvent::WindowStateChange) {
        updateTitleMaximizeButton();
        setResizeGripsVisible(!(windowState() & Qt::WindowMaximized));
    }
}

bool NewUiWindow::event(QEvent *event)
{
    if (event && (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::WindowActivate)) {
        const bool active = (event->type() == QEvent::WindowActivate);
        if (!active) {
            stopSelfPreviewFast();
            if (m_selectionAutoPauseTimer) {
                m_selectionAutoPauseTimer->stop();
            }
            if (QApplication::applicationState() != Qt::ApplicationActive) {
                cancelHoverHiFps();
                const QStringList chs = m_hiFpsPublishers.keys();
                for (const QString &ch : chs) {
                    stopHiFpsPublishing(ch);
                }
            }
        } else {
            if (QApplication::applicationState() == Qt::ApplicationActive && m_listWidget) {
                QListWidgetItem *current = m_listWidget->currentItem();
                QString userId;
                if (current) {
                    userId = current->data(Qt::UserRole).toString();
                    if (userId.isEmpty()) {
                        if (QWidget *iw = m_listWidget->itemWidget(current)) {
                            if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                                userId = card->property("userId").toString();
                            } else {
                                userId = iw->property("userId").toString();
                            }
                        }
                    }
                }
                if (!userId.isEmpty() && userId == m_myStreamId) {
                    startSelfPreviewFast();
                } else {
                    stopSelfPreviewFast();
                }
                if (!userId.isEmpty() && userId != m_myStreamId) {
                    if (userId != m_autoPausedUserId) {
                        startHiFpsForUser(userId);
                        resetSelectionAutoPause(userId);
                    }
                }
            }
        }
    }
    return QWidget::event(event);
}

void NewUiWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint localPos = event->position().toPoint();
        if (localPos.y() <= 80) {
            m_titleBarDragging = true;
            m_titleBarPendingRestore = (windowState() & Qt::WindowMaximized);
            m_titleBarSnapMaximize = false;
            m_titleBarPressGlobal = event->globalPosition().toPoint();
            m_titleBarPressLocalInWindow = localPos;
            m_titleBarDragOffset = m_titleBarPressGlobal - frameGeometry().topLeft();
            m_dragging = false;
            event->accept();
            return;
        }
        m_dragging = true;
        // Use globalPosition() for Qt6
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void NewUiWindow::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && m_titleBarDragging) {
        const QPoint globalPos = event->globalPosition().toPoint();
        if (m_titleBarPendingRestore) {
            m_titleBarPendingRestore = false;
            const QRect restore = normalGeometry().isValid() ? normalGeometry() : geometry();
            const int restoreW = qMax(200, restore.width());
            const int restoreH = qMax(200, restore.height());
            const qreal xRatio = width() > 0 ? (qreal)m_titleBarPressLocalInWindow.x() / (qreal)width() : 0.5;
            const int newX = globalPos.x() - qRound(xRatio * restoreW);
            const int newY = globalPos.y() - m_titleBarPressLocalInWindow.y();
            showNormal();
            setGeometry(QRect(QPoint(newX, newY), QSize(restoreW, restoreH)));
            m_titleBarDragOffset = globalPos - frameGeometry().topLeft();
        } else {
            move(globalPos - m_titleBarDragOffset);
        }

        if (QScreen *screen = QGuiApplication::screenAt(globalPos)) {
            const QRect avail = screen->availableGeometry();
            m_titleBarSnapMaximize = (globalPos.y() <= avail.top() + 24);
        } else {
            m_titleBarSnapMaximize = false;
        }
        event->accept();
        return;
    }
    if (event->buttons() & Qt::LeftButton && m_dragging) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void NewUiWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_titleBarDragging) {
            const bool doMaximize = m_titleBarSnapMaximize && !(windowState() & Qt::WindowMaximized);
            m_titleBarDragging = false;
            m_titleBarPendingRestore = false;
            m_titleBarSnapMaximize = false;
            if (doMaximize) {
                toggleFunction1Maximize();
            }
            event->accept();
            return;
        }
        m_dragging = false;
        event->accept();
    }
}

void NewUiWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint localPos = event->position().toPoint();
        if (localPos.y() <= 80) {
            toggleFunction1Maximize();
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void NewUiWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateResizeGrips();
    if (m_farRightPanel) {
        const int outerMargin = 10;
        const int panelW = m_farRightPanel->width();
        int yTop = outerMargin;
        if (m_titleBar) {
            const QPoint p = m_titleBar->mapTo(this, QPoint(0, 0));
            yTop = p.y() + m_titleBar->height() + outerMargin;
        }
        const int panelH = qMax(0, height() - yTop - outerMargin);
        m_farRightPanel->setGeometry(width() - outerMargin - panelW, yTop, panelW, panelH);
        m_farRightPanel->raise();
    }
}

bool NewUiWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_audioCallDialog) {
        bool insideCallDialog = false;
        QObject *cur = watched;
        while (cur) {
            if (cur == m_audioCallDialog) {
                insideCallDialog = true;
                break;
            }
            cur = cur->parent();
        }

        if (insideCallDialog) {
            bool insideButton = false;
            cur = watched;
            while (cur && cur != m_audioCallDialog) {
                if (qobject_cast<QAbstractButton*>(cur)) {
                    insideButton = true;
                    break;
                }
                cur = cur->parent();
            }

            if (!insideButton) {
                if (event->type() == QEvent::MouseButtonPress) {
                    auto *me = static_cast<QMouseEvent*>(event);
                    if (me->button() == Qt::LeftButton) {
                        m_audioCallDialogDragging = true;
                        m_audioCallDialogDragOffset = me->globalPosition().toPoint() - m_audioCallDialog->frameGeometry().topLeft();
                        event->accept();
                        return true;
                    }
                } else if (event->type() == QEvent::MouseMove) {
                    if (m_audioCallDialogDragging) {
                        auto *me = static_cast<QMouseEvent*>(event);
                        m_audioCallDialog->move(me->globalPosition().toPoint() - m_audioCallDialogDragOffset);
                        event->accept();
                        return true;
                    }
                } else if (event->type() == QEvent::MouseButtonRelease) {
                    auto *me = static_cast<QMouseEvent*>(event);
                    if (me->button() == Qt::LeftButton) {
                        m_audioCallDialogDragging = false;
                        event->accept();
                        return true;
                    }
                }
            }
        }
    }

    if (m_audioCallMiniBar) {
        bool insideMini = false;
        QObject *cur = watched;
        while (cur) {
            if (cur == m_audioCallMiniBar) {
                insideMini = true;
                break;
            }
            cur = cur->parent();
        }
        if (insideMini) {
            if (event->type() == QEvent::MouseButtonDblClick) {
                auto *me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton) {
                    m_audioCallMiniBarDragging = false;
                    setAudioCallMiniHidden(false);
                    hideAudioCallMiniBar();
                    if (m_audioCallDialog && !m_audioCallPeerId.isEmpty()) {
                        m_audioCallDialog->show();
                        m_audioCallDialog->raise();
                    }
                    return true;
                }
            } else if (event->type() == QEvent::MouseButtonPress) {
                auto *me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton) {
                    m_audioCallMiniBarDragging = true;
                    m_audioCallMiniBarDragOffset = me->globalPosition().toPoint() - m_audioCallMiniBar->frameGeometry().topLeft();
                    event->accept();
                    return true;
                }
            } else if (event->type() == QEvent::MouseMove) {
                if (m_audioCallMiniBarDragging) {
                    auto *me = static_cast<QMouseEvent*>(event);
                    m_audioCallMiniBar->move(me->globalPosition().toPoint() - m_audioCallMiniBarDragOffset);
                    event->accept();
                    return true;
                }
            } else if (event->type() == QEvent::MouseButtonRelease) {
                auto *me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton) {
                    m_audioCallMiniBarDragging = false;
                    event->accept();
                    return true;
                }
            }
        }
    }

    if (watched == m_resizeGripLeft || watched == m_resizeGripRight || watched == m_resizeGripTop || watched == m_resizeGripBottom ||
        watched == m_resizeGripTopLeft || watched == m_resizeGripTopRight || watched == m_resizeGripBottomLeft || watched == m_resizeGripBottomRight) {
        if (windowState() & Qt::WindowMaximized) {
            return true;
        }
        auto edgesForGrip = [this](QObject *o) -> Qt::Edges {
            if (o == m_resizeGripLeft) return Qt::LeftEdge;
            if (o == m_resizeGripRight) return Qt::RightEdge;
            if (o == m_resizeGripTop) return Qt::TopEdge;
            if (o == m_resizeGripBottom) return Qt::BottomEdge;
            if (o == m_resizeGripTopLeft) return Qt::LeftEdge | Qt::TopEdge;
            if (o == m_resizeGripTopRight) return Qt::RightEdge | Qt::TopEdge;
            if (o == m_resizeGripBottomLeft) return Qt::LeftEdge | Qt::BottomEdge;
            if (o == m_resizeGripBottomRight) return Qt::RightEdge | Qt::BottomEdge;
            return Qt::Edges();
        };

        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_resizeDragging = true;
                m_resizeEdges = edgesForGrip(watched);
                m_resizePressGlobal = me->globalPosition().toPoint();
                m_resizeStartGeometry = frameGeometry();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            if (m_resizeDragging) {
                auto *me = static_cast<QMouseEvent*>(event);
                const QPoint gp = me->globalPosition().toPoint();
                const int dx = gp.x() - m_resizePressGlobal.x();
                const int dy = gp.y() - m_resizePressGlobal.y();
                QRect r = m_resizeStartGeometry;

                const int minW = qMax(500, minimumWidth());
                const int minH = qMax(480, minimumHeight());

                if (m_resizeEdges.testFlag(Qt::LeftEdge)) r.setLeft(r.left() + dx);
                if (m_resizeEdges.testFlag(Qt::RightEdge)) r.setRight(r.right() + dx);
                if (m_resizeEdges.testFlag(Qt::TopEdge)) r.setTop(r.top() + dy);
                if (m_resizeEdges.testFlag(Qt::BottomEdge)) r.setBottom(r.bottom() + dy);

                if (r.width() < minW) {
                    if (m_resizeEdges.testFlag(Qt::LeftEdge)) r.setLeft(r.right() - minW + 1);
                    else r.setRight(r.left() + minW - 1);
                }
                if (r.height() < minH) {
                    if (m_resizeEdges.testFlag(Qt::TopEdge)) r.setTop(r.bottom() - minH + 1);
                    else r.setBottom(r.top() + minH - 1);
                }
                setGeometry(r);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_resizeDragging = false;
                m_resizeEdges = Qt::Edges();
                return true;
            }
        }
        return true;
    }

    if (m_farRightPanel && m_farRightPanel->isVisible() && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            bool insidePanel = false;
            QObject *cur = watched;
            while (cur) {
                if (cur == m_farRightPanel) {
                    insidePanel = true;
                    break;
                }
                cur = cur->parent();
            }

            if (!insidePanel && watched != m_localCard) {
                bool insideTitleBar = false;
                if (m_titleBar) {
                    QObject *t = watched;
                    while (t) {
                        if (t == m_titleBar) {
                            insideTitleBar = true;
                            break;
                        }
                        t = t->parent();
                    }
                }
                bool insideList = (m_listWidget && watched == m_listWidget->viewport());
                if (insideList) {
                    QListWidgetItem *pressedItem = m_listWidget->itemAt(me->pos());
                    if (pressedItem && m_listWidget->row(pressedItem) == 0) {
                        insideList = false;
                    }
                }
            }
        }
    }

    if (m_titleBar && (watched == m_titleBar || watched->objectName() == QStringLiteral("ToolsContainer") || watched->objectName() == QStringLiteral("TitleControlContainer"))) {
        if (qobject_cast<QAbstractButton*>(watched)) {
            return QWidget::eventFilter(watched, event);
        }
        if (watched == m_toolbarAvatarLabel || watched == m_localAvatarLabel) {
            return QWidget::eventFilter(watched, event);
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                toggleFunction1Maximize();
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_titleBarDragging = true;
                m_titleBarPendingRestore = (windowState() & Qt::WindowMaximized);
                m_titleBarSnapMaximize = false;
                m_titleBarPressGlobal = me->globalPosition().toPoint();
                m_titleBarPressLocalInWindow = mapFromGlobal(m_titleBarPressGlobal);
                m_titleBarDragOffset = m_titleBarPressGlobal - frameGeometry().topLeft();
                return true;
            }
        }
        if (event->type() == QEvent::MouseMove) {
            if (m_titleBarDragging) {
                auto *me = static_cast<QMouseEvent*>(event);
                if (!(me->buttons() & Qt::LeftButton)) {
                    m_titleBarDragging = false;
                    m_titleBarPendingRestore = false;
                    m_titleBarSnapMaximize = false;
                    return true;
                }
                const QPoint globalPos = me->globalPosition().toPoint();
                if (m_titleBarPendingRestore) {
                    m_titleBarPendingRestore = false;
                    const QRect restore = normalGeometry().isValid() ? normalGeometry() : geometry();
                    const int restoreW = qMax(200, restore.width());
                    const int restoreH = qMax(200, restore.height());
                    const qreal xRatio = width() > 0 ? (qreal)m_titleBarPressLocalInWindow.x() / (qreal)width() : 0.5;
                    const int newX = globalPos.x() - qRound(xRatio * restoreW);
                    const int newY = globalPos.y() - m_titleBarPressLocalInWindow.y();
                    showNormal();
                    setGeometry(QRect(QPoint(newX, newY), QSize(restoreW, restoreH)));
                    m_titleBarDragOffset = globalPos - frameGeometry().topLeft();
                } else {
                    move(globalPos - m_titleBarDragOffset);
                }

                if (QScreen *screen = QGuiApplication::screenAt(globalPos)) {
                    const QRect avail = screen->availableGeometry();
                    m_titleBarSnapMaximize = (globalPos.y() <= avail.top() + 24);
                } else {
                    m_titleBarSnapMaximize = false;
                }
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                const bool doMaximize = m_titleBarDragging && m_titleBarSnapMaximize && !(windowState() & Qt::WindowMaximized);
                m_titleBarDragging = false;
                m_titleBarPendingRestore = false;
                m_titleBarSnapMaximize = false;
                if (doMaximize) {
                    toggleFunction1Maximize();
                }
                return true;
            }
        }
    }

    if (m_listWidget && watched == m_listWidget->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            QListWidgetItem *item = m_listWidget->itemAt(me->pos());
            if (!item) {
                m_listWidget->clearSelection();
                m_listWidget->setCurrentItem(nullptr);
                cancelHoverHiFps();
                resetSelectionAutoPause(QString());
                return true;
            }
        }
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            const QVariant v = watched->property("userId");
            if (v.isValid()) {
                const QString userId = v.toString();
                if (!userId.isEmpty() && userId != m_myStreamId) {
                    if (m_listWidget) {
                        const QListWidgetItem *it = m_userItems.value(userId, nullptr);
                        if (it) {
                            m_listWidget->setCurrentItem(const_cast<QListWidgetItem*>(it));
                        }
                    }
                    if (QApplication::applicationState() == Qt::ApplicationActive) {
                        if (userId == m_autoPausedUserId) {
                            resumeSelectedStreamForUser(userId);
                        }
                        startHiFpsForUser(userId);
                        resetSelectionAutoPause(userId);
                    } else {
                        cancelHoverHiFps();
                        resetSelectionAutoPause(QString());
                    }
                }
            }
        }
    }

    if (watched == m_logoLabel && event->type() == QEvent::MouseButtonRelease) {
        QDesktopServices::openUrl(QUrl("http://www.iruler.cn"));
        return true;
    }

    if ((watched == m_toolbarAvatarLabel || watched == m_localAvatarLabel) &&
        event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            pickAndApplyLocalAvatar();
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonDblClick) {
        QString userId = watched->property("userId").toString();
        if (!userId.isEmpty()) {
            if (watched == m_localCard) {
                return true;
            }
            QString name = watched->property("userName").toString();
            emit startWatchingRequested(userId, name);
            return true; // Event handled
        }
    }
    return QWidget::eventFilter(watched, event);
}

void NewUiWindow::setGlobalMicCheckedSilently(bool enabled)
{
    m_globalMicEnabled = enabled;
    if (!m_titleMicBtn) return;
    if (!m_titleMicBtn->isCheckable()) return;
    if (m_titleMicBtn->isChecked() == enabled) return;

    QSignalBlocker blocker(m_titleMicBtn);
    m_titleMicBtn->setChecked(enabled);
    if (!m_titleMicIconOn.isNull() && !m_titleMicIconOff.isNull()) {
        m_titleMicBtn->setIcon(enabled ? m_titleMicIconOn : m_titleMicIconOff);
    }
    m_titleMicBtn->setToolTip(enabled ? QStringLiteral("麦克风：开") : QStringLiteral("麦克风：关"));
    janusSetMuted(!enabled);
}

void NewUiWindow::onBroadcastBtnClicked()
{
    BroadcastNoticeDialog dlg(this);
    connect(&dlg, &BroadcastNoticeDialog::publishRequested, this, &NewUiWindow::broadcastRequested);
    dlg.exec();
}
