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

NewUiWindow::NewUiWindow(QWidget *parent)
    : QWidget(parent)
{
    // --- GLOBAL SIZE CONTROL (ONE VALUE TO RULE THEM ALL) ---
    // [User Setting] 只要修改这个数值，所有尺寸自动计算
    m_cardBaseWidth = 240; // 卡片可见区域的宽度 (Restored to 220)
    
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

    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(1260, 600);
    
    setupUi();

    // Timer for screenshot
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &NewUiWindow::onTimerTimeout);
    m_timer->start(500); // 0.5 seconds

    // Setup StreamClient
    m_streamClient = new StreamClient(this);
    connect(m_streamClient, &StreamClient::logMessage, this, &NewUiWindow::onStreamLog);
    
    // Connect to server (Cloud or Local)
    // Using Cloud IP from project config: 123.207.222.92
    // Generate a random ID for this session to support multiple pushers
    quint32 randomId = QRandomGenerator::global()->bounded(10000, 99999);
    m_myStreamId = QString("user_%1").arg(randomId);
    m_myUserName = QString("User %1").arg(randomId);
    
    // 1. Connect Stream Client (Push)
    QString serverUrl = QString("ws://123.207.222.92:8765/publish/%1").arg(m_myStreamId);
    
    emit onStreamLog(QString("Generated Stream ID: %1").arg(m_myStreamId));
    m_streamClient->connectToServer(QUrl(serverUrl));

    // 2. Connect Login Client (User List & Discovery)
    m_loginClient = new LoginClient(this);
    connect(m_loginClient, &LoginClient::logMessage, this, &NewUiWindow::onStreamLog);
    connect(m_loginClient, &LoginClient::userListUpdated, this, &NewUiWindow::onUserListUpdated);
    connect(m_loginClient, &LoginClient::connected, this, &NewUiWindow::onLoginConnected);

    QString loginUrl = "ws://123.207.222.92:8765/login";
    m_loginClient->connectToServer(QUrl(loginUrl));
}

NewUiWindow::~NewUiWindow()
{
    if (m_timer && m_timer->isActive()) {
        m_timer->stop();
    }
}

void NewUiWindow::onLoginConnected()
{
    // Auto-login after connection
    m_loginClient->login(m_myStreamId, m_myUserName);
}

void NewUiWindow::onUserListUpdated(const QJsonArray &users)
{
    updateListWidget(users);
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
        
        // Text Label (Middle)
        QLabel *txtLabel = new QLabel(name.isEmpty() ? id : name);
        txtLabel->setStyleSheet("color: #e0e0e0; font-size: 12px; border: none; background: transparent;");
        txtLabel->setAlignment(Qt::AlignCenter);

        // Mic Toggle
        QPushButton *micBtn = new QPushButton();
        micBtn->setFixedSize(14, 14);
        micBtn->setCursor(Qt::PointingHandCursor);
        micBtn->setProperty("isOn", false);
        micBtn->setFlat(true);
        micBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
        micBtn->setIcon(QIcon(appDir + "/maps/logo/Mic_off.png"));
        micBtn->setIconSize(QSize(14, 14));

        connect(micBtn, &QPushButton::clicked, [micBtn, appDir]() {
            bool isOn = micBtn->property("isOn").toBool();
            isOn = !isOn;
            micBtn->setProperty("isOn", isOn);
            QString iconName = isOn ? "Mic_on.png" : "Mic_off.png";
            micBtn->setIcon(QIcon(appDir + "/maps/logo/" + iconName));
        });

        // Speaker Toggle
        QPushButton *labaBtn = new QPushButton();
        labaBtn->setFixedSize(14, 14);
        labaBtn->setCursor(Qt::PointingHandCursor);
        labaBtn->setProperty("isOn", false);
        labaBtn->setFlat(true);
        labaBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
        labaBtn->setIcon(QIcon(appDir + "/maps/logo/laba_off.png"));
        labaBtn->setIconSize(QSize(14, 14));

        connect(labaBtn, &QPushButton::clicked, [labaBtn, appDir]() {
            bool isOn = labaBtn->property("isOn").toBool();
            isOn = !isOn;
            labaBtn->setProperty("isOn", isOn);
            QString iconName = isOn ? "laba_on.png" : "laba_off.png";
            labaBtn->setIcon(QIcon(appDir + "/maps/logo/" + iconName));
        });

        bottomLayout->addWidget(tabBtn);
        bottomLayout->addWidget(txtLabel);
        bottomLayout->addWidget(micBtn);
        bottomLayout->addWidget(labaBtn);

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
        
        qDebug() << "Subscribing to user:" << id << " URL:" << subUrl;
    }
}

void NewUiWindow::onStreamLog(const QString &msg)
{
    qDebug() << "[StreamClient]" << msg;
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
    QLabel *logoLabel = new QLabel();
    logoLabel->setFixedSize(40, 40);
    logoLabel->setPixmap(QPixmap(appDir + "/maps/logo/iruler.ico").scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoLabel->setAlignment(Qt::AlignCenter);
    leftLayout->addWidget(logoLabel);
    
    // Spacing between logo and buttons
    leftLayout->addSpacing(20);

    // Add vertical buttons to left panel
    for (int i = 0; i < 4; ++i) {
        QPushButton *btn = new QPushButton();
        btn->setFixedSize(40, 40);
        
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
    toolsContainer->setFixedSize(160, 40); // Increased length from 140 to 160
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

    // d.png
    ResponsiveButton *toolBtn1 = new ResponsiveButton();
    toolBtn1->setFixedSize(40, 40); 
    toolBtn1->setIcon(QIcon(appDir + "/maps/logo/d.png"));
    toolBtn1->setIconSize(QSize(24, 24)); 
    toolBtn1->setCursor(Qt::PointingHandCursor);

    // log.png
    ResponsiveButton *toolBtn2 = new ResponsiveButton();
    toolBtn2->setFixedSize(40, 40); 
    toolBtn2->setIcon(QIcon(appDir + "/maps/logo/log.png"));
    toolBtn2->setIconSize(QSize(24, 24)); 
    toolBtn2->setCursor(Qt::PointingHandCursor);

    // clearn.png
    ResponsiveButton *toolBtn3 = new ResponsiveButton();
    toolBtn3->setFixedSize(40, 40); 
    toolBtn3->setIcon(QIcon(appDir + "/maps/logo/clearn.png"));
    toolBtn3->setIconSize(QSize(24, 24)); 
    toolBtn3->setCursor(Qt::PointingHandCursor);

    toolsLayout->addWidget(toolBtn1);
    toolsLayout->addWidget(toolBtn2);
    toolsLayout->addWidget(toolBtn3);

    titleLayout->addWidget(toolsContainer);
    titleLayout->addStretch();

    QWidget *controlContainer = new QWidget(titleBar);
    // Size adjustment:
    // Buttons: 48x48 (Double size)
    // Container width: 48*3 = 144. Height: 48.
    controlContainer->setFixedSize(144, 48); 
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

    // Menu Button
    ResponsiveButton *menuBtn = new ResponsiveButton();
    menuBtn->setFixedSize(48, 48); 
    menuBtn->setIcon(QIcon(appDir + "/maps/logo/menu.png"));
    menuBtn->setIconSize(QSize(32, 32)); 
    menuBtn->setCursor(Qt::PointingHandCursor);

    // Minimize Button
    ResponsiveButton *minBtn = new ResponsiveButton();
    minBtn->setFixedSize(48, 48); 
    minBtn->setIcon(QIcon(appDir + "/maps/logo/mini.png"));
    minBtn->setIconSize(QSize(32, 32)); 
    minBtn->setCursor(Qt::PointingHandCursor);
    connect(minBtn, &QPushButton::clicked, this, &QWidget::showMinimized);

    // Close Button
    ResponsiveButton *closeBtn = new ResponsiveButton();
    closeBtn->setFixedSize(48, 48); 
    closeBtn->setIcon(QIcon(appDir + "/maps/logo/close.png"));
    closeBtn->setIconSize(QSize(32, 32)); 
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

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


    // Add dummy items
    QString imgPath = appDir + "/maps/t.png";
    QPixmap srcPix(imgPath);
    // If loading fails, create a fallback
    if (srcPix.isNull()) {
        srcPix = QPixmap(m_imgWidth, m_imgHeight); // Use calculated size
        srcPix.fill(Qt::darkGray);
    }

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
        qDebug() << "[NewUiWindow] m_videoLabel assigned successfully. Widget pointer:" << m_videoLabel;
        
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
        
        // Text Label (Middle)
        QLabel *txtLabel = new QLabel(QString("Local Screen (Me)"));
        txtLabel->setStyleSheet("color: #e0e0e0; font-size: 12px; border: none; background: transparent;");
        txtLabel->setAlignment(Qt::AlignCenter);

        // Mic Toggle
        QPushButton *micBtn = new QPushButton();
        micBtn->setFixedSize(14, 14);
        micBtn->setCursor(Qt::PointingHandCursor);
        micBtn->setProperty("isOn", false);
        micBtn->setFlat(true);
        micBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
        micBtn->setIcon(QIcon(appDir + "/maps/logo/Mic_off.png"));
        micBtn->setIconSize(QSize(14, 14));

        connect(micBtn, &QPushButton::clicked, [micBtn, appDir]() {
            bool isOn = micBtn->property("isOn").toBool();
            isOn = !isOn;
            micBtn->setProperty("isOn", isOn);
            QString iconName = isOn ? "Mic_on.png" : "Mic_off.png";
            micBtn->setIcon(QIcon(appDir + "/maps/logo/" + iconName));
        });

        // Speaker Toggle
        QPushButton *labaBtn = new QPushButton();
        labaBtn->setFixedSize(14, 14);
        labaBtn->setCursor(Qt::PointingHandCursor);
        labaBtn->setProperty("isOn", false);
        labaBtn->setFlat(true);
        labaBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
        labaBtn->setIcon(QIcon(appDir + "/maps/logo/laba_off.png"));
        labaBtn->setIconSize(QSize(14, 14));

        connect(labaBtn, &QPushButton::clicked, [labaBtn, appDir]() {
            bool isOn = labaBtn->property("isOn").toBool();
            isOn = !isOn;
            labaBtn->setProperty("isOn", isOn);
            QString iconName = isOn ? "laba_on.png" : "laba_off.png";
            labaBtn->setIcon(QIcon(appDir + "/maps/logo/" + iconName));
        });

        bottomLayout->addWidget(tabBtn);
        bottomLayout->addWidget(txtLabel);
        bottomLayout->addWidget(micBtn);
        bottomLayout->addWidget(labaBtn);

        cardLayout->addWidget(imgLabel);
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
    QWidget *farRightPanel = new QWidget(this);
    farRightPanel->setObjectName("FarRightPanel");
    farRightPanel->setFixedWidth(240); // Wider than left panel (80px)
    farRightPanel->setStyleSheet(
        "QWidget#FarRightPanel {"
        "   background-color: #2b2b2b;"
        "   border-radius: 20px;"
        "}"
    );

    QVBoxLayout *farRightLayout = new QVBoxLayout(farRightPanel);
    farRightLayout->setContentsMargins(20, 20, 20, 20);
    farRightLayout->setSpacing(15);

    // Title
    QLabel *frTitle = new QLabel("龙哥房间"); 
    frTitle->setStyleSheet("color: white; font-size: 16px; font-weight: bold; background: transparent;");
    frTitle->setAlignment(Qt::AlignCenter);
    farRightLayout->addWidget(frTitle);

    // List
    QListWidget *frList = new QListWidget();
    frList->setFrameShape(QFrame::NoFrame);
    // Enable auto-adjust to prevent horizontal scrollbar issues
    frList->setResizeMode(QListWidget::Adjust); 
    frList->setStyleSheet(
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
    frList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    frList->verticalScrollBar()->setSingleStep(10); // Small scroll step
    frList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); 
    
    // Add items to Far Right List
    for (int i = 0; i < 15; ++i) {
        QListWidgetItem *item = new QListWidgetItem(frList);
        // Reduced width hint to ensure it fits within the panel even with scrollbar
        // Panel Width (240) - Panel Margin (40) - Scrollbar (approx 10) - Buffer = 180
        item->setSizeHint(QSize(180, 50));
        
        QWidget *w = new QWidget();
        w->setStyleSheet("background: transparent;");
        QHBoxLayout *l = new QHBoxLayout(w);
        // Standard margins
        l->setContentsMargins(10, 0, 10, 0); 
        
        QLabel *txt = new QLabel(QString("用户名 %1").arg(i+1));
        txt->setStyleSheet("color: #dddddd; font-size: 14px; border: none;");
        
        QPushButton *mic = new QPushButton();
        mic->setFixedSize(24, 24);
        mic->setCursor(Qt::PointingHandCursor);
        mic->setIcon(QIcon(appDir + "/maps/logo/Mic_off.png"));
        mic->setIconSize(QSize(18, 18));
        mic->setFlat(true);
        mic->setStyleSheet("border: none; background: transparent;");
        
        // Simple toggle logic for this mic
        mic->setProperty("isOn", false);
        connect(mic, &QPushButton::clicked, [mic, appDir]() {
            bool isOn = mic->property("isOn").toBool();
            isOn = !isOn;
            mic->setProperty("isOn", isOn);
            QString iconName = isOn ? "Mic_on.png" : "Mic_off.png";
            mic->setIcon(QIcon(appDir + "/maps/logo/" + iconName));
        });
        
        l->addWidget(txt);
        l->addStretch();
        l->addWidget(mic);
        // Removed explicit spacing at the end, relying on margins and width hint
        
        frList->setItemWidget(item, w);
    }
    
    farRightLayout->addWidget(frList);

    // Quit Button
    QPushButton *quitBtn = new QPushButton("退出");
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
    // Connect to application quit
    connect(quitBtn, &QPushButton::clicked, qApp, &QCoreApplication::quit);

    // Center the button horizontally
    farRightLayout->addWidget(quitBtn, 0, Qt::AlignHCenter);

    // Assemble Main Layout
    mainLayout->addWidget(leftPanel);
    mainLayout->addWidget(rightPanel);
    mainLayout->addWidget(farRightPanel);
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

void NewUiWindow::onTimerTimeout()
{
    // Ensure we have a target label to update
    if (!m_videoLabel) return;

    // 1. Capture Screen
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    QPixmap originalPixmap = screen->grabWindow(0);
    
    // 2. Prepare Image (Scale to 300px width as requested)
    QPixmap srcPix = originalPixmap.scaledToWidth(300, Qt::SmoothTransformation);

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
    m_videoLabel->setPixmap(pixmap);

    // 5. Send frame to server
    if (m_streamClient && m_streamClient->isConnected()) {
        // Send the smaller 300px image or the original?
        // Requirement said "0.5秒截图一次，300像素，，然后显示在列表的第一个"
        // It didn't explicitly say push 300px, but usually for streaming preview we push what we see.
        // Or we might want to push higher quality?
        // "实现单路图片的推流功能" - let's push the same image we show for now to save bandwidth.
        m_streamClient->sendFrame(pixmap);
    }
}
