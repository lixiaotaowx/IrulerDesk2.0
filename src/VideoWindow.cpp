#include "VideoWindow.h"
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <QDebug>
#include <windows.h>

VideoWindow::VideoWindow(QWidget *parent)
    : QWidget(parent)
    , m_dragging(false)
    , m_isMaximized(false)
{
    setupUI();
    
    // 设置窗口属性
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground);
    
    // 设置16:9比例的视频内容区域尺寸（标题栏高度额外计算）
    int titleBarHeight = 28;  // 标题栏高度
    int videoWidth = 1280;
    int videoHeight = 720;  // 视频内容区域16:9比例
    int totalHeight = videoHeight + titleBarHeight;  // 总窗口高度 = 视频高度 + 标题栏高度
    
    // 最小尺寸：视频区域960x540 + 标题栏高度
    int minVideoWidth = 960;
    int minVideoHeight = 540;
    int minTotalHeight = minVideoHeight + titleBarHeight;
    
    setMinimumSize(minVideoWidth, minTotalHeight);
    resize(videoWidth, totalHeight);
    
    // 居中显示
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect screenGeometry = screen->availableGeometry();
        int x = (screenGeometry.width() - width()) / 2;
        int y = (screenGeometry.height() - height()) / 2;
        move(x, y);
    }
}

VideoWindow::~VideoWindow()
{
}

VideoDisplayWidget* VideoWindow::getVideoDisplayWidget() const
{
    return m_videoDisplayWidget;
}

void VideoWindow::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
    setupCustomTitleBar();
    
    // 创建视频显示组件
    m_videoDisplayWidget = new VideoDisplayWidget(this);
    m_videoDisplayWidget->setShowControls(false);
    m_videoDisplayWidget->setAutoResize(true);
    m_videoDisplayWidget->setStyleSheet(
        "VideoDisplayWidget {"
        "    background-color: #000000;"
        "    border: none;"
        "}"
    );
    // 双击全屏切换
    connect(m_videoDisplayWidget, &VideoDisplayWidget::fullscreenToggleRequested,
            this, &VideoWindow::toggleFullscreen);
    
    m_mainLayout->addWidget(m_videoDisplayWidget, 1);
}

void VideoWindow::setupCustomTitleBar()
{
    // 创建标题栏 - 更窄更精致
    m_titleBar = new QWidget(this);
    m_titleBar->setFixedHeight(28);  // 从40减少到28，更窄
    m_titleBar->setStyleSheet(
        "QWidget {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "                stop:0 #3a3a3a, stop:1 #2a2a2a);"
        "    border-top-left-radius: 6px;"  // 减小圆角
        "    border-top-right-radius: 6px;"
        "}"
    );
    
    m_titleBarLayout = new QHBoxLayout(m_titleBar);
    m_titleBarLayout->setContentsMargins(12, 0, 4, 0);  // 减少边距
    m_titleBarLayout->setSpacing(0);
    
    // 标题标签 - 更小的字体
    m_titleLabel = new QLabel("视频播放器", m_titleBar);
    m_titleLabel->setStyleSheet(
        "QLabel {"
        "    color: #ffffff;"
        "    font-size: 12px;"  // 从14px减少到12px
        "    font-weight: 500;"  // 从bold改为medium
        "    background: transparent;"
        "}"
    );
    
    m_titleBarLayout->addWidget(m_titleLabel);
    m_titleBarLayout->addStretch();
    
    // 创建窗口控制按钮 - 更小更精致
    QString buttonStyle = 
        "QPushButton {"
        "    background-color: transparent;"
        "    border: none;"
        "    width: 24px;"   // 从30px减少到24px
        "    height: 24px;"
        "    border-radius: 12px;"  // 相应调整圆角
        "    font-size: 14px;"      // 从16px减少到14px
        "    font-weight: 500;"     // 从bold改为medium
        "    margin: 1px;"          // 从2px减少到1px
        "}"
        "QPushButton:hover {"
        "    background-color: rgba(255, 255, 255, 0.15);"  // 稍微增加透明度
        "}"
        "QPushButton:pressed {"
        "    background-color: rgba(255, 255, 255, 0.25);"
        "}";
    
    // 最小化按钮
    m_minimizeButton = new QPushButton("−", m_titleBar);
    m_minimizeButton->setStyleSheet(buttonStyle + 
        "QPushButton:hover { background-color: rgba(255, 193, 7, 0.7); }");  // 稍微降低透明度
    connect(m_minimizeButton, &QPushButton::clicked, this, &VideoWindow::onMinimizeClicked);
    
    // 最大化按钮
    m_maximizeButton = new QPushButton("□", m_titleBar);
    m_maximizeButton->setStyleSheet(buttonStyle + 
        "QPushButton:hover { background-color: rgba(40, 167, 69, 0.7); }");
    connect(m_maximizeButton, &QPushButton::clicked, this, &VideoWindow::onMaximizeClicked);
    
    // 关闭按钮
    m_closeButton = new QPushButton("×", m_titleBar);
    m_closeButton->setStyleSheet(buttonStyle + 
        "QPushButton:hover { background-color: rgba(220, 53, 69, 0.8); }");
    connect(m_closeButton, &QPushButton::clicked, this, &VideoWindow::onCloseClicked);
    
    // 按钮对齐 - 添加适当间距
    m_titleBarLayout->addWidget(m_minimizeButton);
    m_titleBarLayout->addSpacing(2);  // 按钮间添加小间距
    m_titleBarLayout->addWidget(m_maximizeButton);
    m_titleBarLayout->addSpacing(2);
    m_titleBarLayout->addWidget(m_closeButton);
    
    m_mainLayout->addWidget(m_titleBar);
}

void VideoWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_titleBar->geometry().contains(event->pos())) {
        m_dragging = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    // 右键：全局触发清屏（任意窗口位置）
    if (event->button() == Qt::RightButton) {
        if (m_videoDisplayWidget) {
            m_videoDisplayWidget->sendClear();
        }
        event->accept();
        return;
    }
}

void VideoWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton && m_dragging && !m_isMaximized) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void VideoWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    }
}

void VideoWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 绘制窗口背景和边框
    painter.setBrush(QBrush(QColor(42, 42, 42)));
    painter.setPen(QPen(QColor(60, 60, 60), 1));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
    
    QWidget::paintEvent(event);
}

void VideoWindow::onMinimizeClicked()
{
    showMinimized();
}

// 原生事件拦截：确保物理右键（即便系统交换左右键）仍触发清屏
bool VideoWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType);
    MSG *msg = static_cast<MSG*>(message);
    if (!msg) {
        return QWidget::nativeEvent(eventType, message, result);
    }
    switch (msg->message) {
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP: {
        if (m_videoDisplayWidget) {
            qDebug() << "[VideoWindow] WM_RBUTTON* 捕获，触发清屏";
            m_videoDisplayWidget->sendClear();
        }
        if (result) *result = 0;
        return true; // 消费原生右键事件
    }
    default:
        break;
    }
    return QWidget::nativeEvent(eventType, message, result);
}

void VideoWindow::onMaximizeClicked()
{
    if (m_isMaximized) {
        // 恢复窗口
        setGeometry(m_normalGeometry);
        m_maximizeButton->setText("□");
        m_isMaximized = false;
    } else {
        // 最大化窗口
        m_normalGeometry = geometry();
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            setGeometry(screen->availableGeometry());
        }
        m_maximizeButton->setText("❐");
        m_isMaximized = true;
    }
}

void VideoWindow::onCloseClicked()
{
    hide();
}

void VideoWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
        if (m_titleBar) m_titleBar->show();
    } else {
        // 记录当前几何以便恢复
        m_normalGeometry = geometry();
        showFullScreen();
        if (m_titleBar) m_titleBar->hide();
    }
}