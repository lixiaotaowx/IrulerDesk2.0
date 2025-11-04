#ifndef VIDEODISPLAYWIDGET_H
#define VIDEODISPLAYWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTimer>
#include <QPixmap>
#include <QPoint>
#include <QMutex>
#include <QReadWriteLock>
#include <QHash>
#include <QRect>
#include <memory>
#include <atomic>

// 前向声明
class DxvaVP9Decoder;
class WebSocketReceiver;

struct VideoStats {
    int framesReceived = 0;
    int framesDecoded = 0;
    int framesDisplayed = 0;
    double avgDecodeTime = 0.0;
    double avgEndToEndLatency = 0.0; // 端到端延迟（毫秒）
    QString connectionStatus = "Disconnected";
    QSize frameSize = QSize(0, 0);
    
    // 瓦片统计信息
    int totalTiles = 0;
    int activeTiles = 0;
    int dirtyTiles = 0;
    qint64 lastTileUpdate = 0;
    double tileUpdateRate = 0.0;
};

class VideoDisplayWidget : public QWidget
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

    explicit VideoDisplayWidget(QWidget *parent = nullptr);
    ~VideoDisplayWidget();
    
    // 控制接口
    void startReceiving(const QString &serverUrl = "");
    void stopReceiving();
    bool isReceiving() const { return m_isReceiving; }
    
    // 发送观看请求
    void sendWatchRequest(const QString &viewerId, const QString &targetId);
    
    // 获取统计信息
    VideoStats getStats() const { return m_stats; }
    
    // 设置显示模式
    void setShowControls(bool show);
    void setAutoResize(bool autoResize) { m_autoResize = autoResize; }
    
    // 瓦片渲染方法
    void renderTile(int tileId, const QByteArray &tileData, const QRect &sourceRect, qint64 timestamp);
    void updateTile(int tileId, const QByteArray &deltaData, const QRect &updateRect, qint64 timestamp);
    void setTileConfiguration(const QSize &frameSize, const QSize &tileSize);
    void clearTiles();
    void setTileMode(bool enabled) { m_tileMode = enabled; }
    
signals:
    void connectionStatusChanged(const QString &status);
    void frameReceived();
    void statsUpdated(const VideoStats &stats);
    
public slots:
    void renderFrame(const QByteArray &frameData, const QSize &frameSize);
    void renderFrameWithTimestamp(const QByteArray &frameData, const QSize &frameSize, qint64 captureTimestamp);
    void updateConnectionStatus(const QString &status);
    void onMousePositionReceived(const QPoint &position, qint64 timestamp); // 新增：鼠标位置接收槽
    
private slots:
    void onStartStopClicked();
    void updateStatsDisplay();
    
private:
    void setupUI();
    void updateButtonText();
    void drawMouseCursor(QPixmap &pixmap, const QPoint &position); // 新增：绘制鼠标光标
    
    // 瓦片渲染私有方法
    void updateTileMapping();
    void composeTiles();
    void renderTileToPixmap(const TileData &tile);
    QRect calculateTileTargetRect(int tileId) const;
    QRect calculateTileSourceRect(int tileId) const;
    void markTilesDirty();
    void cleanupOldTiles();
    
    // UI组件
    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_controlLayout;
    QLabel *m_videoLabel;
    QPushButton *m_startStopButton;
    QLabel *m_statusLabel;
    QLabel *m_statsLabel;
    QTimer *m_statsTimer;
    
    // 控制接口
    std::unique_ptr<DxvaVP9Decoder> m_decoder;
    std::unique_ptr<WebSocketReceiver> m_receiver;
    
    // 状态
    bool m_isReceiving;
    bool m_showControls;
    bool m_autoResize;
    QString m_serverUrl;
    VideoStats m_stats;
    
    // 端到端延迟统计
    QList<double> m_latencyHistory;
    qint64 m_currentCaptureTimestamp = 0; // 当前帧的捕获时间戳
    static const int MAX_LATENCY_SAMPLES = 30; // 保持最近30帧的延迟数据
    
    // 鼠标位置相关
    QPoint m_mousePosition; // 当前鼠标位置
    qint64 m_mouseTimestamp = 0; // 鼠标位置时间戳
    bool m_hasMousePosition = false; // 是否有有效的鼠标位置数据
    
    // 瓦片渲染相关
    TileComposition m_tileComposition;
    QPixmap m_compositeFrame;      // 合成后的完整帧
    mutable QMutex m_tileMutex;   // 瓦片数据保护（可变以支持const方法）
    QReadWriteLock m_tileReadWriteLock; // 读写锁，提高并发性能
    bool m_tileMode;              // 是否启用瓦片模式
    qint64 m_tileTimeout;         // 瓦片超时时间（毫秒）
    QTimer *m_tileCleanupTimer;   // 瓦片清理定时器
    std::atomic<bool> m_compositionInProgress; // 合成进行中标志
};

#endif // VIDEODISPLAYWIDGET_H