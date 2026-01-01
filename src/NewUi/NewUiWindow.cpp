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
#include <QFileInfo>
#include <QFileDialog>
#include <QSaveFile>
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
#include <QBuffer>
#include <QImage>
#include <QStackedWidget>
#include <QUrl>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QSignalBlocker>
#include <climits>
#include <QAbstractButton>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "../common/AppConfig.h"

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
    m_timer->start(10 * 1000); // 10s interval (will be rate-limited for Cloud)
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
        const QStringList bases = AppConfig::localLanBaseUrls();
        const QString base = bases.value(0);
        if (!base.isEmpty()) {
            QUrl u(base);
            u.setPath(QStringLiteral("/publish/%1").arg(previewChannelId));
            m_streamClientLan->connectToServer(u);
        }
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
            const QStringList bases = AppConfig::localLanBaseUrls();
            const QString base = bases.value(0);
            if (!base.isEmpty()) {
                QUrl u(base);
                u.setPath(QStringLiteral("/publish/%1").arg(channelId));
                m_avatarPublisherLan->connectToServer(u);
            }
        }

        ensureAvatarSubscription(m_myStreamId);
        refreshLocalAvatarFromCache();
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
    const QString iconName = isOn ? "get.png" : "end.png";
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
}

void NewUiWindow::updateTalkOverlay(const QString &userId)
{
    QLabel *overlay = m_talkOverlays.value(userId, nullptr);
    if (!overlay) {
        return;
    }
    QPushButton *btn = m_talkButtons.value(userId, nullptr);
    if (!btn) {
        overlay->setVisible(false);
        return;
    }
    const bool pending = btn->property("isPending").toBool();
    const bool on = btn->property("isOn").toBool();
    const bool visible = (pending || on);
    overlay->setVisible(visible);

    if (m_listWidget) {
        QListWidgetItem *item = m_userItems.value(userId, nullptr);
        QWidget *w = item ? m_listWidget->itemWidget(item) : nullptr;
        QFrame *card = w ? w->findChild<QFrame*>("CardFrame") : nullptr;
        if (card) {
            const bool selected = item && item->isSelected();
            const bool talking = visible;
            if (selected) {
                card->setStyleSheet(
                    "#CardFrame {"
                    "   background-color: rgba(255, 102, 0, 40);"
                    "   border: 1px solid #FF6600;"
                    "   border-radius: 15px;"
                    "}"
                );
            } else if (talking) {
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
        talkOverlay->setText(QStringLiteral("通话中"));
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

            connect(tabBtn, &QPushButton::clicked, this, [this, id]() {
                emit startWatchingRequested(id);
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
            connect(micBtn, &QPushButton::clicked, [this, micBtn, appDir, id]() {
                const bool remoteActive = micBtn->property("remoteActive").toBool();
                bool isOn = micBtn->property("isOn").toBool();
                if (remoteActive) {
                    setTalkRemoteActive(id, false);
                    if (id != m_myStreamId) {
                        setTalkConnected(id, false);
                        emit kickViewerRequested(id);
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
    
    // Title buttons (Menu, Min, Close) with background
    
    // --- New Tool Buttons Group (Left side of title bar) ---
    // Use QFrame to ensure background styling works without custom paintEvent
    QFrame *toolsContainer = new QFrame(titleBar);
    toolsContainer->setObjectName("ToolsContainer");
    toolsContainer->setFixedSize(160, 40);
    toolsContainer->setFrameShape(QFrame::NoFrame);
    toolsContainer->installEventFilter(this);
    // REMOVED: setAttribute(Qt::WA_TranslucentBackground); which was hiding the background
    
    // Style for the pill-shaped background
    toolsContainer->setStyleSheet(
        "#ToolsContainer {"
        "   background-color: #3b3b3b;"
        "   border-radius: 20px;" 
        "}"
        "QPushButton {"
        "   background-color: transparent;"
        "   border: none;"
        "   margin: 3px;" // Margin to make hover effect smaller
        "}"
        "QPushButton:hover {"
        "   background-color: rgba(255, 255, 255, 30);"
        "   border-radius: 17px;" // Adjusted radius for smaller hover area (40-6)/2
        "}"
        // Pressed state handled by ResponsiveButton paintEvent for icon scaling
        "QPushButton:pressed {"
        "   background-color: rgba(255, 255, 255, 40);"
        "}"
    );

    QHBoxLayout *toolsLayout = new QHBoxLayout(toolsContainer);
    toolsLayout->setContentsMargins(10, 0, 10, 0); // Increased margins to space out buttons
    toolsLayout->setSpacing(5);
    toolsLayout->setAlignment(Qt::AlignCenter);

    QLabel *toolbarAvatarLabel = new QLabel(titleBar);
    toolbarAvatarLabel->setFixedSize(30, 30);
    toolbarAvatarLabel->setAlignment(Qt::AlignCenter);
    toolbarAvatarLabel->setCursor(Qt::PointingHandCursor);
    toolbarAvatarLabel->setToolTip(QStringLiteral("更换头像"));
    toolbarAvatarLabel->installEventFilter(this);
    toolbarAvatarLabel->setStyleSheet(
        "QLabel {"
        "   background: transparent;"
        "   border: none;"
        "}"
    );
    m_toolbarAvatarLabel = toolbarAvatarLabel;
    refreshLocalAvatarFromCache();

    // d.png
    ResponsiveButton *toolBtn1 = new ResponsiveButton();
    toolBtn1->setFixedSize(40, 40); 
    toolBtn1->setIcon(QIcon(appDir + "/maps/logo/d.png"));
    toolBtn1->setIconSize(QSize(24, 24)); 
    toolBtn1->setCursor(Qt::PointingHandCursor);
    toolBtn1->setToolTip("灵动岛");
    toolBtn1->installEventFilter(this);
    connect(toolBtn1, &QPushButton::clicked, this, &NewUiWindow::toggleStreamingIslandRequested);

    // log.png
    ResponsiveButton *toolBtn2 = new ResponsiveButton();
    toolBtn2->setFixedSize(40, 40); 
    toolBtn2->setIcon(QIcon(appDir + "/maps/logo/log.png"));
    toolBtn2->setIconSize(QSize(24, 24)); 
    toolBtn2->setCursor(Qt::PointingHandCursor);
    toolBtn2->setToolTip("日志");
    toolBtn2->installEventFilter(this);

    // clearn.png
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
    titleLayout->addWidget(toolbarAvatarLabel);
    titleLayout->addSpacing(8);
    titleLayout->addWidget(toolsContainer);
    titleLayout->addStretch();

    QWidget *controlContainer = new QWidget(titleBar);
    // Size adjustment:
    // Buttons: 48x48 (Double size)
    // Container width: 48*5 = 240. Height: 48.
    controlContainer->setFixedSize(240, 48); 
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

    ResponsiveButton *exitBtn = new ResponsiveButton();
    exitBtn->setFixedSize(48, 48);
    exitBtn->setText(QStringLiteral("测试退出"));
    exitBtn->setStyleSheet("QPushButton { color: #e0e0e0; font-size: 12px; }");
    exitBtn->setCursor(Qt::PointingHandCursor);
    exitBtn->setToolTip(QStringLiteral("测试退出"));
    exitBtn->installEventFilter(this);
    connect(exitBtn, &QPushButton::clicked, qApp, &QCoreApplication::quit);
    exitBtn->setVisible(true);

    // Menu Button
    ResponsiveButton *menuBtn = new ResponsiveButton();
    menuBtn->setFixedSize(48, 48); 
    const QIcon micIconOn(appDir + "/maps/logo/Mic_on.png");
    const QIcon micIconOff(appDir + "/maps/logo/Mic_off.png");
    auto loadMicEnabled = [&appDir]() -> bool {
        QFile f(appDir + "/config/app_config.txt");
        if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            while (!in.atEnd()) {
                const QString line = in.readLine();
                if (line.startsWith("mic_enabled=")) {
                    const QString v = line.mid(QString("mic_enabled=").length()).trimmed();
                    f.close();
                    return v.compare("true", Qt::CaseInsensitive) == 0;
                }
            }
            f.close();
        }
        return true;
    };
    const bool initialMicEnabled = loadMicEnabled();
    menuBtn->setCheckable(true);
    menuBtn->setChecked(initialMicEnabled);
    menuBtn->setIcon(initialMicEnabled ? micIconOn : micIconOff);
    menuBtn->setIconSize(QSize(16, 16)); 
    menuBtn->setCursor(Qt::PointingHandCursor);
    menuBtn->setToolTip(initialMicEnabled ? QStringLiteral("麦克风：开") : QStringLiteral("麦克风：关"));
    menuBtn->installEventFilter(this);
    m_titleMicBtn = menuBtn;
    m_titleMicIconOn = micIconOn;
    m_titleMicIconOff = micIconOff;
    connect(menuBtn, &QPushButton::toggled, this, [this, menuBtn, micIconOn, micIconOff](bool enabled) {
        menuBtn->setIcon(enabled ? micIconOn : micIconOff);
        menuBtn->setToolTip(enabled ? QStringLiteral("麦克风：开") : QStringLiteral("麦克风：关"));
        emit micToggleRequested(enabled);
    });

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

    controlLayout->addWidget(exitBtn);
    controlLayout->addWidget(menuBtn);
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

        // Add Actions
        contextMenu.addAction("打开 (Open)");
        contextMenu.addAction("重命名 (Rename)");
        contextMenu.addSeparator();
        contextMenu.addAction("删除 (Delete)");
        contextMenu.addAction("属性 (Properties)");

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
             emit startWatchingRequested(userId);
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
             QPixmap original = screen->grabWindow(0);
             srcPix = original.scaledToWidth(m_cardBaseWidth, Qt::SmoothTransformation);
        } else {
             srcPix = QPixmap(m_cardBaseWidth, (int)(m_cardBaseWidth/1.77));
             srcPix.fill(Qt::black);
        }

        // Process Image (Rounded Corners)
        QPixmap pixmap(m_imgWidth, m_imgHeight); 
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

        // Bottom Controls Layout
        QHBoxLayout *bottomLayout = new QHBoxLayout();
        // Zero side margins because parent cardLayout already provides MARGIN_X
        // But we might want buttons to extend a bit wider? No, keep alignment.
        // Add a bit of bottom padding
        bottomLayout->setContentsMargins(0, 0, 0, 5); 
        bottomLayout->setSpacing(5);

        QPushButton *tabBtn = new QPushButton();
        tabBtn->setFixedSize(14, 14);
        tabBtn->setCursor(Qt::PointingHandCursor);
        tabBtn->setFlat(true);
        tabBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
        tabBtn->setIcon(QIcon(appDir + "/maps/logo/in.png"));
        tabBtn->setIconSize(QSize(14, 14));
        connect(tabBtn, &QPushButton::clicked, this, [this]() {
            if (m_myStreamId.isEmpty()) {
                return;
            }
            emit startWatchingRequested(m_myStreamId);
        });
        
        // Text Label (Middle)
    QString displayName = m_myUserName.isEmpty() ? m_myStreamId : m_myUserName;
    // Format: Name only (ID removed as requested)
    QString fullText = displayName;
    QLabel *txtLabel = new QLabel(fullText);
    txtLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_localNameLabel = txtLabel; // Store pointer for updates
    txtLabel->setStyleSheet("color: #e0e0e0; font-size: 12px; border: none; background: transparent;");
    txtLabel->setAlignment(Qt::AlignCenter);
        bottomLayout->addWidget(tabBtn);
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

    m_function1WebView->load(QUrl("http://8.130.8.86:3000/"));
    browserLayout->addWidget(m_function1WebView);

    m_function1BrowserPage = browserContainer;
    m_rightContentStack->addWidget(m_function1BrowserPage);
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
                    bool talking = false;
                    if (!userId.isEmpty()) {
                        QPushButton *btn = m_talkButtons.value(userId, nullptr);
                        if (btn) {
                            talking = btn->property("isOn").toBool() || btn->property("isPending").toBool();
                        }
                    }
                    if (item->isSelected()) {
                        // Tech Orange Selection Style
                        card->setStyleSheet(
                            "#CardFrame {"
                            "   background-color: rgba(255, 102, 0, 40);" // Semi-transparent orange tint
                            "   border: 1px solid #FF6600;" // Tech Orange border, 1px
                            "   border-radius: 15px;"
                            "}"
                        );
                    } else if (talking) {
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

    // --- Far Right Panel (New Interface) ---
    m_farRightPanel = new QWidget(this);
    m_farRightPanel->setObjectName("FarRightPanel");
    m_farRightPanel->setFixedWidth(240); // Wider than left panel (80px)
    m_farRightPanel->setVisible(false); // Default hidden
    m_farRightPanel->setStyleSheet(
        "QWidget#FarRightPanel {"
        "   background-color: #2b2b2b;"
        "   border-radius: 20px;"
        "}"
    );

    QVBoxLayout *farRightLayout = new QVBoxLayout(m_farRightPanel);
    farRightLayout->setContentsMargins(20, 20, 20, 20);
    farRightLayout->setSpacing(15);

    // Title
    QLabel *frTitle = new QLabel("我的房间"); 
    frTitle->setStyleSheet("color: white; font-size: 16px; font-weight: bold; background: transparent;");
    frTitle->setAlignment(Qt::AlignCenter);
    farRightLayout->addWidget(frTitle);

    // List
    m_viewerList = new QListWidget();
    m_viewerList->setFrameShape(QFrame::NoFrame);
    // Enable auto-adjust to prevent horizontal scrollbar issues
    m_viewerList->setResizeMode(QListWidget::Adjust); 
    m_viewerList->setStyleSheet(
        "QListWidget {"
        "   background: transparent;"
        "   border: none;"
        "   outline: none;"
        "}"
        "QListWidget::item {"
        "   background: transparent;"
        "   border-bottom: 1px solid #444;"
        "}"
        "QListWidget::item:hover {"
        "   background: rgba(255, 255, 255, 10);"
        "}"
        "QListWidget::item:selected {"
        "   background: transparent;"
        "}"
        // Style the vertical scrollbar to be thin and unobtrusive
        "QScrollBar:vertical {"
        "    border: none;"
        "    background: transparent;"
        "    width: 6px;"
        "    margin: 0px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: #555;"
        "    min-height: 20px;"
        "    border-radius: 3px;"
        "}"
    );
    m_viewerList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_viewerList->verticalScrollBar()->setSingleStep(10); // Small scroll step
    m_viewerList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); 
    
    // Items will be added dynamically via addViewer()
    
    farRightLayout->addWidget(m_viewerList);

    // Quit Button
    QPushButton *quitBtn = new QPushButton("关闭房间");
    quitBtn->setCursor(Qt::PointingHandCursor);
    quitBtn->setFixedHeight(30);
    quitBtn->setFixedWidth(80);
    quitBtn->setStyleSheet(
        "QPushButton {"
        "   background-color: #220505ff;" // Dark Red
        "   color: white;"
        "   font-size: 14px;"
        "   border-radius: 8px;"
        "   border: none;"
        "}"
        "QPushButton:hover {"
        "   background-color: #290505ff;" // Lighter Red (Brownish)
        "}"
        "QPushButton:pressed {"
        "   background-color: #350909ff;" // Very Dark Red
        "}"
    );
    connect(quitBtn, &QPushButton::clicked, this, [this]() {
        const QStringList viewerIds = m_viewerItems.keys();
        qInfo().noquote() << "[KickDiag] close_room clicked"
                          << " my_id=" << m_myStreamId
                          << " viewer_count=" << viewerIds.size();
        for (const QString &viewerId : viewerIds) {
            emit kickViewerRequested(viewerId);
        }
        emit closeRoomRequested();
    });

    // Center the button horizontally
    farRightLayout->addWidget(quitBtn, 0, Qt::AlignHCenter);

    // Assemble Main Layout
    mainLayout->addWidget(leftPanel);
    mainLayout->addWidget(rightPanel);
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

void NewUiWindow::showFunction1Browser()
{
    if (!m_rightContentStack || !m_function1BrowserPage) {
        return;
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
                const bool insideLeftBlank = (watched && watched->objectName() == QStringLiteral("LeftPanel"));
                if (insideTitleBar || insideList || insideLeftBlank) {
                    m_farRightPanel->setVisible(false);
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

    if (event->type() == QEvent::MouseButtonRelease && watched != m_localCard && m_farRightPanel && m_farRightPanel->isVisible()) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            const QString userId = watched->property("userId").toString();
            if (!userId.isEmpty() && userId != m_myStreamId) {
                m_farRightPanel->setVisible(false);
            }
        }
    }

    // Handle single click on local card to toggle "My Room" panel
    if (watched == m_localCard && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() != Qt::LeftButton) {
            return false;
        }
        if (m_farRightPanel) {
            const bool nextVisible = !m_farRightPanel->isVisible();
            m_farRightPanel->setVisible(nextVisible);
            if (nextVisible) {
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
        return true;
    }

    if (event->type() == QEvent::MouseButtonDblClick) {
        QString userId = watched->property("userId").toString();
        // [Interaction Fix] Allow any valid user ID
        // For local user (m_localCard), we prevent double-click streaming because single-click handles side panel
        if (!userId.isEmpty()) {
            if (watched == m_localCard) {
                return true; // Consume double click on self without action
            }
            if (m_farRightPanel) {
                m_farRightPanel->setVisible(false);
            }
            emit startWatchingRequested(userId);
            return true; // Event handled
        }
    }
    return QWidget::eventFilter(watched, event);
}

void NewUiWindow::addViewer(const QString &id, const QString &name)
{
    // [Fix] Prevent duplicate items but allow name updates
    if (m_viewerItems.contains(id)) {
        QListWidgetItem *existingItem = m_viewerItems.value(id);
        if (existingItem) {
             QWidget *w = m_viewerList->itemWidget(existingItem);
             if (w) {
                 QList<QLabel*> labels = w->findChildren<QLabel*>();
                 for (auto label : labels) {
                     // Update name if changed
                     label->setText(name.isEmpty() ? id : name);
                     break; 
                 }
             }
        }
        return; 
    }

    if (!m_viewerList) return;

    QString appDir = QCoreApplication::applicationDirPath();

    QListWidgetItem *item = new QListWidgetItem(m_viewerList);
    // [Fix] Store ID in UserRole for reliable lookup
    item->setData(Qt::UserRole, id);

    // Reduced width hint to ensure it fits within the panel even with scrollbar
    // Panel Width (240) - Panel Margin (40) - Scrollbar (approx 10) - Buffer = 180
    item->setSizeHint(QSize(180, 50));
    
    QWidget *w = new QWidget();
    w->setStyleSheet("background: transparent;");
    QHBoxLayout *l = new QHBoxLayout(w);
    // Standard margins
    l->setContentsMargins(10, 0, 10, 0); 
    
    QLabel *txt = new QLabel(name.isEmpty() ? id : name);
    txt->setStyleSheet("color: #dddddd; font-size: 14px; border: none;");

    QPushButton *removeBtn = new QPushButton();
    removeBtn->setFixedSize(24, 24);
    removeBtn->setCursor(Qt::PointingHandCursor);
    removeBtn->setIcon(QIcon(appDir + "/maps/logo/Remove.png"));
    removeBtn->setIconSize(QSize(18, 18));
    removeBtn->setFlat(true);
    removeBtn->setStyleSheet("border: none; background: transparent;");
    removeBtn->setToolTip("移除");
    removeBtn->installEventFilter(this);

    connect(removeBtn, &QPushButton::clicked, this, [this, id]() {
        qInfo().noquote() << "[KickDiag] kick button clicked"
                          << " viewer_id=" << id
                          << " my_id=" << m_myStreamId;
        emit kickViewerRequested(id);
    });
    
    QPushButton *mic = new QPushButton();
    mic->setFixedSize(24, 24);
    mic->setCursor(Qt::ArrowCursor);
    const bool initialMic = m_viewerMicStates.value(id, false);
    mic->setIcon(QIcon(appDir + "/maps/logo/" + QString(initialMic ? "Mic_on.png" : "Mic_off.png")));
    mic->setIconSize(QSize(18, 18));
    mic->setFlat(true);
    mic->setStyleSheet("border: none; background: transparent;");
    mic->installEventFilter(this);
    mic->setProperty("isOn", initialMic);

    l->addWidget(txt);
    l->addStretch();
    l->addWidget(removeBtn);
    l->addWidget(mic);
    
    m_viewerList->setItemWidget(item, w);
    m_viewerItems.insert(id, item);
    m_viewerMicButtons.insert(id, mic);
}

void NewUiWindow::setViewerMicState(const QString &viewerId, bool enabled)
{
    m_viewerMicStates[viewerId] = enabled;
    QPushButton *btn = m_viewerMicButtons.value(viewerId, nullptr);
    if (!btn) return;
    const QString appDir = QCoreApplication::applicationDirPath();
    btn->setProperty("isOn", enabled);
    btn->setIcon(QIcon(appDir + "/maps/logo/" + QString(enabled ? "Mic_on.png" : "Mic_off.png")));
}

void NewUiWindow::setGlobalMicCheckedSilently(bool enabled)
{
    if (!m_titleMicBtn) return;
    if (!m_titleMicBtn->isCheckable()) return;
    if (m_titleMicBtn->isChecked() == enabled) return;

    QSignalBlocker blocker(m_titleMicBtn);
    m_titleMicBtn->setChecked(enabled);
    if (!m_titleMicIconOn.isNull() && !m_titleMicIconOff.isNull()) {
        m_titleMicBtn->setIcon(enabled ? m_titleMicIconOn : m_titleMicIconOff);
    }
    m_titleMicBtn->setToolTip(enabled ? QStringLiteral("麦克风：开") : QStringLiteral("麦克风：关"));
}

void NewUiWindow::updateViewerNameIfExists(const QString &id, const QString &name)
{
    if (!m_viewerItems.contains(id)) return;
    QListWidgetItem *existingItem = m_viewerItems.value(id);
    if (!existingItem) return;
    QWidget *w = m_viewerList ? m_viewerList->itemWidget(existingItem) : nullptr;
    if (!w) return;
    QList<QLabel*> labels = w->findChildren<QLabel*>();
    for (auto label : labels) {
        label->setText(name.isEmpty() ? id : name);
        break;
    }
}

void NewUiWindow::sendKickToSubscribers(const QString &viewerId)
{
    if (!m_streamClient || !m_streamClient->isConnected()) {
        qInfo().noquote() << "[KickDiag] kick not sent to room: stream client not connected"
                          << " viewer_id=" << viewerId
                          << " my_id=" << m_myStreamId;
        return;
    }
    QJsonObject msg;
    msg["type"] = "kick_viewer";
    msg["viewer_id"] = viewerId;
    msg["target_id"] = m_myStreamId;
    msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    QString payload = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    qint64 bytes = m_streamClient->sendTextMessage(payload);
    qInfo().noquote() << "[KickDiag] kick_viewer sent to room"
                      << " bytes=" << bytes
                      << " payload=" << payload;
}

void NewUiWindow::removeViewer(const QString &id)
{
    // 1. Try to remove using the map
    if (m_viewerItems.contains(id)) {
        QListWidgetItem *item = m_viewerItems.take(id);
        if (item) {
            int row = m_viewerList->row(item);
            if (row >= 0) {
                m_viewerList->takeItem(row);
            }
            delete item;
        }
    }
    m_viewerMicButtons.remove(id);
    m_viewerMicStates.remove(id);
    
    // 2. [Safety] Iterate to clean up any duplicates or map desyncs
    // Loop backwards to safely remove items
    for(int i = m_viewerList->count() - 1; i >= 0; --i) {
        QListWidgetItem* item = m_viewerList->item(i);
        // Check UserRole first (most reliable)
        if (item->data(Qt::UserRole).toString() == id) {
             m_viewerList->takeItem(i);
             delete item;
             continue;
        }

        // Fallback: Check label text if UserRole is missing (legacy items)
        QWidget* w = m_viewerList->itemWidget(item);
        if(w) {
             QList<QLabel*> labels = w->findChildren<QLabel*>();
             for(auto label : labels) {
                 // Check if text exactly matches ID (fallback)
                 if(label->text() == id) {
                     m_viewerList->takeItem(i);
                     delete item;
                     break;
                 }
             }
        }
    }
}

void NewUiWindow::clearViewers()
{
    if (!m_viewerList) return;
    
    m_viewerList->clear();
    m_viewerItems.clear();
    m_viewerMicButtons.clear();
    m_viewerMicStates.clear();
}

int NewUiWindow::getViewerCount() const
{
    if (!m_viewerList) return 0;
    return m_viewerItems.size();
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
        originalPixmap = s->grabWindow(0);
        if (!originalPixmap.isNull()) {
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
        bool allowSend = forceSend;
        if (!allowSend) {
            // Check if this specific client connection is LAN
            if (m_streamClient->isConnectedToLan()) {
                allowSend = true;
            } else {
                // Cloud: Rate limit to 60s to prevent traffic leakage
                if (nowMs - m_lastCloudSlowFrameSentAtMs >= 60000) {
                    allowSend = true;
                }
            }
        }
        
        if (allowSend) {
            m_streamClient->sendFrame(m_lastPreviewSendPixmap, forceSend);
            sentAny = true;
            if (!m_streamClient->isConnectedToLan() && !forceSend) {
                m_lastCloudSlowFrameSentAtMs = nowMs;
            }
        }
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
        originalPixmap = s->grabWindow(0);
        if (!originalPixmap.isNull()) {
            break;
        }
    }
    if (originalPixmap.isNull()) {
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
    if (!m_myStreamId.isEmpty()) {
        return QStringLiteral("hfps_%1_%2").arg(targetUserId, m_myStreamId);
    }
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
            const QStringList bases = AppConfig::localLanBaseUrls();
            const QString base = bases.value(0);
            if (!base.isEmpty()) {
                QUrl u(base);
                u.setPath(QStringLiteral("/publish/%1").arg(channelId));
                pubLan->connectToServer(u);
            }
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
    if (userId.isEmpty() || userId == m_myStreamId) {
        return;
    }
    m_autoPausedUserId = userId;
    if (QLabel *ov = m_reselectOverlays.value(userId, nullptr)) {
        ov->setVisible(true);
        ov->raise();
    }
    if (userId == m_hiFpsActiveUserId) {
        stopHiFpsForUser();
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
    if (userId == m_myStreamId) return; 
    if (m_userItems.contains(userId)) return; 

    QString appDir = QCoreApplication::applicationDirPath();
    const QString displayName = userName.isEmpty() ? userId : userName;

    // Create List Item
    QListWidgetItem *item = new QListWidgetItem(m_listWidget);
    item->setSizeHint(QSize(m_totalItemWidth, m_totalItemHeight));
    item->setData(Qt::UserRole, userId); 

    // Create Widget
    QWidget *itemWidget = new QWidget();
    itemWidget->setAttribute(Qt::WA_TranslucentBackground);
    QVBoxLayout *itemLayout = new QVBoxLayout(itemWidget);
    itemLayout->setContentsMargins(m_shadowSize, m_shadowSize, m_shadowSize, m_shadowSize);
    itemLayout->setSpacing(0);

    // Card Frame
    QFrame *card = new QFrame();
    card->setObjectName("CardFrame");
    // [Interaction Fix] Install event filter to capture double clicks on the card
    card->setProperty("userId", userId);
    card->installEventFilter(this);

    card->setStyleSheet(
        "#CardFrame {"
        "   background-color: #3b3b3b;"
        "   border-radius: 12px;"
        "}"
        "#CardFrame:hover {"
        "   background-color: #444;"
        "}"
    );
    
    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect();
    shadow->setBlurRadius(10); 
    shadow->setColor(QColor(0, 0, 0, 80));
    shadow->setOffset(0, 2);
    card->setGraphicsEffect(shadow);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(m_marginX, m_marginTop, m_marginX, 0);
    cardLayout->setSpacing(0); 

    // Image Label
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
    talkOverlay->setText(QStringLiteral("通话中"));
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

    // Bottom Controls
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(0, 0, 0, 5); 
    bottomLayout->setSpacing(5);

    // Watch Button
    QPushButton *tabBtn = nullptr;
    if (userId != m_myStreamId) {
        tabBtn = new QPushButton();
        tabBtn->setFixedSize(14, 14);
        tabBtn->setCursor(Qt::PointingHandCursor);
        tabBtn->setFlat(true);
        tabBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
        tabBtn->setIcon(QIcon(appDir + "/maps/logo/in.png"));
        tabBtn->setIconSize(QSize(14, 14));

        connect(tabBtn, &QPushButton::clicked, this, [this, userId]() {
            emit startWatchingRequested(userId);
        });
    }

    // Name Label
    QLabel *txtLabel = new QLabel(displayName);
    txtLabel->setStyleSheet("color: #e0e0e0; font-size: 12px; border: none; background: transparent;");
    txtLabel->setAlignment(Qt::AlignCenter);

    // Mic/Speaker Buttons
    QPushButton *micBtn = nullptr;
    if (userId != m_myStreamId) {
        micBtn = new QPushButton();
        micBtn->setFixedSize(14, 14);
        micBtn->setCursor(Qt::PointingHandCursor);
        micBtn->setProperty("isOn", false);
        micBtn->setProperty("remoteActive", false);
        micBtn->setFlat(true);
        micBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
        micBtn->setIcon(QIcon(appDir + "/maps/logo/end.png"));
        micBtn->setIconSize(QSize(14, 14));

        m_talkButtons.insert(userId, micBtn);
        connect(micBtn, &QPushButton::clicked, [this, micBtn, appDir, userId]() {
            const bool remoteActive = micBtn->property("remoteActive").toBool();
            bool isOn = micBtn->property("isOn").toBool();
            if (remoteActive) {
                setTalkRemoteActive(userId, false);
                if (userId != m_myStreamId) {
                    setTalkConnected(userId, false);
                    emit kickViewerRequested(userId);
                    return;
                }
            }
            isOn = !isOn;
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

    // Track Item
    m_userItems.insert(userId, item);
    m_userLabels.insert(userId, imgLabel);
    m_userAvatarLabels.insert(userId, avatarLabel);

    // Start Stream Subscription
    StreamClient *client = new StreamClient(this);
    const QString previewChannelId = QStringLiteral("preview_%1").arg(userId);
    QString subscribeUrl = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), previewChannelId);
    
    client->setProperty("firstFrameReceived", false);
    connect(client, &StreamClient::frameReceived, this, [this, userId, client](const QPixmap &frame) {
        client->setProperty("firstFrameReceived", true);
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
