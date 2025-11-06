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
    , m_tileMode(false)
    , m_tileTimeout(5000) // 5秒超时
    , m_tileCleanupTimer(nullptr)
    , m_compositionInProgress(false)
    , m_compositionTimer(nullptr)
    , m_batchUpdatesEnabled(true)
    , m_lastCompositionTime(0)
    , m_maxCompositionFPS(60)
{
    setupUI();
    setupStatusBar();
    
    memset(&m_stats, 0, sizeof(m_stats));
    
    // 初始化瓦片组合结构
    m_tileComposition.frameSize = QSize(0, 0);
    m_tileComposition.tileSize = QSize(256, 256); // 默认瓦片大小
    m_tileComposition.tilesPerRow = 0;
    m_tileComposition.tilesPerColumn = 0;
    m_tileComposition.lastUpdateTime = 0;
    
    // 设置显示更新定时器
    m_displayTimer = new QTimer(this);
    connect(m_displayTimer, &QTimer::timeout, this, &VideoRenderer::updateDisplay);
    m_displayTimer->start(16); // ~60 FPS，极高流畅度，减少画面跳动
    
    // 设置统计更新定时器
    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &VideoRenderer::updateStats);
    m_statsTimer->start(1000); // 每秒更新一次统计
    
    // 设置瓦片清理定时器
    m_tileCleanupTimer = new QTimer(this);
    connect(m_tileCleanupTimer, &QTimer::timeout, this, &VideoRenderer::cleanupOldTiles);
    m_tileCleanupTimer->start(1000); // 每秒检查一次过期瓦片
    
    // 设置合成调度定时器
    m_compositionTimer = new QTimer(this);
    m_compositionTimer->setSingleShot(true); // 单次触发
    connect(m_compositionTimer, &QTimer::timeout, this, &VideoRenderer::optimizeComposition);
    
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
    
    // 瓦片模式下先合成瓦片
    if (m_tileMode) {
        composeTiles();
        // 使用合成帧
        {
            QMutexLocker tileLocker(&m_tileMutex);
            frameToDisplay = m_compositeFrame;
        }
    } else {
        // 使用普通帧
        QMutexLocker frameLocker(&m_frameMutex);
        frameToDisplay = m_currentFrame;
    }
    
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
    
    // 更新瓦片统计
    if (m_tileMode) {
        QMutexLocker locker(&m_tileMutex);
        m_stats.totalTiles = m_tileComposition.tilesPerRow * m_tileComposition.tilesPerColumn;
        m_stats.activeTiles = m_tileComposition.tiles.size();
        
        int dirtyCount = 0;
        for (const auto &tile : m_tileComposition.tiles) {
            if (tile.isDirty) dirtyCount++;
        }
        m_stats.dirtyTiles = dirtyCount;
        m_stats.lastTileUpdate = m_tileComposition.lastUpdateTime;
        
        // 计算瓦片更新率
        static qint64 lastTileCount = 0;
        static qint64 lastUpdateTime = QDateTime::currentMSecsSinceEpoch();
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        if (currentTime - lastUpdateTime >= 1000) {
            m_stats.tileUpdateRate = (m_stats.activeTiles - lastTileCount) * 1000.0 / (currentTime - lastUpdateTime);
            lastTileCount = m_stats.activeTiles;
            lastUpdateTime = currentTime;
        }
    }
    
    // 更新状态栏
    QString statusText;
    if (m_tileMode) {
        statusText = QString("帧数: %1 | FPS: %2 | 尺寸: %3x%4 | 瓦片: %5/%6 | 脏瓦片: %7")
                    .arg(m_stats.renderedFrames)
                    .arg(m_stats.averageFPS, 0, 'f', 1)
                    .arg(m_stats.currentFrameSize.width())
                    .arg(m_stats.currentFrameSize.height())
                    .arg(m_stats.activeTiles)
                    .arg(m_stats.totalTiles)
                    .arg(m_stats.dirtyTiles);
    } else {
        statusText = QString("帧数: %1 | FPS: %2 | 尺寸: %3x%4")
                    .arg(m_stats.renderedFrames)
                    .arg(m_stats.averageFPS, 0, 'f', 1)
                    .arg(m_stats.currentFrameSize.width())
                    .arg(m_stats.currentFrameSize.height());
    }
    
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

// 瓦片渲染方法实现

void VideoRenderer::setTileConfiguration(const QSize &frameSize, const QSize &tileSize)
{
    QMutexLocker locker(&m_tileMutex);
    
    m_tileComposition.frameSize = frameSize;
    m_tileComposition.tileSize = tileSize;
    m_tileComposition.tilesPerRow = (frameSize.width() + tileSize.width() - 1) / tileSize.width();
    m_tileComposition.tilesPerColumn = (frameSize.height() + tileSize.height() - 1) / tileSize.height();
    
    // 清空现有瓦片
    m_tileComposition.tiles.clear();
    
    // 创建合成帧
    m_compositeFrame = QPixmap(frameSize);
    m_compositeFrame.fill(Qt::black);
    
    m_tileMode = true;
    
    
}

void VideoRenderer::renderTile(int tileId, const QByteArray &tileData, const QRect &sourceRect, qint64 timestamp)
{
    if (!m_tileMode || tileData.isEmpty()) {
        return;
    }
    
    QMutexLocker locker(&m_tileMutex);
    
    // 创建瓦片图像
    QImage tileImage;
    if (tileData.size() == sourceRect.width() * sourceRect.height() * 4) {
        // ARGB32格式
        tileImage = QImage(reinterpret_cast<const uchar*>(tileData.constData()),
                          sourceRect.width(), sourceRect.height(), QImage::Format_ARGB32);
    } else if (tileData.size() == sourceRect.width() * sourceRect.height() * 3) {
        // RGB24格式
        tileImage = QImage(reinterpret_cast<const uchar*>(tileData.constData()),
                          sourceRect.width(), sourceRect.height(), QImage::Format_RGB888);
    } else {
        return;
    }
    
    if (tileImage.isNull()) {
        return;
    }
    
    // 创建或更新瓦片数据
    TileData &tile = m_tileComposition.tiles[tileId];
    tile.tileId = tileId;
    tile.sourceRect = sourceRect;
    tile.targetRect = calculateTileTargetRect(tileId);
    tile.pixmap = QPixmap::fromImage(tileImage);
    tile.timestamp = timestamp;
    tile.isDirty = true;
    
    m_tileComposition.lastUpdateTime = timestamp;
    
    // 更新脏区域并调度合成
    updateCompositionRegion(tile.targetRect);
    batchTileUpdates();
    
    
}

void VideoRenderer::updateTile(int tileId, const QByteArray &deltaData, const QRect &updateRect, qint64 timestamp)
{
    if (!m_tileMode || deltaData.isEmpty()) {
        return;
    }
    
    QMutexLocker locker(&m_tileMutex);
    
    auto it = m_tileComposition.tiles.find(tileId);
    if (it == m_tileComposition.tiles.end()) {
        return;
    }
    
    TileData &tile = it.value();
    
    // 创建增量图像
    QImage deltaImage;
    if (deltaData.size() == updateRect.width() * updateRect.height() * 4) {
        deltaImage = QImage(reinterpret_cast<const uchar*>(deltaData.constData()),
                           updateRect.width(), updateRect.height(), QImage::Format_ARGB32);
    } else if (deltaData.size() == updateRect.width() * updateRect.height() * 3) {
        deltaImage = QImage(reinterpret_cast<const uchar*>(deltaData.constData()),
                           updateRect.width(), updateRect.height(), QImage::Format_RGB888);
    } else {
        return;
    }
    
    if (deltaImage.isNull()) {
        return;
    }
    
    // 将增量应用到现有瓦片
    QImage tileImage = tile.pixmap.toImage();
    QPainter painter(&tileImage);
    painter.drawImage(updateRect.topLeft(), deltaImage);
    painter.end();
    
    tile.pixmap = QPixmap::fromImage(tileImage);
    tile.timestamp = timestamp;
    tile.isDirty = true;
    
    m_tileComposition.lastUpdateTime = timestamp;
    
    
}

void VideoRenderer::clearTiles()
{
    QMutexLocker locker(&m_tileMutex);
    
    m_tileComposition.tiles.clear();
    m_compositeFrame.fill(Qt::black);
    m_tileMode = false;
    
    
}

QRect VideoRenderer::calculateTileTargetRect(int tileId) const
{
    if (m_tileComposition.tilesPerRow == 0 || m_tileComposition.tilesPerColumn == 0) {
        return QRect();
    }
    
    int row = tileId / m_tileComposition.tilesPerRow;
    int col = tileId % m_tileComposition.tilesPerRow;
    
    int x = col * m_tileComposition.tileSize.width();
    int y = row * m_tileComposition.tileSize.height();
    
    return QRect(x, y, m_tileComposition.tileSize.width(), m_tileComposition.tileSize.height());
}

QRect VideoRenderer::calculateTileSourceRect(int tileId) const
{
    return calculateTileTargetRect(tileId); // 在这个实现中，源区域和目标区域相同
}

void VideoRenderer::updateTileMapping()
{
    // 重新计算所有瓦片的目标区域
    QMutexLocker locker(&m_tileMutex);
    
    for (auto &tile : m_tileComposition.tiles) {
        tile.targetRect = calculateTileTargetRect(tile.tileId);
        tile.isDirty = true;
    }
}

void VideoRenderer::composeTiles()
{
    // 检查是否已经在合成中，避免重复合成
    if (m_compositionInProgress.exchange(true)) {
        return; // 已经有合成在进行中
    }
    
    // 使用RAII确保标志被正确重置
    struct CompositionGuard {
        std::atomic<bool>& flag;
        VideoRenderer* renderer;
        CompositionGuard(std::atomic<bool>& f, VideoRenderer* r) : flag(f), renderer(r) {}
        ~CompositionGuard() { 
            flag.store(false); 
            renderer->m_lastCompositionTime = QDateTime::currentMSecsSinceEpoch();
        }
    } guard(m_compositionInProgress, this);
    
    QMutexLocker locker(&m_tileMutex);
    
    if (!m_tileMode || m_tileComposition.tiles.isEmpty()) {
        return;
    }
    
    // 检查是否有脏瓦片需要重新合成
    bool hasDirtyTiles = false;
    for (const auto &tile : m_tileComposition.tiles) {
        if (tile.isDirty) {
            hasDirtyTiles = true;
            break;
        }
    }
    
    if (!hasDirtyTiles) {
        return; // 没有脏瓦片，跳过合成
    }
    
    // 创建临时合成帧以避免在合成过程中阻塞显示
    QPixmap tempComposite(m_tileComposition.frameSize);
    tempComposite.fill(Qt::black);
    
    QPainter painter(&tempComposite);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false); // 禁用平滑变换以提高性能
    
    // 只合成脏瓦片以提高性能
    int composedTiles = 0;
    for (auto &tile : m_tileComposition.tiles) {
        if (!tile.pixmap.isNull() && tile.isDirty) {
            painter.drawPixmap(tile.targetRect, tile.pixmap, tile.sourceRect);
            tile.isDirty = false; // 标记为已合成
            composedTiles++;
        } else if (!tile.pixmap.isNull()) {
            // 绘制非脏瓦片以保持完整性
            painter.drawPixmap(tile.targetRect, tile.pixmap, tile.sourceRect);
        }
    }
    
    painter.end();
    
    // 原子性地更新合成帧
    m_compositeFrame = tempComposite;
    
    // 清空脏区域
    m_dirtyRegion = QRect();
    
    
}

void VideoRenderer::markTilesDirty()
{
    QMutexLocker locker(&m_tileMutex);
    
    for (auto &tile : m_tileComposition.tiles) {
        tile.isDirty = true;
    }
}

void VideoRenderer::cleanupOldTiles()
{
    if (!m_tileMode) {
        return;
    }
    
    QMutexLocker locker(&m_tileMutex);
    
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
    auto it = m_tileComposition.tiles.begin();
    while (it != m_tileComposition.tiles.end()) {
        if (currentTime - it.value().timestamp > m_tileTimeout) {
            it = m_tileComposition.tiles.erase(it);
        } else {
            ++it;
        }
    }
}

// 性能优化方法实现

void VideoRenderer::optimizeComposition()
{
    if (!m_tileMode || !shouldSkipComposition()) {
        composeTiles();
    }
}

bool VideoRenderer::shouldSkipComposition() const
{
    QMutexLocker locker(&m_tileMutex);
    
    // 检查是否有脏瓦片
    bool hasDirtyTiles = false;
    for (const auto &tile : m_tileComposition.tiles) {
        if (tile.isDirty) {
            hasDirtyTiles = true;
            break;
        }
    }
    
    if (!hasDirtyTiles) {
        return true; // 没有脏瓦片，跳过合成
    }
    
    // 检查帧率限制
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 timeSinceLastComposition = currentTime - m_lastCompositionTime;
    qint64 minInterval = 1000 / m_maxCompositionFPS; // 毫秒
    
    if (timeSinceLastComposition < minInterval) {
        return true; // 帧率限制，跳过合成
    }
    
    return false;
}

void VideoRenderer::updateCompositionRegion(const QRect &region)
{
    if (m_dirtyRegion.isNull()) {
        m_dirtyRegion = region;
    } else {
        m_dirtyRegion = m_dirtyRegion.united(region);
    }
}

void VideoRenderer::scheduleComposition()
{
    if (!m_compositionTimer->isActive()) {
        // 延迟合成以批量处理更新
        m_compositionTimer->start(16); // 16ms延迟，约60FPS
    }
}

void VideoRenderer::batchTileUpdates()
{
    if (!m_batchUpdatesEnabled) {
        return;
    }
    
    // 批量处理瓦片更新，减少频繁的重绘
    scheduleComposition();
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