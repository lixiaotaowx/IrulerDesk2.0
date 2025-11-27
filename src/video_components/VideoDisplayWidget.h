#ifndef VIDEODISPLAYWIDGET_H
#define VIDEODISPLAYWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTimer>
#include <QDialog>
#include <QPixmap>
#include <QPoint>
#include <QMutex>
#include <QReadWriteLock>
#include <QHash>
#include <QRect>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <memory>
#include <atomic>
// 音频播放（Qt6）
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>

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
    void pauseReceiving();
    bool isReceiving() const { return m_isReceiving; }
    
    // 发送观看请求
    void sendWatchRequest(const QString &viewerId, const QString &targetId);
    void setViewerName(const QString &name);
    
    // 获取统计信息
    VideoStats getStats() const { return m_stats; }
    
    // 设置显示模式
    void setShowControls(bool show);
    // 批注颜色设置与获取
    void setAnnotationColorId(int colorId);
    int annotationColorId() const { return m_currentColorId; }
    void setAnnotationEnabled(bool enabled);
    void setToolMode(int mode);
    void setTextFontSize(int size);

    // 显示切换中提示
    void showSwitchingIndicator(const QString &message = QStringLiteral("切换中..."));

    // 按索引切屏（供系统设置列表直接调用，保持会话不断流）
    void sendSwitchScreenIndex(int index);
    void setAutoResize(bool autoResize) { m_autoResize = autoResize; }
    // 发送音频测试开关（由上层UI触发）
    void sendAudioToggle(bool enabled);
    // 本地播放开关（不影响推流端）
    void setSpeakerEnabled(bool enabled);
    bool isSpeakerEnabled() const { return m_speakerEnabled; }
    void setVolumePercent(int percent);
    int volumePercent() const { return m_volumePercent; }
    void setMicGainPercent(int percent);
    int micGainPercent() const { return m_micGainPercent; }
    void setTalkEnabled(bool enabled);
    void selectAudioOutputFollowSystem();
    void selectAudioOutputById(const QString &id);
    void selectAudioOutputByRawId(const QByteArray &id);
    bool isAudioOutputFollowSystem() const { return m_followSystemOutput; }
    QString currentAudioOutputId() const { return QString::fromUtf8(m_currentOutputDeviceId); }
    void selectMicInputFollowSystem();
    void selectMicInputById(const QString &id);
    bool isMicInputFollowSystem() const;
    QString currentMicInputDeviceId() const;
    
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
    // 双击视频区域触发全屏切换
    void fullscreenToggleRequested();
    // 批注颜色变化（用于持久化与UI同步）
    void annotationColorChanged(int colorId);
    void audioOutputSelectionChanged(bool followSystem, const QString &deviceId);
    void micInputSelectionChanged(bool followSystem, const QString &deviceId);
    
public slots:
    void renderFrame(const QByteArray &frameData, const QSize &frameSize);
    void renderFrameWithTimestamp(const QByteArray &frameData, const QSize &frameSize, qint64 captureTimestamp);
    void updateConnectionStatus(const QString &status);
    void onMousePositionReceived(const QPoint &position, qint64 timestamp); // 新增：鼠标位置接收槽
    // 批注控制：撤销上一笔与清屏
    void sendUndo();
    void sendClear();
    void sendCloseOverlay();
    void sendLike();
    QImage captureToImage() const;

private slots:
    void onStartStopClicked();
    void updateStatsDisplay();
    
private:
    // 重新创建并连接WebSocket接收器，防止旧实例卡死或残留状态
    void recreateReceiver();
    void setupUI();
    void updateButtonText();
    void drawMouseCursor(QPixmap &pixmap, const QPoint &position); // 保留旧接口（不再使用远端叠加）
    void updateLocalCursorComposite();
    // 捕获鼠标并映射到源坐标
    bool eventFilter(QObject *obj, QEvent *event) override;
    QPoint mapLabelToSource(const QPoint &labelPoint) const;
    void closeEvent(QCloseEvent *event) override;
    void applyCursor();
    double currentScale() const;
    void updateScaledCursorComposite(double scale);
    
    // 瓦片渲染私有方法
    void updateTileMapping();
    void composeTiles();
    void renderTileToPixmap(const TileData &tile);
    QRect calculateTileTargetRect(int tileId) const;
    QRect calculateTileSourceRect(int tileId) const;
    void markTilesDirty();
    void cleanupOldTiles();
    // 定时继续观看提示逻辑
    void setupContinuePrompt();
    void showContinuePrompt();
    void onContinueClicked();
    void onPromptCountdownTick();
    void showWaitingSplash();
    void stopWaitingSplash();
    void updateWaitingSplashFrame();
    void animateWaitingSplashFlyOut();
    
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
    bool m_decoderInitialized = false;
    std::unique_ptr<WebSocketReceiver> m_receiver;
    
    // 状态
    bool m_isReceiving;
    bool m_showControls;
    bool m_autoResize;
    QString m_serverUrl;
    VideoStats m_stats;
    bool m_speakerEnabled = true; // 本地扬声器开关，默认开启
    // 音频播放相关状态
    QAudioFormat m_audioFormat;
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_audioIO = nullptr;
    bool m_audioInitialized = false;
    void initAudioSinkIfNeeded(int sampleRate, int channels, int bitsPerSample);
    QByteArray convertForSink(const QByteArray &srcPcm, int srcSr, int srcCh) const;
    void softRestartSpeakerIfEnabled();
    void forceRecreateSink();
    int m_volumePercent = 100;
    int m_micGainPercent = 100;
    bool m_followSystemOutput = true;
    QByteArray m_outputDeviceId;
    QByteArray m_currentOutputDeviceId;
    QMetaObject::Connection m_audioConn;
    class QMediaDevices *m_mediaDevices = nullptr;
    QTimer *m_defaultOutPollTimer = nullptr;
    int m_sinkSampleRate = 16000;
    int m_sinkChannels = 1;
    bool m_needResample = false;
    int m_lastFrameSampleRate = 16000;
    int m_lastFrameChannels = 1;
    int m_lastFrameBitsPerSample = 16;
    
    // 端到端延迟统计
    QList<double> m_latencyHistory;
    qint64 m_currentCaptureTimestamp = 0; // 当前帧的捕获时间戳
    static const int MAX_LATENCY_SAMPLES = 30; // 保持最近30帧的延迟数据
    
    // 鼠标位置相关
    QPoint m_mousePosition; // 当前鼠标位置
    qint64 m_mouseTimestamp = 0; // 鼠标位置时间戳
    bool m_hasMousePosition = false; // 是否有有效的鼠标位置数据
    QLabel *m_localCursorOverlay = nullptr; // 本地即时光标叠加
    QPixmap m_cursorBase;
    QPixmap m_cursorSmall;
    QPixmap m_cursorComposite;
    QPixmap m_cursorScaledComposite;
    double m_lastCursorScale = -1.0;
    QColor m_localCursorColor;

    // 最近一次观看者与目标ID（用于重发watch_request以恢复推流）
    QString m_lastViewerId;
    QString m_lastTargetId;
    QString m_viewerName;
    bool m_annotationEnabled = false;
    int m_toolMode = 0;
    bool m_isAnnotating = false; // 是否处于批注绘制中（鼠标按下）
    bool m_mouseButtonsSwapped = false; // 系统是否交换了鼠标左右键
    
    // 瓦片渲染相关
    TileComposition m_tileComposition;
    QPixmap m_compositeFrame;      // 合成后的完整帧
    mutable QMutex m_tileMutex;   // 瓦片数据保护（可变以支持const方法）
    QReadWriteLock m_tileReadWriteLock; // 读写锁，提高并发性能
    bool m_tileMode;              // 是否启用瓦片模式
    qint64 m_tileTimeout;         // 瓦片超时时间（毫秒）
    QTimer *m_tileCleanupTimer;   // 瓦片清理定时器
    std::atomic<bool> m_compositionInProgress; // 合成进行中标志
    
    // 继续观看提示定时与对话框
    QTimer *m_continuePromptTimer = nullptr; // 周期触发30分钟/测试1分钟
    int m_promptIntervalMinutes = 30;        // 默认30分钟，可通过环境变量覆盖
    QDialog *m_promptDialog = nullptr;       // 弹窗
    QLabel *m_promptLabel = nullptr;         // 提示文字，显示剩余秒数
    QPushButton *m_continueButton = nullptr; // 继续观看按钮
    QTimer *m_promptCountdownTimer = nullptr;// 倒计时定时器（10秒）
    int m_remainingSeconds = 0;              // 剩余倒计时秒数
    // 等待遮罩动画
    bool m_waitSplashActive = false;
    QTimer *m_waitingDotsTimer = nullptr;
    QPixmap m_waitBaseCached;
    QPixmap m_waitWmCached;
    QPixmap m_lastWaitCanvas;
    int m_waitingDotsPhase = 0;
    QLabel *m_waitFlyLabel = nullptr;
    QGraphicsOpacityEffect *m_waitFlyOpacity = nullptr;
    QPropertyAnimation *m_waitFlyGeomAnim = nullptr;
    QPropertyAnimation *m_waitFlyOpacityAnim = nullptr;
    QParallelAnimationGroup *m_waitFlyGroup = nullptr;
    // 批注颜色ID（0:红,1:绿,2:蓝,3:黄）
    int m_currentColorId = 0;
    int m_textFontSize = 16;
    QPoint m_lastTextSrcPoint;
};

#endif // VIDEODISPLAYWIDGET_H