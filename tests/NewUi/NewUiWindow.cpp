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

NewUiWindow::NewUiWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(1000, 600);
    
    setupUi();
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
    rightLayout->setContentsMargins(20, 10, 20, 20);
    rightLayout->setSpacing(10);

    // Title Bar Area
    QWidget *titleBar = new QWidget(rightPanel);
    titleBar->setFixedHeight(50); // Increase height to accommodate larger buttons
    titleBar->setStyleSheet("background-color: transparent;");
    
    QHBoxLayout *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    
    // Title buttons (Menu, Min, Close) with background
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
    QPushButton *menuBtn = new QPushButton();
    menuBtn->setFixedSize(48, 48); 
    menuBtn->setIcon(QIcon(appDir + "/maps/logo/menu.png"));
    menuBtn->setIconSize(QSize(32, 32)); 
    menuBtn->setCursor(Qt::PointingHandCursor);

    // Minimize Button
    QPushButton *minBtn = new QPushButton();
    minBtn->setFixedSize(48, 48); 
    minBtn->setIcon(QIcon(appDir + "/maps/logo/mini.png"));
    minBtn->setIconSize(QSize(32, 32)); 
    minBtn->setCursor(Qt::PointingHandCursor);
    connect(minBtn, &QPushButton::clicked, this, &QWidget::showMinimized);

    // Close Button
    QPushButton *closeBtn = new QPushButton();
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
    QListWidget *listWidget = new QListWidget(rightPanel);
    listWidget->setViewMode(QListWidget::IconMode);
    listWidget->setIconSize(QSize(220, 140));
    listWidget->setSpacing(15);
    listWidget->setResizeMode(QListWidget::Adjust);
    listWidget->setMovement(QListWidget::Static);
    // Remove default border and background to blend in
    listWidget->setFrameShape(QFrame::NoFrame);
    listWidget->setStyleSheet(
        "QListWidget {"
        "   background-color: transparent;"
        "   outline: none;"
        "}"
        "QListWidget::item {"
        "   background-color: #3b3b3b;"
        "   border-radius: 15px;"
        "   border: none;"
        "   color: #e0e0e0;"
        "   padding: 0px;" 
        "}"
        "QListWidget::item:selected {"
        "   background-color: #505050;"
        "   border: 2px solid #1890ff;"
        "}"
        "QListWidget::item:hover {"
        "   background-color: #444;"
        "}"
    );
    
    // Vertical ScrollBar Styling
    listWidget->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical {"
        "    border: none;"
        "    background: #2b2b2b;"
        "    width: 6px;"
        "    margin: 0px 0px 0px 0px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: #555;"
        "    min-height: 20px;"
        "    border-radius: 3px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "    background: #666;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "    border: none;"
        "    background: none;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "    background: none;"
        "}"
    );

    // Add dummy items
    QString imgPath = appDir + "/maps/t.png";
    QPixmap srcPix(imgPath);
    // If loading fails, create a fallback
    if (srcPix.isNull()) {
        srcPix = QPixmap(220, 120);
        srcPix.fill(Qt::darkGray);
    }

    for (int i = 0; i < 12; ++i) {
        QListWidgetItem *item = new QListWidgetItem();
        item->setText(QString("Card Item %1").arg(i + 1));
        item->setTextAlignment(Qt::AlignBottom | Qt::AlignHCenter);
        
        // Create target pixmap
        QPixmap pixmap(220, 120);
        pixmap.fill(Qt::transparent);
        
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        
        // Rounded path matching item style
        QPainterPath path;
        path.addRoundedRect(0, 0, 220, 120, 15, 15);
        p.setClipPath(path);
        
        // Scale image to fill the rect (KeepAspectRatioByExpanding)
        // This ensures no empty borders, but cuts off excess.
        // User asked to "not cut too much", so we center it.
        QPixmap scaledPix = srcPix.scaled(220, 120, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        
        // Center the image
        int x = (220 - scaledPix.width()) / 2;
        int y = (120 - scaledPix.height()) / 2;
        
        p.drawPixmap(x, y, scaledPix);
        p.end();
        
        item->setIcon(QIcon(pixmap));
        listWidget->addItem(item);
    }

    rightLayout->addWidget(titleBar);
    rightLayout->addWidget(listWidget);

    // Assemble Main Layout
    mainLayout->addWidget(leftPanel);
    mainLayout->addWidget(rightPanel);
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
