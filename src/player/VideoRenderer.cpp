#include "VideoRenderer.h"
#include <QApplication>
#include <QScreen>
#include <QDateTime>
#include <QMutexLocker>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>

VideoRenderer::VideoRenderer(QWidget *parent)
    : QMainWindow(parent)
    , m_centralWidget(nullptr)
    , m_mainLayout(nullptr)
    , m_videoLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_statusBar(nullptr)
    , m_connected(false)
    , m_displayTimer(nullptr)
    , m_statsTimer(nullptr)
    , m_lastFrameTime(0)
{
    setupUI();
    setupStatusBar();
    
    memset(&m_stats, 0, sizeof(m_stats));
    
    // 设置显示更新定时器
    m_displayTimer = new QTimer(this);
    connect(m_displayTimer, &QTimer::timeout, this, &VideoRenderer::updateDisplay);
    m_displayTimer->start(16); // ~60 FPS，极高流畅度，减少画面跳动
    
    // 设置统计更新定时器
    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &VideoRenderer::updateStats);
    m_statsTimer->start(1000); // 每秒更新一次统计
    
}

VideoRenderer::~VideoRenderer()
{
    
}

void VideoRenderer::setupUI()
{
    setWindowTitle("视频播放器 - 解码显示");
    setMinimumSize(640, 480);
    resize(1024, 768);
    
    // 居中显示窗口
    QScreen *screen = QApplication::primaryScreen();
    QRect screenGeometry = screen->geometry();
    int x = (screenGeometry.width() - width()) / 2;
    int y = (screenGeometry.height() - height()) / 2;
    move(x, y);
    
    // 创建中央部件
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    // 创建主布局
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setContentsMargins(5, 5, 5, 5);
    m_mainLayout->setSpacing(5);
    
    // 创建视频显示标签
    m_videoLabel = new QLabel(this);
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setMinimumSize(320, 240);
    m_videoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); // 允许标签扩展
    m_videoLabel->setScaledContents(false); // 禁用自动缩放内容
    m_videoLabel->setText("等待视频流...");
    m_videoLabel->setStyleSheet("QLabel { background-color: black; color: white; border: 1px solid gray; font-size: 14px; }");
    
    // 创建状态标签
    m_statusLabel = new QLabel(this);
    m_statusLabel->setText("状态: 未连接");
    m_statusLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
    
    // 添加到布局
    m_mainLayout->addWidget(m_videoLabel, 1);
    m_mainLayout->addWidget(m_statusLabel, 0);
}

void VideoRenderer::setupStatusBar()
{
    m_statusBar = statusBar();
    m_statusBar->showMessage("视频播放器已启动");
}

void VideoRenderer::renderFrame(const QByteArray &frameData, const QSize &frameSize)
{
    if (frameData.isEmpty() || frameSize.isEmpty()) {
        return;
    }
    
    // 移除丢帧策略，确保所有帧都被处理以避免画面跳动
    QMutexLocker locker(&m_frameMutex);
    
    // 创建QImage从ARGB数据
    // 注意：解码器现在输出的是ARGB格式（libyuv库输出）
    QImage image(reinterpret_cast<const uchar*>(frameData.constData()),
                frameSize.width(), frameSize.height(), 
                frameSize.width() * 4, QImage::Format_ARGB32);
    
    // 如果ARGB格式失败，尝试RGB32格式作为备选
    if (image.isNull()) {
        image = QImage(reinterpret_cast<const uchar*>(frameData.constData()),
                      frameSize.width(), frameSize.height(), 
                      frameSize.width() * 4, QImage::Format_RGB32);
    }
    
    if (image.isNull()) {
        return;
    }
    
    // 转换为QPixmap
    m_currentFrame = QPixmap::fromImage(image);
    m_frameSize = frameSize;
    
    // 更新统计
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    if (m_lastFrameTime > 0) {
        m_frameTimes.append(currentTime - m_lastFrameTime);
        if (m_frameTimes.size() > 30) {
            m_frameTimes.removeFirst();
        }
    }
    m_lastFrameTime = currentTime;
    
    m_stats.totalFrames++;
    m_stats.renderedFrames++;
    m_stats.currentFrameSize = frameSize;
}

void VideoRenderer::setConnectionStatus(bool connected)
{
    m_connected = connected;
    
    if (connected) {
        m_statusLabel->setText("状态: 已连接");
        m_statusLabel->setStyleSheet("QLabel { color: green; font-weight: bold; }");
        m_statusBar->showMessage("已连接到视频流");
    } else {
        m_statusLabel->setText("状态: 未连接");
        m_statusLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
        m_statusBar->showMessage("与视频流断开连接");
        

    }
}

void VideoRenderer::updateDisplay()
{
    QPixmap frameToDisplay;
    
    // 使用普通帧
    QMutexLocker frameLocker(&m_frameMutex);
    frameToDisplay = m_currentFrame;
    
    if (frameToDisplay.isNull()) {
        return;
    }
    
    // 获取标签的实际可用区域
    QSize labelSize = m_videoLabel->size();
    if (labelSize.width() <= 0 || labelSize.height() <= 0) {
        return; // 标签尺寸无效，跳过更新
    }
    
    // 计算缩放后的尺寸，保持宽高比
    QPixmap scaledFrame = scalePixmapToFit(frameToDisplay, labelSize);
    
    // 居中显示
    m_videoLabel->setPixmap(scaledFrame);
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setScaledContents(false); // 禁用自动缩放，使用我们的手动缩放
}

void VideoRenderer::updateStats()
{
    // 计算FPS
    if (!m_frameTimes.isEmpty()) {
        double totalTime = 0;
        for (qint64 time : m_frameTimes) {
            totalTime += time;
        }
        double averageFrameTime = totalTime / m_frameTimes.size();
        m_stats.averageFPS = 1000.0 / averageFrameTime;
    }
    
    // 更新状态栏
    QString statusText;
    statusText = QString("帧数: %1 | FPS: %2 | 尺寸: %3x%4")
                .arg(m_stats.renderedFrames)
                .arg(m_stats.averageFPS, 0, 'f', 1)
                .arg(m_stats.currentFrameSize.width())
                .arg(m_stats.currentFrameSize.height());
    
    m_statusBar->showMessage(statusText);
    
    emit statsUpdated(m_stats);
}



QPixmap VideoRenderer::scalePixmapToFit(const QPixmap &pixmap, const QSize &targetSize)
{
    if (pixmap.isNull() || targetSize.isEmpty()) {
        return pixmap;
    }
    
    return pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void VideoRenderer::closeEvent(QCloseEvent *event)
{
    emit windowClosed();
    event->accept();
}

void VideoRenderer::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // 窗口大小改变时，显示会自动调整
}