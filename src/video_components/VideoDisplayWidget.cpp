#include "VideoDisplayWidget.h"
#include "../player/DxvaVP9Decoder.h"
#include "../player/WebSocketReceiver.h"
#include <QPixmap>
#include <QDebug>
#include <QThread>
#include <QDateTime>
#include <QTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPolygon>
#include <windows.h>

VideoDisplayWidget::VideoDisplayWidget(QWidget *parent)
    : QWidget(parent)
    , m_isReceiving(false)
    , m_showControls(true)
    , m_autoResize(false)
    , m_serverUrl("")  // 默认为空，将通过startReceiving方法设置正确的服务器URL
{
    // 设置控制台编码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    setupUI();
    
    // 创建解码器和接收器
    m_decoder = std::make_unique<DxvaVP9Decoder>();
    m_receiver = std::make_unique<WebSocketReceiver>();
    
    // 连接信号槽 - 修复：确保解码后的帧能正确显示
    connect(m_decoder.get(), &DxvaVP9Decoder::frameDecoded,
            this, &VideoDisplayWidget::renderFrame);
    
    // 统计信息连接（减少日志输出）
    connect(m_decoder.get(), &DxvaVP9Decoder::frameDecoded,
            this, [this](const QByteArray &frameData, const QSize &frameSize) {
                m_stats.framesDecoded++;
                // 移除帧统计打印以提升性能
                // 只在前5帧或每100帧输出一次日志 - 已禁用
                // static int frameCount = 0;
                // frameCount++;
                // if (frameCount <= 5 || frameCount % 100 == 0) {
                //     qDebug() << "[VideoDisplayWidget] 解码帧统计 - 第" << frameCount << "帧，尺寸:" << frameSize;
                // }
            });
    
    connect(m_receiver.get(), &WebSocketReceiver::frameReceivedWithTimestamp,
            this, [this](const QByteArray &frameData, qint64 captureTimestamp) {
                m_stats.framesReceived++;
                // 移除帧统计打印以提升性能
                // 只在前5帧或每100帧输出一次日志 - 已禁用
                // static int receiveCount = 0;
                // receiveCount++;
                // if (receiveCount <= 5 || receiveCount % 100 == 0) {
                //     qDebug() << "[VideoDisplayWidget] 接收帧统计 - 第" << receiveCount << "帧，数据大小:" << frameData.size();
                // }
                
                // 存储时间戳用于延迟计算
                m_currentCaptureTimestamp = captureTimestamp;
                // 解码帧
                m_decoder->decodeFrame(frameData);
            });
    connect(m_receiver.get(), &WebSocketReceiver::connectionStatusChanged,
            this, &VideoDisplayWidget::updateConnectionStatus);
    
    // 连接鼠标位置信号
    connect(m_receiver.get(), &WebSocketReceiver::mousePositionReceived,
            this, &VideoDisplayWidget::onMousePositionReceived);
    
    // 统计定时器
    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &VideoDisplayWidget::updateStatsDisplay);
    m_statsTimer->start(1000); // 每秒更新一次统计
    
    qDebug() << "VideoDisplayWidget 创建完成";
}

VideoDisplayWidget::~VideoDisplayWidget()
{
    stopReceiving();
    // qDebug() << "VideoDisplayWidget 销毁";
}

void VideoDisplayWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0); // 移除边距
    m_mainLayout->setSpacing(2); // 减小间距
    
    // 视频显示区域
    m_videoLabel = new QLabel("等待视频流...");
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setStyleSheet("QLabel { background-color: black; color: white; font-size: 14px; }");
    m_videoLabel->setMinimumSize(320, 240); // 减小最小尺寸
    m_videoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); // 允许扩展
    m_videoLabel->setScaledContents(false); // 禁用自动缩放，保持原始比例
    m_mainLayout->addWidget(m_videoLabel, 1); // 添加拉伸因子，让视频区域占据更多空间
    
    // 控制区域
    m_controlLayout = new QHBoxLayout();
    m_controlLayout->setContentsMargins(5, 2, 5, 2); // 减小控制区域边距
    m_controlLayout->setSpacing(5); // 减小控件间距
    
    m_startStopButton = new QPushButton("开始接收");
    m_startStopButton->setMaximumHeight(25); // 限制按钮高度
    connect(m_startStopButton, &QPushButton::clicked, this, &VideoDisplayWidget::onStartStopClicked);
    m_controlLayout->addWidget(m_startStopButton);
    
    m_statusLabel = new QLabel("状态: 未连接");
    m_statusLabel->setMaximumHeight(25); // 限制标签高度
    m_controlLayout->addWidget(m_statusLabel);
    
    m_controlLayout->addStretch();
    
    m_statsLabel = new QLabel("统计: 0/0/0 帧");
    m_statsLabel->setMaximumHeight(25); // 限制标签高度
    m_controlLayout->addWidget(m_statsLabel);
    
    m_mainLayout->addLayout(m_controlLayout, 0); // 不拉伸，保持最小高度
    
    setLayout(m_mainLayout);
}

void VideoDisplayWidget::startReceiving(const QString &serverUrl)
{
    // 如果已经在接收且URL相同，则不需要重新连接
    if (m_isReceiving && m_serverUrl == serverUrl) {
        qDebug() << "已经连接到相同的服务器URL，无需重新连接:" << serverUrl;
        return;
    }
    
    // 如果正在接收但URL不同，或者需要重新连接，先断开之前的连接
    if (m_isReceiving) {
        qDebug() << "断开之前的连接，准备连接到新的服务器:" << serverUrl;
        stopReceiving();
        // 给一点时间让断开连接完成
        QThread::msleep(100);
    }
    
    m_serverUrl = serverUrl;
    
    // 初始化VP9解码器
    // qDebug() << "初始化VP9解码器...";
    if (!m_decoder->initialize()) {
        qCritical() << "VP9解码器初始化失败";
        return;
    }
    // qDebug() << "VP9解码器初始化成功";
    
    // qDebug() << "开始连接到:" << m_serverUrl;
    m_receiver->connectToServer(m_serverUrl);
    
    m_isReceiving = true;
    updateButtonText();
    
    // 重置统计
    m_stats = VideoStats();
    m_stats.connectionStatus = "Connecting...";
    
    emit connectionStatusChanged(m_stats.connectionStatus);
}

void VideoDisplayWidget::stopReceiving()
{
    if (!m_isReceiving) {
        return;
    }
    
    qDebug() << "停止接收视频流";
    m_receiver->disconnectFromServer();
    
    // 清理解码器缓存，确保切换设备时没有残留状态
    if (m_decoder) {
        // qDebug() << "清理解码器缓存...";
        m_decoder->cleanup();
    }
    
    // 清理显示缓存
    m_videoLabel->clear();
    m_videoLabel->setText("视频显示区域");
    
    m_isReceiving = false;
    updateButtonText();
    
    // 重置统计数据
    m_stats = VideoStats();
    m_stats.connectionStatus = "Disconnected";
    emit connectionStatusChanged(m_stats.connectionStatus);
    
    qDebug() << "视频流停止完成，缓存已清理";
}

void VideoDisplayWidget::sendWatchRequest(const QString &viewerId, const QString &targetId)
{
    if (m_receiver) {
        // qDebug() << "[VideoDisplayWidget] 发送观看请求，观看者ID:" << viewerId << "目标ID:" << targetId;
        m_receiver->sendWatchRequest(viewerId, targetId);
    } else {
        // qDebug() << "[VideoDisplayWidget] WebSocketReceiver未初始化，无法发送观看请求";
    }
}

void VideoDisplayWidget::setShowControls(bool show)
{
    m_showControls = show;
    
    // 显示或隐藏控制区域的所有控件
    for (int i = 0; i < m_controlLayout->count(); ++i) {
        QLayoutItem *item = m_controlLayout->itemAt(i);
        if (item && item->widget()) {
            item->widget()->setVisible(show);
        }
    }
    
    // 当隐藏控制区域时，减小布局间距
    if (!show) {
        m_mainLayout->setSpacing(0);
        m_controlLayout->setContentsMargins(0, 0, 0, 0);
    } else {
        m_mainLayout->setSpacing(2);
        m_controlLayout->setContentsMargins(5, 2, 5, 2);
    }
}

void VideoDisplayWidget::renderFrame(const QByteArray &frameData, const QSize &frameSize)
{
    if (frameData.isEmpty() || frameSize.isEmpty()) {
        return;
    }
    
    // 计算端到端延迟
    qint64 displayTimestamp = QDateTime::currentMSecsSinceEpoch();
    if (m_currentCaptureTimestamp > 0) {
        double latency = displayTimestamp - m_currentCaptureTimestamp;
        
        // 更新延迟历史
        m_latencyHistory.append(latency);
        if (m_latencyHistory.size() > MAX_LATENCY_SAMPLES) {
            m_latencyHistory.removeFirst();
        }
        
        // 计算平均延迟
        double totalLatency = 0.0;
        for (double l : m_latencyHistory) {
            totalLatency += l;
        }
        m_stats.avgEndToEndLatency = totalLatency / m_latencyHistory.size();
        
        // 重置时间戳
        m_currentCaptureTimestamp = 0;
    }
    
    // 验证像素数据大小（ARGB32格式，每像素4字节）
    int expectedSize = frameSize.width() * frameSize.height() * 4;
    if (frameData.size() != expectedSize) {
        qWarning() << "[VideoDisplayWidget] 像素数据大小不匹配，期望:" << expectedSize << "实际:" << frameData.size();
        return;
    }
    
    // 使用双缓冲机制防止闪烁
    static QImage frontBuffer;
    static QImage backBuffer;
    static QMutex bufferMutex;
    static bool bufferInitialized = false;
    
    // 初始化缓冲区
    if (!bufferInitialized || backBuffer.size() != frameSize) {
        frontBuffer = QImage(frameSize, QImage::Format_ARGB32);
        backBuffer = QImage(frameSize, QImage::Format_ARGB32);
        bufferInitialized = true;
    }
    
    // 在后台缓冲区准备新帧
    {
        QMutexLocker locker(&bufferMutex);
        memcpy(backBuffer.bits(), frameData.constData(), frameData.size());
        
        // 交换缓冲区
        std::swap(frontBuffer, backBuffer);
    }
    
    if (!frontBuffer.isNull()) {
        // 使用互斥锁保护显示更新，防止跳闪
        static QMutex displayMutex;
        QMutexLocker locker(&displayMutex);
        
        // 检查标签尺寸有效性
        QSize labelSize = m_videoLabel->size();
        if (labelSize.width() <= 0 || labelSize.height() <= 0) {
            return;
        }
        
        QPixmap pixmap = QPixmap::fromImage(frontBuffer);
        
        // 如果有鼠标位置数据，在图像上绘制鼠标光标
        if (m_hasMousePosition) {
            drawMouseCursor(pixmap, m_mousePosition);
        }
        
        // 始终保持宽高比并居中显示
        QPixmap scaledPixmap = pixmap.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        
        // 使用QTimer确保UI更新在主线程中进行，避免闪烁
        QTimer::singleShot(0, this, [this, scaledPixmap]() {
            m_videoLabel->setPixmap(scaledPixmap);
            m_videoLabel->setAlignment(Qt::AlignCenter);
        });
        
        m_stats.framesDisplayed++;
        m_stats.frameSize = frameSize;
        emit frameReceived();
        
        // 移除显示帧统计打印以提升性能
        // 只在前5帧或每100帧输出一次显示日志 - 已禁用
        // static int displayCount = 0;
        // displayCount++;
        // if (displayCount <= 5 || displayCount % 100 == 0) {
        //     qDebug() << "[VideoDisplayWidget] 显示帧统计 - 第" << displayCount << "帧，总显示:" << m_stats.framesDisplayed;
        // }
    }
}

void VideoDisplayWidget::updateConnectionStatus(const QString &status)
{
    m_stats.connectionStatus = status;
    m_statusLabel->setText(QString("状态: %1").arg(status));
    
    if (status == "Connected") {
        m_videoLabel->setText("");
    } else if (status == "Disconnected" && m_isReceiving) {
        m_videoLabel->setText("连接断开，尝试重连...");
    }
    
    emit connectionStatusChanged(status);
}

void VideoDisplayWidget::onStartStopClicked()
{
    if (m_isReceiving) {
        stopReceiving();
    } else {
        startReceiving(m_serverUrl);
    }
}

void VideoDisplayWidget::updateStatsDisplay()
{
    QString statsText = QString("统计: %1/%2/%3 帧 | 延迟: %4ms")
                       .arg(m_stats.framesReceived)
                       .arg(m_stats.framesDecoded)
                       .arg(m_stats.framesDisplayed)
                       .arg(m_stats.avgEndToEndLatency, 0, 'f', 1);
    
    m_statsLabel->setText(statsText);
    
    emit statsUpdated(m_stats);
}

void VideoDisplayWidget::updateButtonText()
{
    m_startStopButton->setText(m_isReceiving ? "停止接收" : "开始接收");
}

void VideoDisplayWidget::renderFrameWithTimestamp(const QByteArray &frameData, const QSize &frameSize, qint64 captureTimestamp)
{
    // 存储捕获时间戳
    m_currentCaptureTimestamp = captureTimestamp;
    
    // 调用原有的renderFrame方法
    renderFrame(frameData, frameSize);
}

void VideoDisplayWidget::onMousePositionReceived(const QPoint &position, qint64 timestamp)
{
    // 更新鼠标位置数据
    m_mousePosition = position;
    m_mouseTimestamp = timestamp;
    m_hasMousePosition = true;
    
    // 移除鼠标位置统计日志以提升性能
    // 每100条鼠标位置更新输出一次统计（避免日志过多） - 已禁用
    // static int mouseUpdateCount = 0;
    // mouseUpdateCount++;
    // if (mouseUpdateCount % 100 == 0) {
    //     qDebug() << "[VideoDisplayWidget] 已接收" << mouseUpdateCount 
    //              << "次鼠标位置更新，最新位置:" << position;
    // }
}

void VideoDisplayWidget::drawMouseCursor(QPixmap &pixmap, const QPoint &position)
{
    // 在pixmap上绘制鼠标光标
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // 设置光标样式 - 使用经典的箭头光标
    QPen pen(Qt::white, 2);
    painter.setPen(pen);
    painter.setBrush(Qt::black);
    
    // 绘制箭头光标（简化版本）
    QPolygon arrow;
    arrow << QPoint(position.x(), position.y())           // 箭头顶点
          << QPoint(position.x() + 3, position.y() + 8)   // 左下
          << QPoint(position.x() + 6, position.y() + 6)   // 中间
          << QPoint(position.x() + 10, position.y() + 10) // 右下
          << QPoint(position.x() + 8, position.y() + 12)  // 右下角
          << QPoint(position.x(), position.y());          // 回到顶点
    
    painter.drawPolygon(arrow);
    
    // 添加白色边框以提高可见性
    pen.setColor(Qt::white);
    pen.setWidth(1);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(arrow);
}