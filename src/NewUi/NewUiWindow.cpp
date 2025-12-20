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
#include <QDesktopServices>
#include <QHelpEvent>
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
#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QBuffer>
#include <QImage>

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

class FancyToolTipWidget : public QWidget {
public:
    FancyToolTipWidget()
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setWindowFlag(Qt::ToolTip, true);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(kShadowPad + 14, kShadowPad + 10, kShadowPad + 14, kShadowPad + 10);
        layout->setSpacing(0);

        m_label = new QLabel(this);
        m_label->setStyleSheet("color: #f4f4f4; background: transparent; font-size: 12px;");
        m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        layout->addWidget(m_label);
    }

    void setText(const QString &text)
    {
        m_label->setText(text);
        m_label->adjustSize();
        adjustSize();
    }

    void showAt(const QPoint &globalPos)
    {
        adjustSize();
        const QPoint offset(16, 24);
        QPoint pos = globalPos + offset;
        if (QScreen *screen = QGuiApplication::screenAt(globalPos)) {
            const QRect avail = screen->availableGeometry();
            const int maxX = avail.right() - width() + 1;
            const int maxY = avail.bottom() - height() + 1;
            pos.setX(qBound(avail.left(), pos.x(), maxX));
            pos.setY(qBound(avail.top(), pos.y(), maxY));
        }
        move(pos);
        show();
        raise();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF bubbleRect = QRectF(rect()).adjusted(kShadowPad, kShadowPad, -kShadowPad, -kShadowPad);
        const qreal radius = 12.0;

        QPainterPath bubblePath;
        bubblePath.addRoundedRect(bubbleRect, radius, radius);

        for (int i = kShadowPad; i >= 1; --i) {
            const qreal t = static_cast<qreal>(i) / static_cast<qreal>(kShadowPad);
            const int a = qBound(0, static_cast<int>(110 * t * t), 255);
            QPainterPath outer;
            outer.addRoundedRect(bubbleRect.adjusted(-i, -i, i, i), radius + i, radius + i);
            p.fillPath(outer.subtracted(bubblePath), QColor(0, 0, 0, a));
        }

        QLinearGradient g(bubbleRect.topLeft(), bubbleRect.bottomLeft());
        g.setColorAt(0.0, QColor(70, 70, 70, 235));
        g.setColorAt(1.0, QColor(35, 35, 35, 235));

        p.fillPath(bubblePath, g);
        p.setPen(QPen(QColor(255, 255, 255, 35), 1));
        p.drawPath(bubblePath);
    }

private:
    static constexpr int kShadowPad = 16;
    QLabel *m_label = nullptr;
};

static FancyToolTipWidget *ensureFancyToolTip()
{
    static QPointer<FancyToolTipWidget> tip;
    if (!tip) {
        tip = new FancyToolTipWidget();
        tip->hide();
    }
    return tip;
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

    setWindowFlags(Qt::FramelessWindowHint | Qt::Window | Qt::WindowStaysOnTopHint | Qt::WindowSystemMenuHint | Qt::WindowMinimizeButtonHint);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(m_totalItemWidth + 20, 800); // Adjust width to fit cards, height arbitrary for now
    
    setupUi();

    // Timer for screenshot
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &NewUiWindow::onTimerTimeout);
    m_timer->start(500); // 0.5 seconds

    m_talkSpinnerTimer = new QTimer(this);
    connect(m_talkSpinnerTimer, &QTimer::timeout, this, &NewUiWindow::onTalkSpinnerTimeout);

    // Setup StreamClient
    m_streamClient = new StreamClient(this);
    connect(m_streamClient, &StreamClient::logMessage, this, &NewUiWindow::onStreamLog);

    m_avatarPublisher = new StreamClient(this);
    connect(m_avatarPublisher, &StreamClient::connected, this, &NewUiWindow::publishLocalAvatarOnce);

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

    // 1. Connect Stream Client (Push)
    QString serverUrl = QString("ws://123.207.222.92:8765/publish/%1").arg(m_myStreamId);
    
    if (m_streamClient) {
        m_streamClient->disconnectFromServer();
        m_streamClient->connectToServer(QUrl(serverUrl));
    }

    if (!m_myStreamId.isEmpty()) {
        ensureAvatarCacheDir();
        const QString cacheFile = avatarCacheFilePath(m_myStreamId);
        if (!QFileInfo::exists(cacheFile)) {
            const QString appDir = QCoreApplication::applicationDirPath();
            const QString headPath = QDir(appDir).filePath("maps/logo/head.jpg");
            QPixmap src(headPath);
            if (src.isNull()) {
                src = QPixmap(128, 128);
                src.fill(QColor(90, 90, 90));
            }
            QPixmap savePix = src.scaled(128, 128, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
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
            m_avatarPublisher->disconnectFromServer();
            const QString channelId = QString("avatar_%1").arg(m_myStreamId);
            const QString pubUrl = QString("ws://123.207.222.92:8765/publish/%1").arg(channelId);
            m_avatarPublisher->connectToServer(QUrl(pubUrl));
        }

        ensureAvatarSubscription(m_myStreamId);
        refreshLocalAvatarFromCache();
    }

    // 2. Connect Login Client
    // DISABLED: Main Window controls login.
    /*
    QString loginUrl = "ws://123.207.222.92:8765/login";
    if (m_loginClient) {
        m_loginClient->disconnectFromServer();
        m_loginClient->connectToServer(QUrl(loginUrl));
    }
    */
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
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString candidate1 = QDir(appDir).filePath("maps/logo/head.jpg");
    const QString candidate2 = QDir::current().filePath("src/maps/logo/head.jpg");
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
        return;
    }
    if (m_localAvatarPublishPixmap.isNull()) {
        return;
    }
    m_avatarPublisher->sendFrame(m_localAvatarPublishPixmap, true);
}

void NewUiWindow::publishLocalAvatarHint()
{
    if (!m_avatarPublisher || !m_avatarPublisher->isConnected()) {
        return;
    }
    if (m_localAvatarPublishPixmap.isNull()) {
        return;
    }
    m_avatarPublisher->sendFrame(m_localAvatarPublishPixmap, false);
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
    const QString subscribeUrl = QString("ws://123.207.222.92:8765/subscribe/%1").arg(channelId);
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
}

void NewUiWindow::setTalkConnected(const QString &userId, bool connected)
{
    QPushButton *btn = m_talkButtons.value(userId, nullptr);
    if (!btn) {
        return;
    }

    btn->setProperty("isOn", connected);
    setTalkPending(userId, false);
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
            // Already exists, maybe update name?
            // For now, do nothing or update title label if we tracked it
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
        imgLabel->setFixedSize(m_imgWidth, m_imgHeight); 
        imgLabel->setAlignment(Qt::AlignCenter);
        imgLabel->setText("Loading Stream...");
        imgLabel->setStyleSheet("color: #888; font-size: 10px;");

        // Bottom Controls Layout
        QHBoxLayout *bottomLayout = new QHBoxLayout();
        // Zero side margins because parent cardLayout already provides MARGIN_X
        // But we might want buttons to extend a bit wider? No, keep alignment.
        // Add a bit of bottom padding
        bottomLayout->setContentsMargins(0, 0, 0, 5); 
        bottomLayout->setSpacing(5);

        // Left Button (tab1.png)
        QPushButton *tabBtn = new QPushButton();
        tabBtn->setFixedSize(14, 14);
        tabBtn->setCursor(Qt::PointingHandCursor);
        tabBtn->setFlat(true);
        tabBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
        tabBtn->setIcon(QIcon(appDir + "/maps/logo/in.png"));
        tabBtn->setIconSize(QSize(14, 14));
        
        connect(tabBtn, &QPushButton::clicked, this, [this, id]() {
            emit startWatchingRequested(id);
        });
        
        // Text Label (Middle)
        QLabel *txtLabel = new QLabel(name.isEmpty() ? id : name);
        txtLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        txtLabel->setStyleSheet("color: #e0e0e0; font-size: 12px; border: none; background: transparent;");
        txtLabel->setAlignment(Qt::AlignCenter);

        // Mic Toggle
        QPushButton *micBtn = new QPushButton();
        micBtn->setFixedSize(14, 14);
        micBtn->setCursor(Qt::PointingHandCursor);
        micBtn->setProperty("isOn", false);
        micBtn->setFlat(true);
        micBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
        micBtn->setIcon(QIcon(appDir + "/maps/logo/end.png"));
        micBtn->setIconSize(QSize(14, 14));

        m_talkButtons.insert(id, micBtn);
        connect(micBtn, &QPushButton::clicked, [this, micBtn, appDir, id]() {
            bool isOn = micBtn->property("isOn").toBool();
            isOn = !isOn;
            micBtn->setProperty("isOn", isOn);
            if (id == m_myStreamId) {
                QString iconName = isOn ? "get.png" : "end.png";
                micBtn->setIcon(QIcon(appDir + "/maps/logo/" + iconName));
                return;
            }
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

        bottomLayout->addWidget(tabBtn);
        bottomLayout->addWidget(txtLabel);
        bottomLayout->addWidget(micBtn);

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
        QString subUrl = QString("ws://123.207.222.92:8765/subscribe/%1").arg(id);
        client->connectToServer(QUrl(subUrl));
    }
}

void NewUiWindow::onStreamLog(const QString &msg)
{
    Q_UNUSED(msg);
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

    // Add vertical buttons to left panel
    for (int i = 0; i < 4; ++i) {
        QPushButton *btn = new QPushButton();
        btn->setFixedSize(40, 40);
        btn->setCursor(Qt::PointingHandCursor);
        if (i == 0) btn->setToolTip("主页");
        else if (i == 1) btn->setToolTip("功能 1");
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
            connect(btn, &QPushButton::clicked, [btn]() {
                QSequentialAnimationGroup *group = new QSequentialAnimationGroup(btn);
                
                // Step 1: Scale down slightly
                QPropertyAnimation *anim1 = new QPropertyAnimation(btn, "iconSize");
                anim1->setDuration(100);
                anim1->setStartValue(QSize(28, 28));
                anim1->setEndValue(QSize(22, 22));
                anim1->setEasingCurve(QEasingCurve::OutQuad);
                
                // Step 2: Scale up larger than normal
                QPropertyAnimation *anim2 = new QPropertyAnimation(btn, "iconSize");
                anim2->setDuration(100);
                anim2->setStartValue(QSize(22, 22));
                anim2->setEndValue(QSize(34, 34));
                anim2->setEasingCurve(QEasingCurve::OutQuad);
                
                // Step 3: Restore to normal
                QPropertyAnimation *anim3 = new QPropertyAnimation(btn, "iconSize");
                anim3->setDuration(100);
                anim3->setStartValue(QSize(34, 34));
                anim3->setEndValue(QSize(28, 28));
                anim3->setEasingCurve(QEasingCurve::OutElastic); // Elastic for a bouncy finish

                group->addAnimation(anim1);
                group->addAnimation(anim2);
                group->addAnimation(anim3);
                
                // Clean up animation object after finish
                connect(group, &QAbstractAnimation::finished, group, &QObject::deleteLater);
                
                group->start();
            });
        }
        
        leftLayout->addWidget(btn);
    }
    
    leftLayout->addStretch();
    
    // Bottom setting button
    QPushButton *settingBtn = new QPushButton();
    settingBtn->setFixedSize(40, 40);
    settingBtn->setCursor(Qt::PointingHandCursor);
    settingBtn->setToolTip("设置");
    settingBtn->installEventFilter(this);
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
    
    QHBoxLayout *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    
    // Title buttons (Menu, Min, Close) with background
    
    // --- New Tool Buttons Group (Left side of title bar) ---
    // Use QFrame to ensure background styling works without custom paintEvent
    QFrame *toolsContainer = new QFrame(titleBar);
    toolsContainer->setObjectName("ToolsContainer");
    toolsContainer->setFixedSize(160, 40);
    toolsContainer->setFrameShape(QFrame::NoFrame);
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
    // Container width: 48*4 = 192. Height: 48.
    controlContainer->setFixedSize(192, 48); 
    // Important: Ensure the widget itself doesn't paint a background, only the stylesheet image
    controlContainer->setAttribute(Qt::WA_TranslucentBackground);
    controlContainer->setObjectName("TitleControlContainer");
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

    // Menu Button
    ResponsiveButton *menuBtn = new ResponsiveButton();
    menuBtn->setFixedSize(48, 48); 
    menuBtn->setIcon(QIcon(appDir + "/maps/logo/menu.png"));
    menuBtn->setIconSize(QSize(32, 32)); 
    menuBtn->setCursor(Qt::PointingHandCursor);
    menuBtn->setToolTip("系统设置");
    menuBtn->installEventFilter(this);
    connect(menuBtn, &QPushButton::clicked, this, &NewUiWindow::systemSettingsRequested);

    // Minimize Button
    ResponsiveButton *minBtn = new ResponsiveButton();
    minBtn->setFixedSize(48, 48); 
    minBtn->setIcon(QIcon(appDir + "/maps/logo/mini.png"));
    minBtn->setIconSize(QSize(32, 32)); 
    minBtn->setCursor(Qt::PointingHandCursor);
    minBtn->setToolTip("最小化");
    minBtn->installEventFilter(this);
    connect(minBtn, &QPushButton::clicked, this, &QWidget::showMinimized);

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
    controlLayout->addWidget(closeBtn);

    titleLayout->addWidget(controlContainer);
    titleLayout->addSpacing(20);

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

        // Left Button (tab1.png)
        QPushButton *tabBtn = new QPushButton();
        tabBtn->setFixedSize(14, 14);
        tabBtn->setCursor(Qt::PointingHandCursor);
        tabBtn->setFlat(true);
        tabBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
        tabBtn->setIcon(QIcon(appDir + "/maps/logo/in.png"));
        tabBtn->setIconSize(QSize(14, 14));
        
        // [Interaction Fix] Connect local tab button for testing
        connect(tabBtn, &QPushButton::clicked, this, [this]() {
             if (!m_myStreamId.isEmpty()) {
                 emit startWatchingRequested(m_myStreamId);
             }
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

        // Mic Toggle
        QPushButton *micBtn = new QPushButton();
        micBtn->setFixedSize(14, 14);
        micBtn->setCursor(Qt::PointingHandCursor);
        micBtn->setProperty("isOn", false);
        micBtn->setFlat(true);
        micBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
        micBtn->setIcon(QIcon(appDir + "/maps/logo/end.png"));
        micBtn->setIconSize(QSize(14, 14));

        connect(micBtn, &QPushButton::clicked, [micBtn, appDir]() {
            bool isOn = micBtn->property("isOn").toBool();
            isOn = !isOn;
            micBtn->setProperty("isOn", isOn);
            QString iconName = isOn ? "get.png" : "end.png";
            micBtn->setIcon(QIcon(appDir + "/maps/logo/" + iconName));
        });

        bottomLayout->addWidget(tabBtn);
        bottomLayout->addWidget(txtLabel);
        bottomLayout->addWidget(micBtn);

        cardLayout->addWidget(imageContainer);
        cardLayout->addLayout(bottomLayout);

        itemLayout->addWidget(card);
        
        m_listWidget->setItemWidget(item, itemWidget);
    }

    listContainerLayout->addWidget(m_listWidget);
    rightLayout->addWidget(titleBar);
    rightLayout->addWidget(listContainer);

    // Connect selection change to update styles
    connect(m_listWidget, &QListWidget::itemSelectionChanged, [this]() {
        for(int i = 0; i < m_listWidget->count(); ++i) {
            QListWidgetItem *item = m_listWidget->item(i);
            QWidget *w = m_listWidget->itemWidget(item);
            if (w) {
                QFrame *card = w->findChild<QFrame*>("CardFrame");
                if (card) {
                    if (item->isSelected()) {
                        // Tech Orange Selection Style
                        card->setStyleSheet(
                            "#CardFrame {"
                            "   background-color: rgba(255, 102, 0, 40);" // Semi-transparent orange tint
                            "   border: 1px solid #FF6600;" // Tech Orange border, 1px
                            "   border-radius: 15px;"
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
    mainLayout->addWidget(m_farRightPanel);
}

void NewUiWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        // Use globalPosition() for Qt6
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void NewUiWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton && m_dragging) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void NewUiWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    }
}

bool NewUiWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Enter || event->type() == QEvent::HoverEnter) {
        auto *w = qobject_cast<QWidget*>(watched);
        FancyToolTipWidget *tip = ensureFancyToolTip();
        if (w && tip) {
            const QString text = w->toolTip();
            if (!text.isEmpty()) {
                tip->setText(text);
                tip->showAt(QCursor::pos());
            }
        }
    }

    if (event->type() == QEvent::ToolTip) {
        auto *w = qobject_cast<QWidget*>(watched);
        FancyToolTipWidget *tip = ensureFancyToolTip();
        if (w && tip) {
            const QString text = w->toolTip();
            if (!text.isEmpty()) {
                tip->setText(text);
                QPoint gp = QCursor::pos();
                if (auto *he = dynamic_cast<QHelpEvent*>(event)) {
                    gp = he->globalPos();
                }
                tip->showAt(gp);
            } else {
                tip->hide();
            }
        }
        return true;
    }

    if (event->type() == QEvent::Leave ||
        event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseButtonRelease ||
        event->type() == QEvent::Wheel ||
        event->type() == QEvent::FocusOut) {
        if (auto *tip = ensureFancyToolTip()) {
            tip->hide();
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

    // Handle single click on local card to toggle "My Room" panel
    if (watched == m_localCard && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() != Qt::LeftButton) {
            return false;
        }
        if (m_farRightPanel) {
            m_farRightPanel->setVisible(!m_farRightPanel->isVisible());
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
    mic->setCursor(Qt::PointingHandCursor);
    // [UI Only] Default mic state to ON as requested
    mic->setIcon(QIcon(appDir + "/maps/logo/Mic_on.png"));
    mic->setIconSize(QSize(18, 18));
    mic->setFlat(true);
    mic->setStyleSheet("border: none; background: transparent;");
    mic->setToolTip("麦克风");
    mic->installEventFilter(this);
    
    // Simple toggle logic for this mic (UI Only)
    mic->setProperty("isOn", true);
    connect(mic, &QPushButton::clicked, [mic, appDir]() {
        bool isOn = mic->property("isOn").toBool();
        isOn = !isOn;
        mic->setProperty("isOn", isOn);
        QString iconName = isOn ? "Mic_on.png" : "Mic_off.png";
        mic->setIcon(QIcon(appDir + "/maps/logo/" + iconName));
    });
    
    l->addWidget(txt);
    l->addStretch();
    l->addWidget(removeBtn);
    l->addWidget(mic);
    
    m_viewerList->setItemWidget(item, w);
    m_viewerItems.insert(id, item);
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
}

int NewUiWindow::getViewerCount() const
{
    if (!m_viewerList) return 0;
    return m_viewerItems.size();
}



void NewUiWindow::onTimerTimeout()
{
    // Ensure we have a target label to update
    if (!m_videoLabel) return;

    // 1. Capture Screen
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    QPixmap originalPixmap = screen->grabWindow(0);
    
    // 2. Prepare Image (Scale to 300px width as requested)
    // Use m_cardBaseWidth to ensure we capture enough resolution for the UI
    QPixmap srcPix = originalPixmap.scaledToWidth(m_cardBaseWidth, Qt::SmoothTransformation);

    // 3. Process Image (Rounded Corners) - Reusing logic from setupUi for consistency
    QPixmap pixmap(m_imgWidth, m_imgHeight); 
    pixmap.fill(Qt::transparent);
    
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    
    // Scale to fit the label size (m_imgWidth x m_imgHeight)
    QPixmap scaledPix = srcPix.scaled(m_imgWidth, m_imgHeight, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    
    // Center crop calculation
    int x = (m_imgWidth - scaledPix.width()) / 2;
    int y = (m_imgHeight - scaledPix.height()) / 2;
    
    QPainterPath path;
    path.addRoundedRect(0, 0, m_imgWidth, m_imgHeight, 8, 8);
    p.setClipPath(path);
    
    p.drawPixmap(x, y, scaledPix);
    p.end();

    // 4. Update the label directly (Video effect)
    QPixmap previewPix = pixmap;
    QPixmap sendPix = pixmap;
    if (sendPix.width() > 250) {
        sendPix = sendPix.scaledToWidth(250, Qt::SmoothTransformation);
    }
    {
        const int previewJpegQuality = 30;
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (sendPix.save(&buffer, "JPG", previewJpegQuality)) {
            QImage decoded;
            if (decoded.loadFromData(bytes, "JPG") && !decoded.isNull()) {
                QPixmap decodedPix = QPixmap::fromImage(decoded);
                QPixmap scaledDecoded = decodedPix.scaled(m_imgWidth, m_imgHeight, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                QPixmap finalPreview(m_imgWidth, m_imgHeight);
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
        }
    }
    m_videoLabel->setPixmap(previewPix);

    // 5. Send frame to server
    if (m_streamClient && m_streamClient->isConnected()) {
        m_streamClient->sendFrame(sendPix);
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
    imageContainer->setFixedSize(m_imgWidth, m_imgHeight);
    imgLabel->setParent(imageContainer);
    imgLabel->move(0, 0);

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
    QPixmap avatarPix = buildTestAvatarPixmap(30);
    if (!avatarPix.isNull()) {
        avatarLabel->setPixmap(avatarPix);
    }

    // Bottom Controls
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(0, 0, 0, 5); 
    bottomLayout->setSpacing(5);

    // Watch Button
    QPushButton *tabBtn = new QPushButton();
    tabBtn->setFixedSize(14, 14);
    tabBtn->setCursor(Qt::PointingHandCursor);
    tabBtn->setFlat(true);
    tabBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
    tabBtn->setIcon(QIcon(appDir + "/maps/logo/in.png"));
    tabBtn->setIconSize(QSize(14, 14));
    
    connect(tabBtn, &QPushButton::clicked, this, [this, userId]() {
        emit startWatchingRequested(userId);
    });

    // Name Label
    QLabel *txtLabel = new QLabel(userName.isEmpty() ? userId : userName);
    txtLabel->setStyleSheet("color: #e0e0e0; font-size: 12px; border: none; background: transparent;");
    txtLabel->setAlignment(Qt::AlignCenter);

    // Mic/Speaker Buttons
    QPushButton *micBtn = new QPushButton();
    micBtn->setFixedSize(14, 14);
    micBtn->setCursor(Qt::PointingHandCursor);
    micBtn->setProperty("isOn", false);
    micBtn->setFlat(true);
    micBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
    micBtn->setIcon(QIcon(appDir + "/maps/logo/end.png"));
    micBtn->setIconSize(QSize(14, 14));

    m_talkButtons.insert(userId, micBtn);
    connect(micBtn, &QPushButton::clicked, [this, micBtn, appDir, userId]() {
        bool isOn = micBtn->property("isOn").toBool();
        isOn = !isOn;
        micBtn->setProperty("isOn", isOn);
        if (userId == m_myStreamId) {
            QString iconName = isOn ? "get.png" : "end.png";
            micBtn->setIcon(QIcon(appDir + "/maps/logo/" + iconName));
            return;
        }
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

    bottomLayout->addWidget(tabBtn);
    bottomLayout->addWidget(txtLabel);
    bottomLayout->addWidget(micBtn);

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
    QString subscribeUrl = QString("ws://123.207.222.92:8765/subscribe/%1").arg(userId);
    
    connect(client, &StreamClient::frameReceived, this, [this, userId](const QPixmap &frame) {
        if (m_userLabels.contains(userId)) {
             QLabel *label = m_userLabels[userId];
             
             QPixmap pixmap(m_imgWidth, m_imgHeight); 
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
        }
    });

    client->connectToServer(QUrl(subscribeUrl));
    m_remoteStreams.insert(userId, client);

    ensureAvatarSubscription(userId);
    publishLocalAvatarHint();
}

void NewUiWindow::removeUser(const QString &userId)
{
    m_talkButtons.remove(userId);

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

void NewUiWindow::clearUserList()
{
    QList<QString> keys = m_remoteStreams.keys();
    for (const QString &id : keys) {
        removeUser(id);
    }
}

void NewUiWindow::updateUserAvatar(const QString &userId, int iconId)
{
    Q_UNUSED(iconId);

    QLabel *label = m_userAvatarLabels.value(userId, nullptr);
    if (!label) {
        return;
    }

    const int s = qMin(label->width(), label->height());
    QPixmap avatarPix = buildTestAvatarPixmap(s);
    if (!avatarPix.isNull()) {
        label->setPixmap(avatarPix);
    }
}

QString NewUiWindow::getCurrentUserId() const
{
    return m_myStreamId;
}
