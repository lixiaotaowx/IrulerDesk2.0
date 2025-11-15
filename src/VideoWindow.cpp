#include "VideoWindow.h"
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QDebug>
#include <windows.h>

VideoWindow::VideoWindow(QWidget *parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_titleBar(nullptr)
    , m_titleBarLayout(nullptr)
    , m_titleLabel(nullptr)
    , m_minimizeButton(nullptr)
    , m_maximizeButton(nullptr)
    , m_closeButton(nullptr)
    , m_colorButton(nullptr)
    , m_micButton(nullptr)
    , m_speakerButton(nullptr)
    , m_videoDisplayWidget(nullptr)
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
    // 颜色变化时更新标题栏按钮外观
    connect(m_videoDisplayWidget, &VideoDisplayWidget::annotationColorChanged,
            this, [this](int colorId){ updateColorButtonVisual(colorId); });
    
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
    
    // 麦克风按钮（置于标题栏最右侧区域，靠近最小化）
    m_micButton = new QPushButton("", m_titleBar);
    m_micButton->setCheckable(true);
    m_micButton->setChecked(false);
    m_micButton->setToolTip(QStringLiteral("点击开启/关闭麦克风"));
    // 使用与头像相同的相对路径策略：appDir/maps/logo
    QString appDir = QCoreApplication::applicationDirPath();
    QString iconDir = appDir + "/maps/logo";
    m_micIconOff = QIcon(iconDir + "/Mic_off.png");
    m_micIconOn  = QIcon(iconDir + "/Mic_on.png");
    m_micButton->setIcon(m_micIconOff);
    m_micButton->setIconSize(QSize(16, 16));
    // 与窗口控制按钮风格一致
    QString micButtonStyle = 
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
    m_micButton->setStyleSheet(micButtonStyle);
    connect(m_micButton, &QPushButton::toggled, this, &VideoWindow::onMicToggled);

    // 批注颜色按钮（循环切换红/绿/蓝/黄）- 使用自绘圆形按钮确保纯圆与抗锯齿
    m_colorButton = new ColorCircleButton(m_titleBar);
    m_colorButton->setToolTip(QStringLiteral("批注颜色"));
    m_colorButton->setFixedSize(13, 13);
    m_colorButton->setCursor(Qt::PointingHandCursor);
    connect(m_colorButton, &ColorCircleButton::clicked, this, &VideoWindow::onColorClicked);
    // 初始颜色与按钮外观
    if (m_videoDisplayWidget) {
        updateColorButtonVisual(m_videoDisplayWidget->annotationColorId());
    } else {
        updateColorButtonVisual(0);
    }

    // 扬声器按钮（本地播放开/关，不影响推流端）
    m_speakerButton = new QPushButton("", m_titleBar);
    m_speakerButton->setCheckable(true);
    m_speakerButton->setChecked(true);
    m_speakerButton->setToolTip(QStringLiteral("点击开启/关闭扬声器"));
    m_speakerIconOff = QIcon(iconDir + "/laba_off.png");
    m_speakerIconOn  = QIcon(iconDir + "/laba_on.png");
    m_speakerButton->setIcon(m_speakerIconOn);
    m_speakerButton->setIconSize(QSize(16, 16));
    m_speakerButton->setStyleSheet(micButtonStyle);
    connect(m_speakerButton, &QPushButton::toggled, this, &VideoWindow::onSpeakerToggled);

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
    
    // 布局调整：将颜色按钮居中到标题栏中间
    // 左侧：标题标签（已添加）
    // 中间：颜色按钮（居中）
    m_titleBarLayout->addWidget(m_colorButton, 0, Qt::AlignCenter);
    m_titleBarLayout->addStretch();
    // 右侧：扬声器、麦克风、窗口控制按钮
    m_titleBarLayout->addWidget(m_speakerButton);
    m_titleBarLayout->addSpacing(2);
    m_titleBarLayout->addWidget(m_micButton);
    m_titleBarLayout->addSpacing(2);
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
    // 右键由VideoDisplayWidget处理其上下文菜单，这里不拦截
    QWidget::mousePressEvent(event);
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
    // 不再拦截原生右键事件，交由Qt事件系统与VideoDisplayWidget处理
    Q_UNUSED(msg);
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
    // 关闭时停止接收，通知采集端停止推流
    if (m_videoDisplayWidget) {
        m_videoDisplayWidget->stopReceiving();
    }
    hide();
}

void VideoWindow::onMicToggled(bool checked)
{
    // 更新图标反馈；暂不接入实际麦克风逻辑
    m_micButton->setIcon(checked ? m_micIconOn : m_micIconOff);
    m_micButton->setToolTip(checked ? QStringLiteral("麦克风：开") : QStringLiteral("麦克风：关"));
    // 发出音频测试开关到观看会话
    if (m_videoDisplayWidget) {
        m_videoDisplayWidget->sendAudioToggle(checked);
    }
}

void VideoWindow::onSpeakerToggled(bool checked)
{
    // 本地播放控制（不影响推流端）
    m_speakerButton->setIcon(checked ? m_speakerIconOn : m_speakerIconOff);
    m_speakerButton->setToolTip(checked ? QStringLiteral("扬声器：开") : QStringLiteral("扬声器：关"));
    if (m_videoDisplayWidget) {
        m_videoDisplayWidget->setSpeakerEnabled(checked);
    }
}

void VideoWindow::onColorClicked()
{
    int current = m_videoDisplayWidget ? m_videoDisplayWidget->annotationColorId() : 0;
    int next = (current + 1) % 4; // 0:红,1:绿,2:蓝,3:黄
    if (m_videoDisplayWidget) {
        m_videoDisplayWidget->setAnnotationColorId(next);
    }
    updateColorButtonVisual(next);
}

void VideoWindow::updateColorButtonVisual(int colorId)
{
    QString tip;
    switch (colorId) {
    case 0: tip = QStringLiteral("批注颜色：红"); break;
    case 1: tip = QStringLiteral("批注颜色：绿"); break;
    case 2: tip = QStringLiteral("批注颜色：蓝"); break;
    default: tip = QStringLiteral("批注颜色：黄"); break;
    }
    if (m_colorButton) {
        m_colorButton->setColorId(colorId);
        m_colorButton->setToolTip(tip);
    }
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
