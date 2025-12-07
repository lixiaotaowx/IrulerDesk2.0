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
#include "AudioPlayer.h"

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
    
    // 瓦片统计信息已移除

};

class VideoDisplayWidget : public QWidget
{
    Q_OBJECT

public:
    // 瓦片渲染相关数据结构已移除

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
    bool isSpeakerEnabled() const { return m_audioPlayer ? m_audioPlayer->isSpeakerEnabled() : false; }
    void setVolumePercent(int percent);
    int volumePercent() const { return m_audioPlayer ? m_audioPlayer->volumePercent() : 0; }
    void setMicGainPercent(int percent);
    int micGainPercent() const { return m_micGainPercent; }
    void setTalkEnabled(bool enabled);
    void setMicSendEnabled(bool enabled);
    bool micSendEnabled() const { return m_micSendEnabled; }
    void selectAudioOutputFollowSystem();
    void selectAudioOutputById(const QString &id);
    void selectAudioOutputByRawId(const QByteArray &id);
    bool isAudioOutputFollowSystem() const { return m_audioPlayer ? m_audioPlayer->isAudioOutputFollowSystem() : true; }
    QString currentAudioOutputId() const { return m_audioPlayer ? m_audioPlayer->currentAudioOutputId() : QString(); }
    void applyAudioOutputSelectionRuntime();
    void selectMicInputFollowSystem();
    void selectMicInputById(const QString &id);
    bool isMicInputFollowSystem() const;
    QString currentMicInputDeviceId() const;
    
    // 瓦片渲染方法已移除

    
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
    void avatarUpdateReceived(const QString &userId, int iconId);
    
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
    void notifyTargetOffline(const QString &reason = QString());
    void clearOfflineReminder();

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
    
    // 瓦片渲染私有方法已移除

    // 定时继续观看提示逻辑
    void setupContinuePrompt();
    void showContinuePrompt();
    void onContinueClicked();
    void onPromptCountdownTick();
    void showWaitingSplash();
    void stopWaitingSplash();
    void updateWaitingSplashFrame();
    void animateWaitingSplashFlyOut();
    void showOfflineReminder(const QString &reason = QString());
    void resizeEvent(QResizeEvent *event) override;
    
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
    bool m_micSendEnabled = true;
    int m_micGainPercent = 100;
    
    std::unique_ptr<AudioPlayer> m_audioPlayer;

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
    
    // 瓦片渲染相关成员已移除

    
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
    QLabel *m_offlineLabel = nullptr;
    QGraphicsOpacityEffect *m_offlineOpacity = nullptr;
    QTimer *m_offlineHideTimer = nullptr;
    // 批注颜色ID（0:红,1:绿,2:蓝,3:黄）
    int m_currentColorId = 0;
    int m_textFontSize = 16;
    QPoint m_lastTextSrcPoint;
};

#endif // VIDEODISPLAYWIDGET_H
