#pragma once
#include <QWidget>
#include <QMouseEvent>
#include <QListWidget>
#include <QTimer>
#include <QLabel>
#include <QFrame>
#include <QMap>
#include <QPushButton>
#include <QIcon>
#include <QPixmap>
#include <QPointer>
#include <QMessageBox>
#include <QRubberBand>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QVideoFrame>
#include "StreamClient.h"
#include "../ui/VolumeLevelBar.h"
#include "MicLevelMonitor.h"
#include "LoginClient.h"

class QStackedWidget;
class QWebEngineView;
class QDialog;
class QTimer;
class QScrollArea;
class QHBoxLayout;
class VideoDisplayWidget;
class AnnotationToolbar;
class AutoUpdater;
class QProgressDialog;
class NewUserGuide;
class LocalActivityMonitor;

class NewUiWindow : public QWidget
{
    Q_OBJECT

public:
    explicit NewUiWindow(QWidget *parent = nullptr);
    ~NewUiWindow();

public slots:
    void showUserGuide();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void onTimerTimeout();
    void onTalkSpinnerTimeout();
    void onStreamLog(const QString &msg);
    void onUserListUpdated(const QJsonArray &users);
    void onLoginConnected();
    void toggleFunction1Maximize();
    void onBroadcastBtnClicked();
    void onMeetingBtnClicked();
    void onInviteRequested(const QStringList &userIds);
    void onTextMessageReceived(const QString &message);

    // Auto Update Slots
    void checkForUpdates();
    void onUpdateAvailable(const QString &version, const QString &downloadUrl, const QString &description, bool force);
    void onUpdateDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onUpdateError(const QString &error);

public:
    void setMyStreamId(const QString &id, const QString &name = QString());
    void setCaptureScreenIndex(int index);
    void setRemoteActivityState(const QString &userId, bool active);
    bool localActivityActive() const;
    void setTalkPending(const QString &userId, bool pending);
    void setTalkConnected(const QString &userId, bool connected);
    void setTalkRemoteActive(const QString &userId, bool active);
    void setViewerMicState(const QString &viewerId, bool enabled);
    void setGlobalMicCheckedSilently(bool enabled);
    void janusSwitchToUserRoom(const QString &userId);
    void janusSwitchToMyRoom();
    void janusSetIgnoreAlone(bool ignore);
    void janusSetMuted(bool muted);
    void janusStop();
    void showAudioCallUiForSession(const QString &peerId, bool forceEnableMic);
    void showAudioCallMiniBar();
    void hideAudioCallMiniBar();
    void setAudioCallMiniHidden(bool hidden);
    void restoreAudioCallUi();
    QString activeAudioCallPeerId() const;

    // Viewer List Management
    void addViewer(const QString &id, const QString &name);
    void removeViewer(const QString &id);
    void clearViewers(); // Clear all viewers
    int getViewerCount() const; // Get current viewer count
    QStringList getViewerIds() const;
    void updateViewerNameIfExists(const QString &id, const QString &name);
    void sendKickToSubscribers(const QString &viewerId);

    void setWatchingTarget(const QString &targetId);

public:
    // Main Program Integration Methods
    void addUser(const QString &userId, const QString &userName);
    void addUser(const QString &userId, const QString &userName, int iconId);
    void removeUser(const QString &userId);
    void clearUserList();
    void updateUserAvatar(const QString &userId, int iconId);
    void restartUserStreamSubscription(const QString &userId);
    void onVideoReceivingStopped(const QString &targetId);
    QString getCurrentUserId() const; // Returns the local user ID
    VideoDisplayWidget* embeddedVideoWidget() const;
    void toggleEmbeddedVideoFullscreen(bool maximized);
    void enterEmbeddedWatchingUi(const QString &targetId, const QString &targetName = QString());
    void startEmbeddedReceiving(const QString &viewerId,
                               const QString &targetId,
                               const QString &viewerName,
                               const QString &serverUrl,
                               int initialColorId);
    void stopEmbeddedWatching();
    bool isEmbeddedWatching() const;
    bool isEmbeddedWatchingTarget(const QString &targetId) const;

    void updateNotificationPositions();
    void showInviteNotification(const QString &inviterId, const QString &inviterName, const QString &type);
    void closeInviteNotification();

signals:
    void startWatchingRequested(const QString &targetId, const QString &targetName = QString());
    void systemSettingsRequested();
    void micToggleRequested(bool enabled);
    void clearMarksRequested();
    void toggleStreamingIslandRequested();
    void setStreamingIslandVisibleRequested(bool visible);
    void kickViewerRequested(const QString &viewerId);
    void closeRoomRequested();
    void talkToggleRequested(const QString &targetId, bool enabled);
    void avatarPixmapUpdated(const QString &userId, const QPixmap &pixmap);
    void broadcastRequested(const QString &content, const QStringList &targets);
    void audioCallRestoreAvailableChanged(bool available);
    void stopWatchingRequested(const QString &targetId);
    void videoReceivingStopped(const QString &targetId);
    void videoFullscreenToggled(bool maximized);
    void localActivityStateChanged(bool active);

private:
    void setupUi();
    void showFunction1Browser();
    void showHomeContent();
    void updateTitleMaximizeButton();
    void updateEmbeddedFullscreenOverlayGeometry();
    // void toggleEmbeddedVideoFullscreen(bool maximized); // Moved to public
    void updateListWidget(const QJsonArray &users);
    void updateTalkOverlay(const QString &userId);
    QIcon buildSpinnerIcon(int size, int angleDeg) const;
    QPixmap buildTestAvatarPixmap(int size) const;
    QPixmap buildHeadAvatarPixmap(int size) const;
    void pickAndApplyLocalAvatar();
    QString avatarCacheDirPath() const;
    QString avatarCacheFilePath(const QString &userId) const;
    void ensureAvatarCacheDir();
    QPixmap makeCircularPixmap(const QPixmap &src, int size) const;
    void setAvatarLabelPixmap(QLabel *label, const QPixmap &src);
    void ensureAvatarSubscription(const QString &userId);
    void refreshLocalAvatarFromCache();
    void publishLocalAvatarOnce();
    void publishLocalAvatarHint();
    void publishLocalScreenFrame(bool force);
    void publishLocalScreenFrameTriggered(const QString &reason, bool forceSend, bool allowCapture);
    void startSelfPreviewFast();
    void stopSelfPreviewFast();
    void buildLocalPreviewFrameFast(QPixmap &previewPix);
    void buildLocalScreenFrame(QPixmap &previewPix, QPixmap &sendPix);
    QString extractUserId(QObject *obj) const;
    QString makeHoverChannelId(const QString &targetUserId) const;
    void scheduleHoverHiFps(const QString &userId, const QPoint &globalPos);
    void cancelHoverHiFps();
    void startHiFpsForUser(const QString &userId);
    void stopHiFpsForUser();
    void sendHiFpsControl(const QString &targetUserId, const QString &channelId, int fps, bool enabled);
    void startHiFpsPublishing(const QString &channelId, int fps);
    void stopHiFpsPublishing(const QString &channelId);
    void resetSelectionAutoPause(const QString &userId);
    void pauseSelectedStreamForUser(const QString &userId);
    void resumeSelectedStreamForUser(const QString &userId);
    void updateResizeGrips();
    void setResizeGripsVisible(bool visible);
    void ensureJanusAudioLoaded();
    void applyJanusAudioState();
    void scheduleJanusEnsure(const QString &desiredOwnerId);
    void stopJanusEnsure();
    void ensureAudioCallUi();
    void showAudioCallUi(const QString &peerId);
    void showAudioCallUiInternal(const QString &peerId, bool forceEnableMic);
    void hideAudioCallUi();
    void refreshAudioCallParticipants();
    void rebuildAudioCallParticipantsUi(const QStringList &names);
    void hangupAudioCallUi();
    void updateTalkButtonsAvailability();
    void updateLocalWatchedOverlay();
    bool isInMyRoomViewerList(const QString &userId) const;
    void setRemotePreviewsSuspended(bool suspended);
    bool hasCardImage(const QString &userId) const;
    void requestPreviewFrameForUser(const QString &userId);
    
    // Dragging support
    bool m_dragging = false;
    bool m_isWaitingForAttendees = false;
    QPointer<QMessageBox> m_inviteWaitDialog;
    QStringList m_pendingInvitees;
    QWidget *m_expiredInviteNotification = nullptr;
    QWidget *m_activeInviteNotification = nullptr;
    void showExpiredInviteNotification(const QString &inviterName);
    void showCancelledInviteNotification(const QString &inviterName, const QString &timeStr);
    // Moved to public: void showInviteNotification(const QString &inviterId, const QString &inviterName, const QString &type);
    // Moved to public: void updateNotificationPositions();
    void updateLocalCardActivityStyle(bool active);
    void updateRemoteCardActivityStyle(const QString &userId);

    QPoint m_dragPosition;

    QListWidget *m_listWidget = nullptr;
    QTimer *m_timer = nullptr;
    QTimer *m_selfPreviewFastTimer = nullptr;
    QTimer *m_cardWatchdogTimer = nullptr;
    LocalActivityMonitor *m_localActivityMonitor = nullptr;
    QLabel *m_videoLabel = nullptr; // Local preview label (Index 0)
    QLabel *m_logoLabel = nullptr;
    QWidget *m_farRightPanel = nullptr; // Far right panel (My Room)
    QListWidget *m_viewerList = nullptr; // Viewer list widget
    QMap<QString, QListWidgetItem*> m_viewerItems; // Viewer ID -> List Item
    QMap<QString, QPushButton*> m_viewerMicButtons;
    QMap<QString, bool> m_viewerMicStates;
    QLabel *m_localNameLabel = nullptr; // Local name label (Index 0)
    QFrame *m_localCard = nullptr; // Local card frame (Index 0)
    QLabel *m_toolbarAvatarLabel = nullptr;
    QLabel *m_localAvatarLabel = nullptr;
    QLabel *m_localWatchedOverlay = nullptr;
    QPushButton *m_titleMaximizeBtn = nullptr;
    QPushButton *m_titleMicBtn = nullptr;
    QIcon m_titleMicIconOn;
    QIcon m_titleMicIconOff;
    QPushButton *m_titleBackBtn = nullptr;
    QStackedWidget *m_rightContentStack = nullptr;
    QWidget *m_homeContentPage = nullptr;
    QWidget *m_function1BrowserPage = nullptr;
    QWidget *m_function2BrowserPage = nullptr;
    QWidget *m_function3BrowserPage = nullptr;
    QWidget *m_videoContentPage = nullptr;
    QWebEngineView *m_function1WebView = nullptr;
    QWebEngineView *m_function2WebView = nullptr;
    QWebEngineView *m_function3WebView = nullptr;
    QWebEngineView *m_janusWebView = nullptr;
    StreamClient *m_streamClient = nullptr;
    StreamClient *m_streamClientLan = nullptr;
    LoginClient *m_loginClient = nullptr;
    StreamClient *m_avatarPublisher = nullptr;
    StreamClient *m_avatarPublisherLan = nullptr;
    QTimer *m_avatarPublishTimer = nullptr;
    QMap<QString, StreamClient*> m_avatarSubscribers;
    QPixmap m_localAvatarPublishPixmap;

    QString m_myStreamId; // Store my own ID to identify myself in the list
    QString m_myUserName;

    // Remote Stream Management
    QMap<QString, StreamClient*> m_remoteStreams; // userId -> StreamClient
    QMap<QString, QListWidgetItem*> m_userItems;  // userId -> ListWidgetItem
    QMap<QString, QLabel*> m_userLabels;          // userId -> Image Label (for updating frame)
    QMap<QString, QLabel*> m_userAvatarLabels;    // userId -> Avatar Label (top-left overlay)
    QMap<QString, bool> m_remoteActivityStates; // uid -> isActive
    QSet<QString> m_talkingUsers; // uid of talking users
    QSet<QString> m_suspendedRemoteStreams;
    QMap<QString, qint64> m_lastCardFrameAtMs;
    QMap<QString, qint64> m_lastPreviewRequestAtMs;
    bool m_suspendRemotePreviewsRequested = false;
    
    // Helper to update remote volume
    void updateRemoteVolume(float vol);
    QMap<QString, QPushButton*> m_talkButtons;    // userId -> Talk Button (end/get)
    QMap<QString, QLabel*> m_talkOverlays;        // userId -> "通话中" overlay label
    QTimer *m_talkSpinnerTimer = nullptr;
    QMap<QString, int> m_talkSpinnerAngles;
    QMap<QString, QLabel*> m_reselectOverlays;
    QTimer *m_selectionAutoPauseTimer = nullptr;
    QString m_selectionAutoPauseUserId;
    QString m_autoPausedUserId;
    QString m_watchingTargetId;
    QString m_embeddedTargetId;
    VideoDisplayWidget *m_embeddedVideoWidget = nullptr;
    AnnotationToolbar *m_annotationToolbar = nullptr;

    // Auto Update
    AutoUpdater *m_autoUpdater = nullptr;
    QProgressDialog *m_updateProgressDialog = nullptr;
    
    // Window dragging optimization
    bool m_isWin10 = false;
    bool m_isWin11 = false;
    bool m_isPrivacyMode = false; // [Privacy Mode] 隐私模式状态
    bool m_isCameraMode = false; // [Camera Mode] 摄像头模式状态
    
    QScopedPointer<QCamera> m_camera;
    QScopedPointer<QMediaCaptureSession> m_captureSession;
    QScopedPointer<QVideoSink> m_videoSink;
    void ensureCameraStarted();
    void stopCamera();

    void togglePrivacyMode(bool enable);
    void updateAcrylicState(bool enable);
    QRubberBand *m_dragGhost = nullptr;

    QFrame *m_annotationContainer = nullptr;
    QWidget *m_videoTopBar = nullptr;
    QWidget *m_videoTopRightPlaceholder = nullptr;
    QWidget *m_embeddedFullscreenOverlay = nullptr;
    bool m_embeddedFullscreenActive = false;

    QWidget *m_titleBar = nullptr;
    bool m_titleBarDragging = false;
    bool m_titleBarPendingRestore = false;
    bool m_titleBarSnapMaximize = false;
    QPoint m_titleBarPressGlobal;
    QPoint m_titleBarPressLocalInWindow;
    QPoint m_titleBarDragOffset;

    QWidget *m_resizeGripLeft = nullptr;
    QWidget *m_resizeGripRight = nullptr;
    QWidget *m_resizeGripTop = nullptr;
    QWidget *m_resizeGripBottom = nullptr;
    QWidget *m_resizeGripTopLeft = nullptr;
    QWidget *m_resizeGripTopRight = nullptr;
    QWidget *m_resizeGripBottomLeft = nullptr;
    QWidget *m_resizeGripBottomRight = nullptr;
    bool m_resizeDragging = false;
    Qt::Edges m_resizeEdges;
    QPoint m_resizePressGlobal;
    QRect m_resizeStartGeometry;

    QTimer *m_hoverCandidateTimer = nullptr;
    QString m_hoverCandidateUserId;
    QPoint m_hoverCandidatePos;
    QString m_hiFpsActiveUserId;
    QString m_hiFpsActiveChannelId;
    StreamClient *m_hiFpsSubscriber = nullptr;
    QMap<QString, StreamClient*> m_hiFpsPublishers;
    QMap<QString, StreamClient*> m_hiFpsPublishersLan;
    QMap<QString, QTimer*> m_hiFpsPublisherTimers;
    QTimer *m_hiFpsWatchdogTimer = nullptr;
    qint64 m_hiFpsLastFrameAtMs = 0;
    qint64 m_hiFpsLastRecoveryAtMs = 0;
    bool m_keepAwakeRequested = false;

    // channel_id -> { sender_id -> fps }
    QMap<QString, QMap<QString, int>> m_channelSubscribers;

    // Layout constants
    int m_cardBaseWidth;
    int m_bottomAreaHeight;
    int m_shadowSize;
    double m_aspectRatio;
    int m_cardBaseHeight;
    int m_totalItemWidth;
    int m_totalItemHeight;
    int m_imgWidth;
    int m_imgHeight;
    int m_marginX;
    int m_topAreaHeight;
    int m_marginTop;
    int m_captureScreenIndex = -1;

    QPixmap m_lastPreviewFramePixmap;
    QPixmap m_lastPreviewSendPixmap;
    qint64 m_lastPreviewCaptureAtMs = 0;
    qint64 m_lastPreviewResendAtMs = 0;
    qint64 m_lastPreviewLogAtMs = 0;
    bool m_localActivityActive = true;

    bool m_globalMicEnabled = true;
    bool m_janusAudioLoaded = false;
    bool m_janusIgnoreAlone = false;
    QString m_janusDesiredRoomOwnerId;
    QString m_janusActiveRoomOwnerId;
    QTimer *m_janusEnsureTimer = nullptr;
    QString m_janusEnsureOwnerId;
    int m_janusEnsureAttempt = 0;
    qint64 m_janusEnsureStartAtMs = 0;

    QDialog *m_audioCallDialog = nullptr;
    bool m_audioCallDialogDragging = false;
    QPoint m_audioCallDialogDragOffset;
    QPushButton *m_audioCallMuteBtn = nullptr;
    QPushButton *m_audioCallHangupBtn = nullptr;
    QPushButton *m_audioCallSpeakerBtn = nullptr;
    QTimer *m_audioCallPollTimer = nullptr;
    QString m_audioCallPeerId;
    QString m_pendingAudioCallPeerId;
    bool m_pendingAudioCallForceMic = false;
    QScrollArea *m_audioCallParticipantsArea = nullptr;
    QWidget *m_audioCallParticipantsWidget = nullptr;
    QHBoxLayout *m_audioCallParticipantsLayout = nullptr;
    VolumeLevelBar *m_localVolumeBar = nullptr;
    MicLevelMonitor *m_micMonitor = nullptr;
    bool m_audioCallSpeakerEnabled = true;

    QDialog *m_audioCallMiniBar = nullptr;
    QLabel *m_audioCallMiniLogoLabel = nullptr;
    bool m_audioCallMiniBarDragging = false;
    QPoint m_audioCallMiniBarDragOffset;
    bool m_audioCallMiniHidden = false;
    QPushButton *m_audioCallTitleRestoreBtn = nullptr;
    bool m_audioCallRestoreAvailable = false;
    
    NewUserGuide *m_userGuide = nullptr;
};
