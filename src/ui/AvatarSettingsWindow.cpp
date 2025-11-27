#include "AvatarSettingsWindow.h"
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <QDir>
#include <QPixmap>
#include <QCoreApplication>

AvatarSettingsWindow::AvatarSettingsWindow(QWidget *parent)
    : QWidget(parent)
    , m_dragging(false)
    , m_selectedIconId(-1)
    , m_selectedAvatarLabel(nullptr)
{
    setupUI();
    
    // 设置窗口属性
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground);
    
    // 设置固定窗口大小
    setFixedSize(WINDOW_WIDTH, WINDOW_HEIGHT);
    
    // 居中显示
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect screenGeometry = screen->availableGeometry();
        int x = (screenGeometry.width() - width()) / 2;
        int y = (screenGeometry.height() - height()) / 2;
        move(x, y);
    }
    
    // 加载头像图片
    loadAvatarImages();
}

AvatarSettingsWindow::~AvatarSettingsWindow()
{
}

void AvatarSettingsWindow::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
    setupCustomTitleBar();
    setupAvatarGrid();
}

void AvatarSettingsWindow::setupCustomTitleBar()
{
    // 创建标题栏 - 与VideoWindow相同的样式
    m_titleBar = new QWidget(this);
    m_titleBar->setFixedHeight(28);
    m_titleBar->setStyleSheet(
        "QWidget {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "                stop:0 #3a3a3a, stop:1 #2a2a2a);"
        "    border-top-left-radius: 6px;"
        "    border-top-right-radius: 6px;"
        "}"
    );
    
    m_titleBarLayout = new QHBoxLayout(m_titleBar);
    m_titleBarLayout->setContentsMargins(12, 0, 4, 0);
    m_titleBarLayout->setSpacing(0);
    
    // 标题标签
    m_titleLabel = new QLabel("设置头像", m_titleBar);
    m_titleLabel->setStyleSheet(
        "QLabel {"
        "    color: #ffffff;"
        "    font-size: 12px;"
        "    font-weight: 500;"
        "    background: transparent;"
        "}"
    );
    
    m_titleBarLayout->addWidget(m_titleLabel);
    m_titleBarLayout->addStretch();
    
    // 创建窗口控制按钮
    QString buttonStyle = 
        "QPushButton {"
        "    background-color: transparent;"
        "    border: none;"
        "    width: 24px;"
        "    height: 24px;"
        "    border-radius: 12px;"
        "    font-size: 14px;"
        "    font-weight: 500;"
        "    margin: 1px;"
        "}"
        "QPushButton:hover {"
        "    background-color: rgba(255, 255, 255, 0.15);"
        "}"
        "QPushButton:pressed {"
        "    background-color: rgba(255, 255, 255, 0.25);"
        "}";
    
    // 最小化按钮
    m_minimizeButton = new QPushButton("−", m_titleBar);
    m_minimizeButton->setStyleSheet(buttonStyle + 
        "QPushButton:hover { background-color: rgba(255, 193, 7, 0.7); }");
    connect(m_minimizeButton, &QPushButton::clicked, this, &AvatarSettingsWindow::onMinimizeClicked);
    
    // 关闭按钮
    m_closeButton = new QPushButton("×", m_titleBar);
    m_closeButton->setStyleSheet(buttonStyle + 
        "QPushButton:hover { background-color: rgba(220, 53, 69, 0.8); }");
    connect(m_closeButton, &QPushButton::clicked, this, &AvatarSettingsWindow::onCloseClicked);
    
    m_titleBarLayout->addWidget(m_minimizeButton);
    m_titleBarLayout->addSpacing(2);
    m_titleBarLayout->addWidget(m_closeButton);
    
    m_mainLayout->addWidget(m_titleBar);
}

void AvatarSettingsWindow::setupAvatarGrid()
{
    // 创建滚动区域
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setStyleSheet(
        "QScrollArea {"
        "    background-color: #2a2a2a;"
        "    border: none;"
        "    border-bottom-left-radius: 6px;"
        "    border-bottom-right-radius: 6px;"
        "}"
        "QScrollBar:vertical {"
        "    background-color: #3a3a3a;"
        "    width: 12px;"
        "    border-radius: 6px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background-color: #5a5a5a;"
        "    border-radius: 6px;"
        "    min-height: 20px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "    background-color: #6a6a6a;"
        "}"
    );
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    // 创建内容组件
    m_contentWidget = new QWidget();
    m_contentWidget->setStyleSheet("background-color: #2a2a2a;");
    
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(20, 20, 20, 10);
    m_contentLayout->setSpacing(20);
    
    // 创建网格布局用于头像
    QWidget *gridWidget = new QWidget();
    m_avatarGridLayout = new QGridLayout(gridWidget);
    m_avatarGridLayout->setSpacing(15);
    m_avatarGridLayout->setContentsMargins(0, 0, 0, 0);
    
    m_contentLayout->addWidget(gridWidget);
    m_contentLayout->addStretch();
    
    m_scrollArea->setWidget(m_contentWidget);
    m_mainLayout->addWidget(m_scrollArea, 1);
}

void AvatarSettingsWindow::loadAvatarImages()
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString mapsDir = QString("%1/maps/icon").arg(appDir);
    for (int i = 3; i <= 21; ++i) {
        QString imagePath = QString("%1/%2.png").arg(mapsDir, QString::number(i));
        
        // 创建头像标签
        QLabel *avatarLabel = new QLabel();
        avatarLabel->setFixedSize(AVATAR_SIZE, AVATAR_SIZE);
        avatarLabel->setStyleSheet(
            "QLabel {"
            "    border: 2px solid transparent;"
            "    border-radius: 8px;"
            "    background-color: #3a3a3a;"
            "}"
            "QLabel:hover {"
            "    border-color: #4caf50;"
            "    background-color: #4a4a4a;"
            "}"
        );
        avatarLabel->setAlignment(Qt::AlignCenter);
        avatarLabel->setScaledContents(true);
        
        // 尝试加载图片
        QPixmap pixmap(imagePath);
        if (!pixmap.isNull()) {
            avatarLabel->setPixmap(pixmap);
            
        } else {
            // 如果图片加载失败，显示图片编号
            avatarLabel->setText(QString::number(i));
            avatarLabel->setStyleSheet(avatarLabel->styleSheet() + 
                "QLabel { color: white; font-size: 16px; font-weight: bold; }");
            
        }
        
        // 设置属性用于识别图片ID
        avatarLabel->setProperty("iconId", i);
        
        // 添加鼠标事件
        avatarLabel->installEventFilter(this);
        avatarLabel->setAttribute(Qt::WA_Hover, true);
        
        // 添加到网格布局
        int row = (i - 3) / GRID_COLUMNS;
        int col = (i - 3) % GRID_COLUMNS;
        m_avatarGridLayout->addWidget(avatarLabel, row, col);
        
        // 保存引用
        m_avatarLabels.append(avatarLabel);
    }
}

void AvatarSettingsWindow::selectAvatar(int iconId)
{
    // 清除之前的选择
    if (m_selectedAvatarLabel) {
        m_selectedAvatarLabel->setStyleSheet(
            "QLabel {"
            "    border: 2px solid transparent;"
            "    border-radius: 8px;"
            "    background-color: #3a3a3a;"
            "}"
            "QLabel:hover {"
            "    border-color: #4caf50;"
            "    background-color: #4a4a4a;"
            "}"
        );
    }
    
    // 设置新的选择
    m_selectedIconId = iconId;
    
    // 找到对应的标签并高亮显示
    for (QLabel *label : m_avatarLabels) {
        if (label->property("iconId").toInt() == iconId) {
            m_selectedAvatarLabel = label;
            label->setStyleSheet(
                "QLabel {"
                "    border: 3px solid #4caf50;"
                "    border-radius: 8px;"
                "    background-color: #4a4a4a;"
                "}"
            );
            break;
        }
    }
    
}

bool AvatarSettingsWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QLabel *label = qobject_cast<QLabel*>(obj);
        if (label && m_avatarLabels.contains(label)) {
            int iconId = label->property("iconId").toInt();
            onAvatarClicked(iconId);
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void AvatarSettingsWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_titleBar->geometry().contains(event->pos())) {
        m_dragging = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void AvatarSettingsWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton && m_dragging) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void AvatarSettingsWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    }
}

void AvatarSettingsWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 绘制窗口背景和边框
    painter.setBrush(QBrush(QColor(42, 42, 42)));
    painter.setPen(QPen(QColor(60, 60, 60), 1));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
    
    QWidget::paintEvent(event);
}

void AvatarSettingsWindow::onMinimizeClicked()
{
    showMinimized();
}

void AvatarSettingsWindow::onCloseClicked()
{
    close();
}

void AvatarSettingsWindow::onAvatarClicked(int iconId)
{
    // 选择头像并提供视觉反馈
    selectAvatar(iconId);
    
    // 触发头像选择信号，但保持窗口打开
    
    emit avatarSelected(iconId);
}