#ifndef VIDEORENDERER_H
#define VIDEORENDERER_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QPainter>
#include <QPixmap>
#include <QImage>
#include <QSize>
#include <QMutex>
#include <QStatusBar>
#include <QMainWindow>
#include <QCloseEvent>
#include <QHash>
#include <QMap>
#include <QRect>
#include <QReadWriteLock>
#include <atomic>
#include <QDateTime>

class VideoRenderer : public QMainWindow
{
    Q_OBJECT

public:
    // 瓦片渲染相关数据结构
    struct TileData {
        int tileId;
        QRect sourceRect;      // 瓦片在原始帧中的位置
        QRect targetRect;      // 瓦片在屏幕上的位置
        QPixmap pixmap;        // 瓦片图像数据
        qint64 timestamp;      // 瓦片时间戳
        bool isDirty;          // 是否需要重新渲染
    };
    
    struct TileComposition {
        QSize frameSize;       // 完整帧大小
        QSize tileSize;        // 单个瓦片大小
        int tilesPerRow;       // 每行瓦片数
        int tilesPerColumn;    // 每列瓦片数
        QHash<int, TileData> tiles; // 瓦片数据映射
        qint64 lastUpdateTime; // 最后更新时间
    };

    explicit VideoRenderer(QWidget *parent = nullptr);
    ~VideoRenderer();
    
    // 渲染帧数据
    void renderFrame(const QByteArray &frameData, const QSize &frameSize);
    
    // 瓦片渲染方法
    void renderTile(int tileId, const QByteArray &tileData, const QRect &sourceRect, qint64 timestamp);
    void updateTile(int tileId, const QByteArray &deltaData, const QRect &updateRect, qint64 timestamp);
    void setTileConfiguration(const QSize &frameSize, const QSize &tileSize);
    void clearTiles();
    
    // 设置连接状态
    void setConnectionStatus(bool connected);
    
    // 获取渲染统计信息
    struct RenderStats {
        quint64 totalFrames;
        quint64 renderedFrames;
        double averageFPS;
        QSize currentFrameSize;
        // 瓦片统计
        int totalTiles;
        int activeTiles;
        int dirtyTiles;
        double tileUpdateRate;
        qint64 lastTileUpdate;
    };
    RenderStats getStats() const { return m_stats; }

signals:
    void windowClosed();
    void statsUpdated(const RenderStats &stats);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void updateDisplay();
    void updateStats();

private:
    void setupUI();
    void setupStatusBar();
    QPixmap scalePixmapToFit(const QPixmap &pixmap, const QSize &targetSize);
    
    // 瓦片渲染私有方法
    void updateTileMapping();
    void composeTiles();
    void renderTileToPixmap(const TileData &tile);
    QRect calculateTileTargetRect(int tileId) const;
    QRect calculateTileSourceRect(int tileId) const;
    void markTilesDirty();
    void cleanupOldTiles();
    
    // 性能优化方法
    void optimizeComposition();
    bool shouldSkipComposition() const;
    void updateCompositionRegion(const QRect &region);
    void scheduleComposition();
    void batchTileUpdates();
    
    // UI组件
    QWidget *m_centralWidget;
    QVBoxLayout *m_mainLayout;
    QLabel *m_videoLabel;
    QLabel *m_statusLabel;
    QStatusBar *m_statusBar;
    
    // 渲染相关
    QPixmap m_currentFrame;
    QSize m_frameSize;
    QMutex m_frameMutex;
    
    // 瓦片渲染相关
    TileComposition m_tileComposition;
    QPixmap m_compositeFrame;      // 合成后的完整帧
    mutable QMutex m_tileMutex;   // 瓦片数据保护（可变以支持const方法）
    QReadWriteLock m_tileReadWriteLock; // 读写锁，提高并发性能
    bool m_tileMode;              // 是否启用瓦片模式
    qint64 m_tileTimeout;         // 瓦片超时时间（毫秒）
    QTimer *m_tileCleanupTimer;   // 瓦片清理定时器
    std::atomic<bool> m_compositionInProgress; // 合成进行中标志
    
    // 性能优化相关
    QTimer *m_compositionTimer;   // 合成调度定时器
    QRect m_dirtyRegion;          // 脏区域
    bool m_batchUpdatesEnabled;   // 批量更新启用标志
    qint64 m_lastCompositionTime; // 最后合成时间
    int m_maxCompositionFPS;      // 最大合成帧率
    
    // 状态管理
    bool m_connected;
    QTimer *m_displayTimer;
    QTimer *m_statsTimer;
    
    // 统计信息
    RenderStats m_stats;
    QList<qint64> m_frameTimes;
    qint64 m_lastFrameTime;
    

};

#endif // VIDEORENDERER_H