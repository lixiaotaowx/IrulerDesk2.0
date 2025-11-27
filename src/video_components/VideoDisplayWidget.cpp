#include "VideoDisplayWidget.h"
#include "../player/DxvaVP9Decoder.h"
#include "../player/WebSocketReceiver.h"
#include <QApplication>
#include <QPixmap>
#include <QThread>
#include <QDateTime>
#include <QTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPolygon>
#include <QMouseEvent>
#include <QShortcut>
#include <QMenu>
#include <QAction>
#include <QCursor>
#include <QLineEdit>
#include <QFont>
#include <QCoreApplication>
#include <QFile>
#include <QFontMetrics>
#include <QRandomGenerator>
#include <QRandomGenerator>
#include <QMediaDevices>
#include <QDebug>
#include <windows.h>
#include <iostream>

VideoDisplayWidget::VideoDisplayWidget(QWidget *parent)
    : QWidget(parent)
    , m_isReceiving(false)
    , m_showControls(true)
    , m_autoResize(false)
    , m_serverUrl("")  // 默认为空，将通过startReceiving方法设置正确的服务器URL
    , m_tileMode(false)  // 默认关闭瓦片模式
    , m_tileTimeout(5000)  // 瓦片超时时间5秒
    , m_compositionInProgress(false)
{
    // 设置控制台编码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // 检测系统是否交换了鼠标左右键（Windows）
#ifdef _WIN32
    m_mouseButtonsSwapped = (GetSystemMetrics(SM_SWAPBUTTON) != 0);
#else
    m_mouseButtonsSwapped = false;
#endif
    
    setupUI();
    
    // 创建解码器和接收器
    m_decoder = std::make_unique<DxvaVP9Decoder>();
    m_receiver = std::make_unique<WebSocketReceiver>();
    if (!m_decoderInitialized) {
        m_decoderInitialized = m_decoder->initialize();
    }
    
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
    connect(m_receiver.get(), &WebSocketReceiver::connected, this, [this]() {
        if (!m_lastViewerId.isEmpty() && !m_lastTargetId.isEmpty()) {
            m_receiver->sendWatchRequest(m_lastViewerId, m_lastTargetId);
        }
    });
    
    // 连接瓦片相关信号
    connect(m_receiver.get(), &WebSocketReceiver::tileCompleted,
            this, [this](int tileId, const QByteArray &completeData) {
                // 计算瓦片在屏幕上的位置
                QRect sourceRect = calculateTileSourceRect(tileId);
                qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
                
                // 渲染瓦片
                renderTile(tileId, completeData, sourceRect, timestamp);
                
            });
    
    connect(m_receiver.get(), &WebSocketReceiver::tileUpdateReceived,
            this, [this](const WebSocketReceiver::TileUpdate &update) {
                QRect updateRect(update.x, update.y, update.width, update.height);
                updateTile(update.tileId, update.deltaData, updateRect, update.timestamp);
                
            });
    
    connect(m_receiver.get(), &WebSocketReceiver::tileMetadataReceived,
            this, [this](const WebSocketReceiver::TileMetadata &metadata) {
                // 如果是第一个瓦片元数据，设置瓦片配置
                if (m_tileComposition.frameSize.isEmpty()) {
                    // 根据瓦片信息推算完整帧大小
                    QSize frameSize(metadata.x + metadata.width, metadata.y + metadata.height);
                    QSize tileSize(metadata.width, metadata.height);
                    setTileConfiguration(frameSize, tileSize);
                    
                    // 启用瓦片模式
                    setTileMode(true);
                    
                }
            });

    // 音频帧接收（本地扬声器开关控制是否处理）
    m_audioConn = connect(m_receiver.get(), &WebSocketReceiver::audioFrameReceived,
            this, [this](const QByteArray &pcmData, int sampleRate, int channels, int bitsPerSample, qint64) {
                if (!m_isReceiving) return;
                if (!m_speakerEnabled) return;
                m_lastFrameSampleRate = sampleRate;
                m_lastFrameChannels = channels;
                m_lastFrameBitsPerSample = bitsPerSample;
                initAudioSinkIfNeeded(sampleRate, channels, bitsPerSample);
                if (!m_audioSink) return;
                if (m_audioSink->state() == QAudio::StoppedState) {
                    m_audioIO = m_audioSink->start();
                }
                if (m_audioSink->state() == QAudio::SuspendedState) {
                    m_audioSink->resume();
                }
                if (m_audioIO) {
                    QByteArray out = m_needResample ? convertForSink(pcmData, sampleRate, channels) : pcmData;
                    m_audioIO->write(out);
                }
            });
    
    // 统计定时器
    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &VideoDisplayWidget::updateStatsDisplay);
    m_statsTimer->start(1000); // 每秒更新一次统计
    
    // 瓦片清理定时器
    m_tileCleanupTimer = new QTimer(this);
    connect(m_tileCleanupTimer, &QTimer::timeout, this, &VideoDisplayWidget::cleanupOldTiles);
    m_tileCleanupTimer->start(2000); // 每2秒清理一次过期瓦片
    
    // 继续观看提示定时器初始化（默认30分钟，可通过环境变量覆盖）
    setupContinuePrompt();

    m_mediaDevices = new QMediaDevices(this);
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged, this, [this]() {
        int sr = m_audioFormat.sampleRate() > 0 ? m_audioFormat.sampleRate() : 16000;
        int ch = m_audioFormat.channelCount() > 0 ? m_audioFormat.channelCount() : 1;
        if (m_followSystemOutput) {
            forceRecreateSink();
        }
    });
    m_defaultOutPollTimer = new QTimer(this);
    m_defaultOutPollTimer->setInterval(500);
    connect(m_defaultOutPollTimer, &QTimer::timeout, this, [this]() {
        if (!m_followSystemOutput) return;
        QAudioDevice def = QMediaDevices::defaultAudioOutput();
        if (m_currentOutputDeviceId != def.id()) {
            forceRecreateSink();
        }
    });
    m_defaultOutPollTimer->start();
}

VideoDisplayWidget::~VideoDisplayWidget()
{
    stopReceiving();
    if (m_decoder) {
        m_decoder->cleanup();
        m_decoderInitialized = false;
    }
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
    m_videoLabel->setCursor(Qt::ArrowCursor);
    // 启用鼠标跟踪并安装事件过滤器，用于批注坐标采集
    m_videoLabel->setMouseTracking(true);
    m_videoLabel->setFocusPolicy(Qt::StrongFocus); // 允许接收键盘事件（用于Ctrl+Z撤销）
    m_videoLabel->setContextMenuPolicy(Qt::NoContextMenu); // 禁用默认右键菜单，避免干扰
    m_videoLabel->installEventFilter(this);
    m_mainLayout->addWidget(m_videoLabel, 1); // 添加拉伸因子，让视频区域占据更多空间

    // 键盘快捷键：Ctrl+Z 撤销（窗口级）
    QShortcut *undoShortcut = new QShortcut(QKeySequence::Undo, this);
    undoShortcut->setContext(Qt::WindowShortcut);
    connect(undoShortcut, &QShortcut::activated, this, &VideoDisplayWidget::sendUndo);
    
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
        return;
    }
    
    // 如果正在接收但URL不同，或者需要重新连接，先断开之前的连接
    if (m_isReceiving) {
        stopReceiving();
        // 断开后直接重建接收器，避免旧实例残留状态导致卡死
        recreateReceiver();
    }
    
    m_serverUrl = serverUrl;
    
    // 解码器已在构造时预初始化；若未初始化则尝试一次
    if (!m_decoderInitialized) {
        m_decoderInitialized = m_decoder->initialize();
        if (!m_decoderInitialized) { return; }
    }
    
    // qDebug() << "开始连接到:" << m_serverUrl;
    m_receiver->connectToServer(m_serverUrl);

    m_isReceiving = true;
    updateButtonText();
    
    // 重置统计
    m_stats = VideoStats();
    m_stats.connectionStatus = "Connecting...";
    
    emit connectionStatusChanged(m_stats.connectionStatus);
    showWaitingSplash();

    // 启动“是否继续观看”周期提示定时器
    if (!m_continuePromptTimer) {
        setupContinuePrompt();
    }
    if (m_continuePromptTimer) {
        m_continuePromptTimer->stop();
        m_continuePromptTimer->setInterval(m_promptIntervalMinutes * 60 * 1000);
        // 测试时可通过环境变量将分钟设为1
        m_continuePromptTimer->start();
    }

    // 如果之前已经有viewer/target信息，连接后自动重发观看请求，以便恢复推流
    if (!m_lastViewerId.isEmpty() && !m_lastTargetId.isEmpty()) {
        if (m_receiver && m_receiver->isConnected()) {
            m_receiver->sendWatchRequest(m_lastViewerId, m_lastTargetId);
        }
    }
}

void VideoDisplayWidget::stopReceiving()
{
    if (!m_isReceiving) {
        return;
    }
    
    m_receiver->disconnectFromServer();
    
    // 清理解码器缓存，确保切换设备时没有残留状态
    // 避免在停止时清理解码器，减少重开延迟；仅在析构时清理

    // 停止并清理音频输出，避免残留状态影响下一次播放
    if (m_audioSink) {
        if (m_audioSink->state() != QAudio::StoppedState) {
            m_audioSink->stop();
        }
    }
    m_audioIO = nullptr;
    m_audioInitialized = false;
    
    // 清理瓦片相关状态，防止残留导致新会话不初始化配置而黑屏
    clearTiles();
    m_tileComposition.frameSize = QSize();
    m_tileComposition.tileSize = QSize();
    m_tileComposition.tilesPerRow = 0;
    m_tileComposition.tilesPerColumn = 0;
    m_tileComposition.lastUpdateTime = 0;
    setTileMode(false);
    m_compositeFrame = QPixmap();
    
    // 清理显示缓存
    m_videoLabel->clear();
    showWaitingSplash();
    
    m_isReceiving = false;
    updateButtonText();
    
    // 重置统计数据
    m_stats = VideoStats();
    m_stats.connectionStatus = "Disconnected";
    emit connectionStatusChanged(m_stats.connectionStatus);
    
    // 停止“是否继续观看”的周期提示与倒计时弹窗
    if (m_continuePromptTimer) {
        m_continuePromptTimer->stop();
    }
    if (m_promptCountdownTimer) {
        m_promptCountdownTimer->stop();
    }
    if (m_promptDialog) {
        m_promptDialog->hide();
    }
    recreateReceiver();
}

void VideoDisplayWidget::sendWatchRequest(const QString &viewerId, const QString &targetId)
{
    // 保存最近一次viewer/target，用于在重新开始接收或重连后自动重发
    m_lastViewerId = viewerId;
    m_lastTargetId = targetId;
    if (m_receiver) {
        if (!m_viewerName.isEmpty()) {
            m_receiver->setViewerName(m_viewerName);
        }
        m_receiver->sendWatchRequest(viewerId, targetId);
    } else {
    }
}

void VideoDisplayWidget::setViewerName(const QString &name)
{
    m_viewerName = name;
    updateLocalCursorComposite();
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
        
        
        
        // 始终保持宽高比并居中显示
        QPixmap scaledPixmap = pixmap.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        
        // 使用QTimer确保UI更新在主线程中进行，避免闪烁
        QTimer::singleShot(0, this, [this, scaledPixmap]() {
            m_videoLabel->setPixmap(scaledPixmap);
            m_videoLabel->setAlignment(Qt::AlignCenter);
            stopWaitingSplash();
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
    if (status == "Connecting..." || (status == "Disconnected" && m_isReceiving)) {
        showWaitingSplash();
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

void VideoDisplayWidget::showWaitingSplash()
{
    if (!m_videoLabel) return;
    m_waitSplashActive = true;
    if (!m_waitingDotsTimer) {
        m_waitingDotsTimer = new QTimer(this);
        m_waitingDotsTimer->setInterval(400);
        connect(m_waitingDotsTimer, &QTimer::timeout, this, &VideoDisplayWidget::updateWaitingSplashFrame);
    }
    m_waitBaseCached = QPixmap();
    m_waitWmCached = QPixmap();
    m_waitingDotsPhase = 0;
    updateWaitingSplashFrame();
    m_waitingDotsTimer->start();
}

void VideoDisplayWidget::stopWaitingSplash()
{
    m_waitSplashActive = false;
    if (m_waitingDotsTimer) {
        m_waitingDotsTimer->stop();
    }
}

void VideoDisplayWidget::updateWaitingSplashFrame()
{
    if (!m_videoLabel) return;
    QSize labelSize = m_videoLabel->size();
    if (labelSize.width() <= 0 || labelSize.height() <= 0) {
        return;
    }
    QPixmap canvas(labelSize);
    canvas.fill(Qt::black);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (!m_waitBaseCached.isNull()) {
        QPixmap scaledBase = m_waitBaseCached.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int offsetX = (labelSize.width() - scaledBase.width()) / 2;
        int offsetY = (labelSize.height() - scaledBase.height()) / 2;
        painter.drawPixmap(offsetX, offsetY, scaledBase);
    }

    if (!m_waitWmCached.isNull()) {
        int maxW = qMax(24, labelSize.width() / 12);
        int wmW = qMin(m_waitWmCached.width(), maxW);
        int wmH = (wmW * m_waitWmCached.height()) / qMax(1, m_waitWmCached.width());
        int margin = 12;
        int x = labelSize.width() - wmW - margin;
        int y = labelSize.height() - wmH - margin;
        painter.setOpacity(0.85);
        painter.drawPixmap(x, y, m_waitWmCached.scaled(wmW, wmH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        painter.setOpacity(1.0);
    }

    QFont f = painter.font();
    int textPx = qMax(14, labelSize.width() / 48);
    f.setPixelSize(textPx);
    painter.setFont(f);
    painter.setPen(QColor(255,255,255,220));
    QString text = QStringLiteral("画面等待中");
    QFontMetrics fm(f);
    int tw = fm.horizontalAdvance(text);
    int th = fm.height();
    int marginBottom = 20;
    int baseY = labelSize.height() - th - marginBottom;
    int baseX = (labelSize.width() - tw) / 2;
    painter.drawText(QPoint(baseX, baseY), text);
    int dotR = qMax(3, textPx / 4);
    int spacing = dotR * 2;
    int dotsStartX = baseX + tw + spacing;
    int dotsY = baseY - th/2;
    for (int i = 0; i < 3; ++i) {
        int alpha = (i == m_waitingDotsPhase) ? 230 : 90;
        painter.setBrush(QColor(255,255,255,alpha));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPoint(dotsStartX + i * spacing, dotsY), dotR, dotR);
    }

    painter.end();
    m_lastWaitCanvas = canvas;
    m_videoLabel->setPixmap(canvas);
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_waitingDotsPhase = (m_waitingDotsPhase + 1) % 3;
}



void VideoDisplayWidget::showSwitchingIndicator(const QString &message)
{
    // 更新状态栏文案并显示等待图片
    m_statusLabel->setText(QString("状态: %1").arg(message));
    showWaitingSplash();
}

void VideoDisplayWidget::sendSwitchScreenIndex(int index)
{
    // 显示切换提示并通过接收器发送索引切屏请求
    showSwitchingIndicator(QStringLiteral("切换中..."));
    if (m_receiver) {
        m_receiver->sendSwitchScreenIndex(index);
    }
}

void VideoDisplayWidget::sendAudioToggle(bool enabled)
{
    if (m_receiver) {
        m_receiver->sendAudioToggle(enabled);
    }
}

void VideoDisplayWidget::setSpeakerEnabled(bool enabled)
{
    m_speakerEnabled = enabled;
    {
        QString desc;
        QByteArray id;
        if (m_followSystemOutput) {
            QAudioDevice d = QMediaDevices::defaultAudioOutput();
            desc = d.description();
            id = d.id();
        } else {
            const auto devs = QMediaDevices::audioOutputs();
            for (const auto &d : devs) { if (d.id() == m_outputDeviceId) { desc = d.description(); id = d.id(); break; } }
            if (desc.isEmpty()) {
                QAudioDevice d = QMediaDevices::defaultAudioOutput();
                desc = d.description();
                id = d.id();
            }
        }
        
    }
    if (!enabled) {
        if (!m_lastViewerId.isEmpty() && !m_lastTargetId.isEmpty() && m_lastViewerId == m_lastTargetId) {
            if (m_receiver) m_receiver->sendViewerListenMute(true);
        }
        if (m_audioConn) { QObject::disconnect(m_audioConn); }
        if (m_audioSink) { m_audioSink->stop(); }
        m_audioIO = nullptr;
        if (m_audioSink) { m_audioSink->setVolume(0.0); }
        if (m_audioSink) { delete m_audioSink; m_audioSink = nullptr; }
        m_audioInitialized = false;
    } else {
        if (!m_lastViewerId.isEmpty() && !m_lastTargetId.isEmpty() && m_lastViewerId == m_lastTargetId) {
            if (m_receiver) m_receiver->sendViewerListenMute(false);
        }
        int sr = m_audioFormat.sampleRate() > 0 ? m_audioFormat.sampleRate() : 16000;
        int ch = m_audioFormat.channelCount() > 0 ? m_audioFormat.channelCount() : 1;
        initAudioSinkIfNeeded(sr, ch, 16);
    }
}

// 设置批注颜色ID（0:红,1:绿,2:蓝,3:黄），越界归一到有效范围
void VideoDisplayWidget::setAnnotationColorId(int colorId)
{
    int normalized = colorId;
    if (normalized < 0) normalized = 0;
    if (normalized > 3) normalized = 3;
    if (m_currentColorId == normalized) return;
    m_currentColorId = normalized;
    emit annotationColorChanged(m_currentColorId);
}

void VideoDisplayWidget::initAudioSinkIfNeeded(int sampleRate, int channels, int bitsPerSample)
{
    QAudioDevice desiredOut = QMediaDevices::defaultAudioOutput();
    if (!m_followSystemOutput && !m_outputDeviceId.isEmpty()) {
        const auto devs = QMediaDevices::audioOutputs();
        for (const auto &d : devs) {
            if (d.id() == m_outputDeviceId) { desiredOut = d; break; }
        }
    }

    bool deviceChanged = (m_currentOutputDeviceId != desiredOut.id());
    bool needsReinit = deviceChanged || (!m_audioInitialized) ||
                       (m_audioFormat.sampleRate() != sampleRate) ||
                       (m_audioFormat.channelCount() != channels) ||
                       (m_audioFormat.sampleFormat() != QAudioFormat::Int16);

    if (!needsReinit && m_audioSink) {
        return;
    }

    // 停止并释放旧的输出
    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
        m_audioIO = nullptr;
    }

    // 配置音频格式
    m_audioFormat = QAudioFormat();
    m_audioFormat.setSampleRate(sampleRate);
    m_audioFormat.setChannelCount(channels);
    Q_UNUSED(bitsPerSample);
    m_audioFormat.setSampleFormat(QAudioFormat::Int16);
    if (!desiredOut.isFormatSupported(m_audioFormat)) {
        QAudioFormat pf = desiredOut.preferredFormat();
        m_audioFormat = pf;
    }
    m_sinkSampleRate = m_audioFormat.sampleRate();
    m_sinkChannels = m_audioFormat.channelCount();
    m_needResample = (m_sinkSampleRate != sampleRate) || (m_sinkChannels != channels);

    // 创建音频输出
    m_audioSink = new QAudioSink(desiredOut, m_audioFormat, this);
    m_audioSink->setBufferSize(4096);
    m_currentOutputDeviceId = desiredOut.id();
    
    if (m_speakerEnabled) {
        m_audioIO = m_audioSink->start();
        m_audioSink->setVolume(qBound(0.0, m_volumePercent / 100.0, 1.0));
        
    } else {
        m_audioIO = nullptr;
    }
    m_audioInitialized = true;
}

void VideoDisplayWidget::selectAudioOutputFollowSystem()
{
    m_followSystemOutput = true;
    m_outputDeviceId.clear();
    
    int sr = m_audioFormat.sampleRate() > 0 ? m_audioFormat.sampleRate() : 16000;
    int ch = m_audioFormat.channelCount() > 0 ? m_audioFormat.channelCount() : 1;
    initAudioSinkIfNeeded(sr, ch, 16);
    emit audioOutputSelectionChanged(true, QString());
}

void VideoDisplayWidget::selectAudioOutputById(const QString &id)
{
    m_followSystemOutput = false;
    m_outputDeviceId = id.toUtf8();
    m_currentOutputDeviceId.clear();
    m_audioInitialized = false;
    
    int sr = m_audioFormat.sampleRate() > 0 ? m_audioFormat.sampleRate() : 16000;
    int ch = m_audioFormat.channelCount() > 0 ? m_audioFormat.channelCount() : 1;
    initAudioSinkIfNeeded(sr, ch, 16);
    softRestartSpeakerIfEnabled();
    emit audioOutputSelectionChanged(false, id);
}

void VideoDisplayWidget::selectAudioOutputByRawId(const QByteArray &id)
{
    m_followSystemOutput = false;
    m_outputDeviceId = id;
    m_currentOutputDeviceId.clear();
    m_audioInitialized = false;
    
    int sr = m_audioFormat.sampleRate() > 0 ? m_audioFormat.sampleRate() : 16000;
    int ch = m_audioFormat.channelCount() > 0 ? m_audioFormat.channelCount() : 1;
    initAudioSinkIfNeeded(sr, ch, 16);
    softRestartSpeakerIfEnabled();
    emit audioOutputSelectionChanged(false, QString::fromUtf8(id));
}

void VideoDisplayWidget::selectMicInputFollowSystem()
{
    if (m_receiver) {
        m_receiver->setLocalInputDeviceFollowSystem();
    }
    emit micInputSelectionChanged(true, QString());
}

void VideoDisplayWidget::selectMicInputById(const QString &id)
{
    if (m_receiver) {
        m_receiver->setLocalInputDeviceById(id);
    }
    emit micInputSelectionChanged(false, id);
}

bool VideoDisplayWidget::isMicInputFollowSystem() const
{
    if (!m_receiver) return true;
    return m_receiver->isLocalInputFollowSystem();
}

QString VideoDisplayWidget::currentMicInputDeviceId() const
{
    if (!m_receiver) return QString();
    return m_receiver->localInputDeviceId();
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
    static QPixmap cursorPixmap;
    static bool loaded = false;
    if (!loaded) {
        QString appDir = QCoreApplication::applicationDirPath();
        QString iconDir = appDir + "/maps/logo";
        QString file = iconDir + "/cursor.png";
        if (QFile::exists(file)) {
            cursorPixmap.load(file);
        }
        loaded = true;
    }
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (!cursorPixmap.isNull()) {
        painter.drawPixmap(position, cursorPixmap);
    }
}

// ==================== 瓦片渲染相关方法实现 ====================

void VideoDisplayWidget::renderTile(int tileId, const QByteArray &tileData, const QRect &sourceRect, qint64 timestamp)
{
    QWriteLocker locker(&m_tileReadWriteLock);
    
    // 创建瓦片数据
    TileData tile;
    tile.tileId = tileId;
    tile.sourceRect = sourceRect;
    tile.targetRect = calculateTileTargetRect(tileId);
    tile.timestamp = timestamp;
    tile.isDirty = true;
    
    // 将字节数据转换为QPixmap
    QPixmap pixmap;
    if (pixmap.loadFromData(tileData)) {
        tile.pixmap = pixmap;
        
        // 更新瓦片映射
        m_tileComposition.tiles[tileId] = tile;
        m_tileComposition.lastUpdateTime = QDateTime::currentMSecsSinceEpoch();
        
        // 更新统计信息
        m_stats.totalTiles = m_tileComposition.tiles.size();
        m_stats.lastTileUpdate = timestamp;
        
        
        
        // 触发合成
        if (m_tileMode) {
            composeTiles();
        }
    } else {
        
    }
}

void VideoDisplayWidget::updateTile(int tileId, const QByteArray &deltaData, const QRect &updateRect, qint64 timestamp)
{
    QWriteLocker locker(&m_tileReadWriteLock);
    
    auto it = m_tileComposition.tiles.find(tileId);
    if (it != m_tileComposition.tiles.end()) {
        TileData &tile = it.value();
        
        // 应用增量更新（这里简化处理，实际应该应用差分数据）
        QPixmap deltaPixmap;
        if (deltaPixmap.loadFromData(deltaData)) {
            // 在现有瓦片上绘制增量数据
            QPainter painter(&tile.pixmap);
            painter.drawPixmap(updateRect.topLeft(), deltaPixmap);
            
            tile.timestamp = timestamp;
            tile.isDirty = true;
            
            
            // 触发合成
            if (m_tileMode) {
                composeTiles();
            }
        }
    } else {
    }
}

void VideoDisplayWidget::setTileConfiguration(const QSize &frameSize, const QSize &tileSize)
{
    QWriteLocker locker(&m_tileReadWriteLock);
    
    m_tileComposition.frameSize = frameSize;
    m_tileComposition.tileSize = tileSize;
    m_tileComposition.tilesPerRow = (frameSize.width() + tileSize.width() - 1) / tileSize.width();
    m_tileComposition.tilesPerColumn = (frameSize.height() + tileSize.height() - 1) / tileSize.height();
    
    // 清空现有瓦片
    m_tileComposition.tiles.clear();
    
    // 初始化合成帧
    m_compositeFrame = QPixmap(frameSize);
    m_compositeFrame.fill(Qt::black);
    
    
}

void VideoDisplayWidget::clearTiles()
{
    QWriteLocker locker(&m_tileReadWriteLock);
    
    m_tileComposition.tiles.clear();
    m_compositeFrame.fill(Qt::black);
    
    // 重置统计信息
    m_stats.totalTiles = 0;
    m_stats.activeTiles = 0;
    m_stats.dirtyTiles = 0;
    
    
}

void VideoDisplayWidget::composeTiles()
{
    if (m_compositionInProgress.exchange(true)) {
        return; // 已经在合成中，避免重复合成
    }
    
    QReadLocker locker(&m_tileReadWriteLock);
    
    if (m_compositeFrame.isNull()) {
        m_compositionInProgress = false;
        return;
    }
    
    QPainter painter(&m_compositeFrame);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    int dirtyCount = 0;
    int activeCount = 0;
    
    // 遍历所有瓦片并合成
    for (auto it = m_tileComposition.tiles.begin(); it != m_tileComposition.tiles.end(); ++it) {
        const TileData &tile = it.value();
        activeCount++;
        
        if (tile.isDirty) {
            dirtyCount++;
            painter.drawPixmap(tile.targetRect, tile.pixmap);
            
            // 标记为已处理（注意：这里需要去掉const才能修改）
            const_cast<TileData&>(tile).isDirty = false;
        }
    }
    
    // 更新统计信息
    m_stats.activeTiles = activeCount;
    m_stats.dirtyTiles = dirtyCount;
    
    // 远端叠加层已绘制所有鼠标，无需在观看端再叠加
    
    // 更新显示
    QMetaObject::invokeMethod(this, [this]() {
        if (m_videoLabel && !m_compositeFrame.isNull()) {
            QPixmap scaledPixmap = m_compositeFrame.scaled(m_videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            m_videoLabel->setPixmap(scaledPixmap);
            m_stats.framesDisplayed++;
            stopWaitingSplash();
        }
    }, Qt::QueuedConnection);
    
    m_compositionInProgress = false;
}

QRect VideoDisplayWidget::calculateTileTargetRect(int tileId) const
{
    if (m_tileComposition.tilesPerRow <= 0) {
        return QRect();
    }
    
    int row = tileId / m_tileComposition.tilesPerRow;
    int col = tileId % m_tileComposition.tilesPerRow;
    
    int x = col * m_tileComposition.tileSize.width();
    int y = row * m_tileComposition.tileSize.height();
    
    return QRect(x, y, m_tileComposition.tileSize.width(), m_tileComposition.tileSize.height());
}

QRect VideoDisplayWidget::calculateTileSourceRect(int tileId) const
{
    // 源区域与目标区域相同（1:1映射）
    return calculateTileTargetRect(tileId);
}

void VideoDisplayWidget::markTilesDirty()
{
    QWriteLocker locker(&m_tileReadWriteLock);
    
    for (auto it = m_tileComposition.tiles.begin(); it != m_tileComposition.tiles.end(); ++it) {
        it.value().isDirty = true;
    }
}

void VideoDisplayWidget::cleanupOldTiles()
{
    QWriteLocker locker(&m_tileReadWriteLock);
    
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
    auto it = m_tileComposition.tiles.begin();
    while (it != m_tileComposition.tiles.end()) {
        if (currentTime - it.value().timestamp > m_tileTimeout) {
            it = m_tileComposition.tiles.erase(it);
        } else {
            ++it;
        }
    }
    
    // 更新统计信息
    m_stats.totalTiles = m_tileComposition.tiles.size();
}

// 将标签坐标映射到源帧坐标（考虑保持比例与居中）
QPoint VideoDisplayWidget::mapLabelToSource(const QPoint &labelPoint) const
{
    if (!m_videoLabel) return QPoint(-1, -1);
    const QSize frameSize = m_stats.frameSize;
    const QSize labelSize = m_videoLabel->size();
    if (frameSize.isEmpty() || labelSize.isEmpty()) return QPoint(-1, -1);

    const double sx = static_cast<double>(labelSize.width()) / static_cast<double>(frameSize.width());
    const double sy = static_cast<double>(labelSize.height()) / static_cast<double>(frameSize.height());
    const double scale = (sx < sy ? sx : sy);
    const int scaledW = static_cast<int>(frameSize.width() * scale);
    const int scaledH = static_cast<int>(frameSize.height() * scale);
    const int offsetX = (labelSize.width() - scaledW) / 2;
    const int offsetY = (labelSize.height() - scaledH) / 2;

    // 若鼠标在黑边区域，则认为不在视频区域内
    if (labelPoint.x() < offsetX || labelPoint.y() < offsetY ||
        labelPoint.x() > offsetX + scaledW || labelPoint.y() > offsetY + scaledH) {
        return QPoint(-1, -1);
    }

    const double srcXf = (static_cast<double>(labelPoint.x() - offsetX)) / scale;
    const double srcYf = (static_cast<double>(labelPoint.y() - offsetY)) / scale;
    int srcX = static_cast<int>(srcXf);
    int srcY = static_cast<int>(srcYf);

    // 夹取到源帧范围内
    if (srcX < 0) srcX = 0;
    if (srcY < 0) srcY = 0;
    if (srcX >= frameSize.width()) srcX = frameSize.width() - 1;
    if (srcY >= frameSize.height()) srcY = frameSize.height() - 1;

    return QPoint(srcX, srcY);
}

// 捕获鼠标事件并发送批注事件
bool VideoDisplayWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_videoLabel) {
        switch (event->type()) {
        case QEvent::MouseButtonDblClick: {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            bool isPrimary   = (!m_mouseButtonsSwapped && me->button() == Qt::LeftButton) ||
                               (m_mouseButtonsSwapped && me->button() == Qt::RightButton);
            if (isPrimary) {
                emit fullscreenToggleRequested();
                return true; // 消费双击事件，避免影响批注
            }
            break;
        }
        case QEvent::MouseButtonPress: {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            // 次键：清屏（考虑系统交换左右键）
            bool isSecondary = (!m_mouseButtonsSwapped && me->button() == Qt::RightButton) ||
                               (m_mouseButtonsSwapped && me->button() == Qt::LeftButton);
            bool isPrimary   = (!m_mouseButtonsSwapped && me->button() == Qt::LeftButton) ||
                               (m_mouseButtonsSwapped && me->button() == Qt::RightButton);
            if (isSecondary) {
                // 右键弹出上下文菜单
                QMenu menu(this);
                QAction *clearAction = menu.addAction(QStringLiteral("清理绘制"));
                QAction *switchAction = menu.addAction(QStringLiteral("切换屏幕"));
                // 质量子菜单
                QMenu *qualityMenu = menu.addMenu(QStringLiteral("质量"));
                QAction *qualityHigh = qualityMenu->addAction(QStringLiteral("高"));
                QAction *qualityMedium = qualityMenu->addAction(QStringLiteral("中"));
                QAction *qualityLow = qualityMenu->addAction(QStringLiteral("低"));
                QAction *chosen = menu.exec(me->globalPosition().toPoint());
                if (chosen == clearAction) {
                    sendClear();
                } else if (chosen == switchAction) {
                    // 显示切换提示并发送切屏请求
                    showSwitchingIndicator(QStringLiteral("切换中..."));
                    if (m_receiver) {
                        m_receiver->sendSwitchScreenNext();
                    }
                } else if (chosen == qualityHigh) {
                    if (m_receiver) m_receiver->sendSetQuality("high");
                } else if (chosen == qualityMedium) {
                    if (m_receiver) m_receiver->sendSetQuality("medium");
                } else if (chosen == qualityLow) {
                    if (m_receiver) m_receiver->sendSetQuality("low");
                }
                m_isAnnotating = false;
                return true; // 消费右键事件
            }
            QPoint src = mapLabelToSource(me->pos());
            if (src.x() >= 0 && isPrimary) {
                if (m_toolMode == 0 && m_annotationEnabled) {
                    m_isAnnotating = true;
                    if (m_receiver) {
                        m_receiver->sendAnnotationEvent("down", src.x(), src.y(), m_currentColorId);
                    }
                } else if (m_toolMode == 1) {
                    m_isAnnotating = true;
                    if (m_receiver) {
                        m_receiver->sendAnnotationEvent("erase_down", src.x(), src.y(), m_currentColorId);
                    }
                } else if (m_toolMode == 2) {
                    m_isAnnotating = true;
                    if (m_receiver) {
                        m_receiver->sendAnnotationEvent("rect_down", src.x(), src.y(), m_currentColorId);
                    }
                } else if (m_toolMode == 3) {
                    m_isAnnotating = true;
                    if (m_receiver) {
                        m_receiver->sendAnnotationEvent("circle_down", src.x(), src.y(), m_currentColorId);
                    }
                } else if (m_toolMode == 4) {
                    if (m_receiver) {
                        m_lastTextSrcPoint = src;
                        QLineEdit *edit = new QLineEdit(m_videoLabel);
                        QFont f; f.setPixelSize(m_textFontSize);
                        edit->setFont(f);
                        edit->setText("");
                        edit->setMinimumWidth(80);
                        edit->resize(200, m_textFontSize + 10);
                        edit->move(me->pos());
                        edit->setStyleSheet("QLineEdit { background: rgba(0,0,0,120); color: white; border: 1px solid rgba(255,255,255,180); padding: 2px; } ");
                        edit->setFocusPolicy(Qt::StrongFocus);
                        edit->setFocus();
                        edit->show();
                        connect(edit, &QLineEdit::editingFinished, this, [this, edit]() {
                            QString txt = edit->text();
                            if (!txt.isEmpty() && m_receiver) {
                                m_receiver->sendTextAnnotation(txt, m_lastTextSrcPoint.x(), m_lastTextSrcPoint.y(), m_currentColorId, m_textFontSize);
                            }
                            edit->deleteLater();
                        });
                    }
                } else if (m_toolMode == 5) {
                    m_isAnnotating = true;
                    if (m_receiver) {
                        m_receiver->sendAnnotationEvent("arrow_down", src.x(), src.y(), m_currentColorId);
                    }
                }
            }
            return true; // 消费事件
        }
        case QEvent::MouseMove: {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (m_isAnnotating) {
                QPoint src = mapLabelToSource(me->pos());
                if (src.x() >= 0 && m_receiver) {
                    if (m_toolMode == 0) {
                        m_receiver->sendAnnotationEvent("move", src.x(), src.y(), m_currentColorId);
                    } else if (m_toolMode == 1) {
                        m_receiver->sendAnnotationEvent("erase_move", src.x(), src.y(), m_currentColorId);
                    } else if (m_toolMode == 2) {
                        m_receiver->sendAnnotationEvent("rect_move", src.x(), src.y(), m_currentColorId);
                    } else if (m_toolMode == 3) {
                        m_receiver->sendAnnotationEvent("circle_move", src.x(), src.y(), m_currentColorId);
                    } else if (m_toolMode == 5) {
                        m_receiver->sendAnnotationEvent("arrow_move", src.x(), src.y(), m_currentColorId);
                    }
                }
                return true;
            }
            {
                QPoint src = mapLabelToSource(me->pos());
                if (src.x() >= 0 && m_receiver) {
                    m_receiver->sendViewerCursor(src.x(), src.y());
                }
            }
            break;
        }
        case QEvent::Leave: {
            if (m_localCursorOverlay) m_localCursorOverlay->hide();
            break;
        }
        case QEvent::MouseButtonRelease: {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            QPoint src = mapLabelToSource(me->pos());
            bool isPrimary   = (!m_mouseButtonsSwapped && me->button() == Qt::LeftButton) ||
                               (m_mouseButtonsSwapped && me->button() == Qt::RightButton);
            if (m_isAnnotating && src.x() >= 0 && isPrimary) {
                if (m_receiver) {
                    if (m_toolMode == 0) {
                        m_receiver->sendAnnotationEvent("up", src.x(), src.y(), m_currentColorId);
                    } else if (m_toolMode == 1) {
                        m_receiver->sendAnnotationEvent("erase_up", src.x(), src.y(), m_currentColorId);
                    } else if (m_toolMode == 2) {
                        m_receiver->sendAnnotationEvent("rect_up", src.x(), src.y(), m_currentColorId);
                    } else if (m_toolMode == 3) {
                        m_receiver->sendAnnotationEvent("circle_up", src.x(), src.y(), m_currentColorId);
                    } else if (m_toolMode == 5) {
                        m_receiver->sendAnnotationEvent("arrow_up", src.x(), src.y(), m_currentColorId);
                    }
                }
            }
            m_isAnnotating = false;
            return true;
        }
        case QEvent::ContextMenu: {
            // 键盘菜单键或长按触发的上下文菜单
            QContextMenuEvent *ce = static_cast<QContextMenuEvent*>(event);
            QMenu menu(this);
            QAction *clearAction = menu.addAction(QStringLiteral("清理绘制"));
            QAction *switchAction = menu.addAction(QStringLiteral("切换屏幕"));
            QMenu *qualityMenu = menu.addMenu(QStringLiteral("质量"));
            QAction *qualityHigh = qualityMenu->addAction(QStringLiteral("高"));
            QAction *qualityMedium = qualityMenu->addAction(QStringLiteral("中"));
            QAction *qualityLow = qualityMenu->addAction(QStringLiteral("低"));
            QAction *chosen = menu.exec(ce->globalPos());
            if (chosen == clearAction) {
                sendClear();
            } else if (chosen == switchAction) {
                showSwitchingIndicator(QStringLiteral("切换中..."));
                if (m_receiver) {
                    m_receiver->sendSwitchScreenNext();
                }
            } else if (chosen == qualityHigh) {
                if (m_receiver) m_receiver->sendSetQuality("high");
            } else if (chosen == qualityMedium) {
                if (m_receiver) m_receiver->sendSetQuality("medium");
            } else if (chosen == qualityLow) {
                if (m_receiver) m_receiver->sendSetQuality("low");
            }
            m_isAnnotating = false;
            return true;
        }
        case QEvent::KeyPress: {
            QKeyEvent *ke = static_cast<QKeyEvent*>(event);
            if ((ke->modifiers() & Qt::ControlModifier) && ke->key() == Qt::Key_Z) {
                if (m_receiver) {
    m_receiver->sendAnnotationEvent("undo", 0, 0, m_currentColorId);
                }
                return true; // 消费Ctrl+Z
            }
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(obj, event);
}

// 撤销上一笔
void VideoDisplayWidget::sendUndo()
{
    if (m_receiver) {
    m_receiver->sendAnnotationEvent("undo", 0, 0, m_currentColorId);
    }
}

// 清屏
void VideoDisplayWidget::sendClear()
{
    if (m_receiver) {
    m_receiver->sendAnnotationEvent("clear", 0, 0, m_currentColorId);
    }
    m_isAnnotating = false;
}

void VideoDisplayWidget::sendCloseOverlay()
{
    if (m_receiver) {
    m_receiver->sendAnnotationEvent("overlay_close", 0, 0, m_currentColorId);
    }
}

void VideoDisplayWidget::sendLike()
{
    if (m_receiver) {
        m_receiver->sendLikeEvent();
    }
}

QImage VideoDisplayWidget::captureToImage() const
{
    if (!m_videoLabel) return QImage();
    QPixmap pm = m_videoLabel->grab();
    return pm.toImage();
}

void VideoDisplayWidget::closeEvent(QCloseEvent *event)
{
    // 关闭窗口时，主动停止接收并断开连接（会发送 stop_streaming）
    stopReceiving();
    QWidget::closeEvent(event);
}

// ====== 继续观看提示与倒计时 ======
void VideoDisplayWidget::setupContinuePrompt()
{
    if (!m_continuePromptTimer) {
        m_continuePromptTimer = new QTimer(this);
        m_continuePromptTimer->setSingleShot(false);
        // 读取环境变量 IRULER_PROMPT_MINUTES（整数，单位分钟），用于测试覆盖
        bool ok = false;
        int envMin = qEnvironmentVariableIntValue("IRULER_PROMPT_MINUTES", &ok);
        if (ok && envMin > 0) {
            m_promptIntervalMinutes = envMin;
        } else {
            m_promptIntervalMinutes = 30;
        }
        m_continuePromptTimer->setInterval(m_promptIntervalMinutes * 60 * 1000);
        connect(m_continuePromptTimer, &QTimer::timeout, this, &VideoDisplayWidget::showContinuePrompt);
    }
}

void VideoDisplayWidget::showContinuePrompt()
{
    // 仅在接收中且窗口可见时提示
    if (!m_isReceiving || !isVisible()) {
        return;
    }

    // 防止重入：展示弹窗期间暂停周期定时器，结束后再恢复
    if (m_continuePromptTimer) {
        m_continuePromptTimer->stop();
    }

    if (!m_promptDialog) {
        m_promptDialog = new QDialog(this);
        m_promptDialog->setWindowTitle(QStringLiteral("是否继续观看"));
        m_promptDialog->setModal(true);
        m_promptDialog->setWindowFlags(m_promptDialog->windowFlags() | Qt::WindowStaysOnTopHint);
        auto *layout = new QVBoxLayout(m_promptDialog);
        m_promptLabel = new QLabel(m_promptDialog);
        m_continueButton = new QPushButton(QStringLiteral("继续观看"), m_promptDialog);
        layout->addWidget(m_promptLabel);
        layout->addWidget(m_continueButton);
        connect(m_continueButton, &QPushButton::clicked, this, &VideoDisplayWidget::onContinueClicked);
    }

    m_remainingSeconds = 10;
    m_promptLabel->setText(QStringLiteral("是否继续观看？%1秒后将自动关闭此窗口").arg(m_remainingSeconds));

    if (!m_promptCountdownTimer) {
        m_promptCountdownTimer = new QTimer(this);
        m_promptCountdownTimer->setInterval(1000);
        connect(m_promptCountdownTimer, &QTimer::timeout, this, &VideoDisplayWidget::onPromptCountdownTick);
    }
    m_promptCountdownTimer->start();
    m_promptDialog->resize(320, 120);
    m_promptDialog->show();
}

void VideoDisplayWidget::onContinueClicked()
{
    if (m_promptCountdownTimer) {
        m_promptCountdownTimer->stop();
    }
    if (m_promptDialog) {
        m_promptDialog->hide();
    }
    // 恢复周期定时器，等待下一个周期再提示
    if (m_continuePromptTimer) {
        m_continuePromptTimer->setInterval(m_promptIntervalMinutes * 60 * 1000);
        m_continuePromptTimer->start();
    }
}

void VideoDisplayWidget::setAnnotationEnabled(bool enabled)
{
    m_annotationEnabled = enabled;
    if (m_localCursorOverlay) m_localCursorOverlay->setVisible(!enabled);
    applyCursor();
}

void VideoDisplayWidget::setToolMode(int mode)
{
    int m = mode;
    if (m < 0) m = 0;
    if (m > 5) m = 5;
    m_toolMode = m;
    applyCursor();
}

void VideoDisplayWidget::applyCursor()
{
    if (!m_videoLabel) return;
    if (!m_annotationEnabled) {
        m_videoLabel->setCursor(Qt::ArrowCursor);
        return;
    }
    if (m_toolMode == 1) {
        int r = 20;
        QPixmap pix(2*r + 2, 2*r + 2);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor(255, 255, 255, 230));
        pen.setWidth(2);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPoint(r + 1, r + 1), r, r);
        QCursor cur(pix, r + 1, r + 1);
        m_videoLabel->setCursor(cur);
    } else if (m_toolMode == 2 || m_toolMode == 3 || m_toolMode == 5) {
        m_videoLabel->setCursor(Qt::CrossCursor);
    } else if (m_toolMode == 4) {
        m_videoLabel->setCursor(Qt::IBeamCursor);
    } else {
        m_videoLabel->setCursor(Qt::ArrowCursor);
    }
}

double VideoDisplayWidget::currentScale() const
{
    QSize labelSize = m_videoLabel ? m_videoLabel->size() : QSize();
    QSize frameSize;
    if (m_tileMode && !m_tileComposition.frameSize.isEmpty()) {
        frameSize = m_tileComposition.frameSize;
    } else {
        frameSize = m_stats.frameSize;
    }
    if (labelSize.isEmpty() || frameSize.isEmpty()) {
        return 1.0;
    }
    double sx = double(labelSize.width()) / double(frameSize.width());
    double sy = double(labelSize.height()) / double(frameSize.height());
    double s = (sx < sy) ? sx : sy;
    if (s <= 0.0) s = 1.0;
    return s;
}

void VideoDisplayWidget::setTextFontSize(int size)
{
    int s = size;
    if (s < 8) s = 8;
    if (s > 72) s = 72;
    m_textFontSize = s;
}

void VideoDisplayWidget::onPromptCountdownTick()
{
    m_remainingSeconds -= 1;
    if (m_remainingSeconds < 0) m_remainingSeconds = 0;
    if (m_promptLabel) {
        m_promptLabel->setText(QStringLiteral("是否继续观看？%1秒后将自动关闭此窗口").arg(m_remainingSeconds));
    }
    if (m_remainingSeconds == 0) {
        if (m_promptCountdownTimer) {
            m_promptCountdownTimer->stop();
        }
        if (m_promptDialog) {
            m_promptDialog->hide();
        }
        // 倒计时结束：将自动关闭此窗口
        stopReceiving();
        QWidget *top = this->window();
        // 统一改为隐藏顶层窗口，避免关闭整个主程序
        if (top) {
            top->hide();
        } else {
            this->hide();
        }
    }
}
void VideoDisplayWidget::recreateReceiver()
{
    // 删除旧接收器实例
    m_receiver.reset();
    // 创建新实例并重新连接信号
    m_receiver = std::make_unique<WebSocketReceiver>();
    // 日志清理：移除接收器重建提示

    connect(m_receiver.get(), &WebSocketReceiver::frameReceivedWithTimestamp,
            this, [this](const QByteArray &frameData, qint64 captureTimestamp) {
                m_stats.framesReceived++;
                m_currentCaptureTimestamp = captureTimestamp;
                m_decoder->decodeFrame(frameData);
            });

    connect(m_receiver.get(), &WebSocketReceiver::connectionStatusChanged,
            this, &VideoDisplayWidget::updateConnectionStatus);

    connect(m_receiver.get(), &WebSocketReceiver::mousePositionReceived,
            this, &VideoDisplayWidget::onMousePositionReceived);

    connect(m_receiver.get(), &WebSocketReceiver::tileCompleted,
            this, [this](int tileId, const QByteArray &completeData) {
                QRect sourceRect = calculateTileSourceRect(tileId);
                qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
                renderTile(tileId, completeData, sourceRect, timestamp);
            });

    connect(m_receiver.get(), &WebSocketReceiver::tileUpdateReceived,
            this, [this](const WebSocketReceiver::TileUpdate &update) {
                QRect updateRect(update.x, update.y, update.width, update.height);
                updateTile(update.tileId, update.deltaData, updateRect, update.timestamp);
            });

    connect(m_receiver.get(), &WebSocketReceiver::tileMetadataReceived,
            this, [this](const WebSocketReceiver::TileMetadata &metadata) {
        if (m_tileComposition.frameSize.isEmpty()) {
            QSize frameSize(metadata.x + metadata.width, metadata.y + metadata.height);
            QSize tileSize(metadata.width, metadata.height);
            // 日志清理：移除瓦片配置打印
            setTileConfiguration(frameSize, tileSize);
            setTileMode(true);
        }
    });

    
    connect(m_receiver.get(), &WebSocketReceiver::connected, this, [this]() {
        if (!m_lastViewerId.isEmpty() && !m_lastTargetId.isEmpty()) {
            m_receiver->sendWatchRequest(m_lastViewerId, m_lastTargetId);
        }
    });
    if (m_audioConn) QObject::disconnect(m_audioConn);
    if (m_speakerEnabled) {
        m_audioConn = connect(m_receiver.get(), &WebSocketReceiver::audioFrameReceived,
            this, [this](const QByteArray &pcmData, int sampleRate, int channels, int bitsPerSample, qint64) {
                if (!m_isReceiving) return;
                if (!m_speakerEnabled) return;
                initAudioSinkIfNeeded(sampleRate, channels, bitsPerSample);
                if (!m_audioSink) return;
                if (m_audioSink->state() == QAudio::StoppedState) {
                    m_audioIO = m_audioSink->start();
                }
                if (m_audioSink->state() == QAudio::SuspendedState) {
                    m_audioSink->resume();
                }
                if (m_audioIO) m_audioIO->write(pcmData);
            });
    }
}
void VideoDisplayWidget::setVolumePercent(int percent)
{
    int p = percent;
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    m_volumePercent = p;
    if (m_audioSink) {
        m_audioSink->setVolume(qBound(0.0, m_volumePercent / 100.0, 1.0));
    }
}
void VideoDisplayWidget::setMicGainPercent(int percent)
{
    int p = percent;
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    m_micGainPercent = p;
    if (m_receiver) {
        m_receiver->sendAudioGain(m_micGainPercent);
    }
}
void VideoDisplayWidget::setTalkEnabled(bool enabled)
{
    if (m_receiver) { m_receiver->setTalkEnabled(enabled); }
}

void VideoDisplayWidget::pauseReceiving()
{
    // 保留WebSocket连接，仅通知采集端停止推流，便于下次快速恢复
    if (m_receiver && m_isReceiving) {
        m_receiver->sendStopStreaming();
        // 为确保服务器侧流量归零，同时断开订阅连接
        m_receiver->disconnectFromServer();
    }
    // 停止音频播放
    if (m_audioSink) {
        if (m_audioSink->state() != QAudio::StoppedState) {
            m_audioSink->stop();
        }
    }
    m_audioIO = nullptr;
    m_audioInitialized = false;
    // 清理瓦片显示，但不更改连接状态
    clearTiles();
    setTileMode(false);
    m_compositeFrame = QPixmap();
    // 显示等待图并更新状态
    m_videoLabel->clear();
    showWaitingSplash();
    m_stats.connectionStatus = "Disconnected";
    emit connectionStatusChanged(m_stats.connectionStatus);
    m_isReceiving = false;
    updateButtonText();
}

void VideoDisplayWidget::updateLocalCursorComposite()
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString iconDir = appDir + "/maps/logo";
    QString file = iconDir + "/cursor.png";
    if (m_cursorBase.isNull()) {
        if (QFile::exists(file)) {
            m_cursorBase.load(file);
        }
    }
    if (!m_cursorBase.isNull()) {
        int w = qMax(8, int(m_cursorBase.width() * 0.5));
        int h = qMax(8, int(m_cursorBase.height() * 0.5));
        m_cursorSmall = m_cursorBase.scaled(QSize(w, h), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPixmap base = m_cursorSmall.isNull() ? m_cursorBase : m_cursorSmall;
        if (!m_localCursorColor.isValid()) {
            quint32 seed = qHash(m_viewerName);
            QRandomGenerator gen(seed);
            int r = gen.bounded(40, 216);
            int g = gen.bounded(40, 216);
            int b = gen.bounded(40, 216);
            m_localCursorColor = QColor(r, g, b);
        }
        QPixmap tint = base.copy();
        QPainter tp(&tint);
        tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tp.fillRect(tint.rect(), m_localCursorColor);
        tp.end();
        QFont f; f.setPixelSize(qMax(12, base.height() / 2));
        QFontMetrics fm(f);
        int tw = fm.horizontalAdvance(m_viewerName);
        int th = fm.height();
        int cw = base.width() + 6 + tw;
        int ch = qMax(base.height(), th + 4);
        QPixmap comp(cw, ch);
        comp.fill(Qt::transparent);
        QPainter cp(&comp);
        cp.setRenderHint(QPainter::Antialiasing, true);
        cp.drawPixmap(0, 0, tint);
        cp.setFont(f);
        cp.setPen(QPen(m_localCursorColor));
        cp.drawText(QPoint(base.width() + 6, base.height() - 4), m_viewerName);
        cp.end();
        m_cursorComposite = comp;
    }
}
void VideoDisplayWidget::updateScaledCursorComposite(double scale)
{
    if (scale <= 0.0) scale = 1.0;
    int w = qMax(8, int(m_cursorComposite.width() * scale));
    int h = qMax(8, int(m_cursorComposite.height() * scale));
    m_cursorScaledComposite = m_cursorComposite.scaled(QSize(w, h), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_lastCursorScale = scale;
}
QByteArray VideoDisplayWidget::convertForSink(const QByteArray &srcPcm, int srcSr, int srcCh) const
{
    if (srcPcm.isEmpty()) return srcPcm;
    const int16_t *in = reinterpret_cast<const int16_t*>(srcPcm.constData());
    int totalSamples = srcPcm.size() / 2;
    int srcFrames = srcCh > 0 ? totalSamples / srcCh : 0;
    if (srcFrames <= 0) return srcPcm;
    int dstFrames = int(std::llround(double(srcFrames) * double(m_sinkSampleRate) / double(srcSr)));
    if (dstFrames <= 0) return QByteArray();
    int dstCh = m_sinkChannels;
    QByteArray out;
    out.resize(dstFrames * dstCh * 2);
    int16_t *outp = reinterpret_cast<int16_t*>(out.data());
    for (int f = 0; f < dstFrames; ++f) {
        double pos = double(f) * double(srcSr) / double(m_sinkSampleRate);
        int i = int(pos);
        if (i >= srcFrames) i = srcFrames - 1;
        double frac = pos - double(i);
        auto sampleAt = [&](int chIndex)->int {
            int i0 = i * srcCh + chIndex;
            int i1 = (i + 1 < srcFrames ? (i + 1) : i) * srcCh + chIndex;
            int s0 = in[i0];
            int s1 = in[i1];
            int s = int(std::llround(double(s0) + (double(s1 - s0) * frac)));
            if (s > 32767) s = 32767; if (s < -32768) s = -32768;
            return s;
        };
        int l = (srcCh == 1) ? sampleAt(0) : sampleAt(0);
        int r = (srcCh == 1) ? l : sampleAt(1);
        if (dstCh == 1) {
            int m = (srcCh == 2) ? ((l + r) / 2) : l;
            outp[f] = int16_t(m);
        } else {
            outp[f * 2] = int16_t(l);
            outp[f * 2 + 1] = int16_t(r);
        }
    }
    return out;
}
void VideoDisplayWidget::softRestartSpeakerIfEnabled()
{
    if (!m_speakerEnabled) return;
    if (!m_audioSink) return;
    m_audioSink->stop();
    m_audioIO = m_audioSink->start();
    m_audioSink->setVolume(qBound(0.0, m_volumePercent / 100.0, 1.0));
}
void VideoDisplayWidget::forceRecreateSink()
{
    if (!m_speakerEnabled) return;
    m_audioInitialized = false;
    m_currentOutputDeviceId.clear();
    if (m_audioSink) { m_audioSink->stop(); delete m_audioSink; m_audioSink = nullptr; m_audioIO = nullptr; }
    initAudioSinkIfNeeded(m_lastFrameSampleRate, m_lastFrameChannels, m_lastFrameBitsPerSample);
    softRestartSpeakerIfEnabled();
}