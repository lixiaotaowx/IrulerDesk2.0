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
#include "StreamClient.h"
#include "LoginClient.h"

class QStackedWidget;
class QWebEngineView;

class NewUiWindow : public QWidget
{
    Q_OBJECT

public:
    explicit NewUiWindow(QWidget *parent = nullptr);
    ~NewUiWindow();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void onTimerTimeout();
    void onTalkSpinnerTimeout();
    void onStreamLog(const QString &msg);
    void onUserListUpdated(const QJsonArray &users);
    void onLoginConnected();
    void toggleFunction1Maximize();

public:
    void setMyStreamId(const QString &id, const QString &name = QString());
    void setCaptureScreenIndex(int index);
    void setTalkPending(const QString &userId, bool pending);
    void setTalkConnected(const QString &userId, bool connected);
    void setTalkRemoteActive(const QString &userId, bool active);
    void setViewerMicState(const QString &viewerId, bool enabled);
    void setGlobalMicCheckedSilently(bool enabled);

    // Viewer List Management
    void addViewer(const QString &id, const QString &name);
    void removeViewer(const QString &id);
    void clearViewers(); // Clear all viewers
    int getViewerCount() const; // Get current viewer count
    void updateViewerNameIfExists(const QString &id, const QString &name);
    void sendKickToSubscribers(const QString &viewerId);

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

signals:
    void startWatchingRequested(const QString &targetId);
    void systemSettingsRequested();
    void micToggleRequested(bool enabled);
    void clearMarksRequested();
    void toggleStreamingIslandRequested();
    void kickViewerRequested(const QString &viewerId);
    void closeRoomRequested();
    void talkToggleRequested(const QString &targetId, bool enabled);
    void avatarPixmapUpdated(const QString &userId, const QPixmap &pixmap);

private:
    void setupUi();
    void showFunction1Browser();
    void showHomeContent();
    void updateTitleMaximizeButton();
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
    
    // Dragging support
    bool m_dragging = false;
    QPoint m_dragPosition;

    QListWidget *m_listWidget = nullptr;
    QTimer *m_timer = nullptr;
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
    QPushButton *m_titleMaximizeBtn = nullptr;
    QPushButton *m_titleMicBtn = nullptr;
    QIcon m_titleMicIconOn;
    QIcon m_titleMicIconOff;
    QStackedWidget *m_rightContentStack = nullptr;
    QWidget *m_homeContentPage = nullptr;
    QWidget *m_function1BrowserPage = nullptr;
    QWebEngineView *m_function1WebView = nullptr;
    StreamClient *m_streamClient = nullptr;
    LoginClient *m_loginClient = nullptr;
    StreamClient *m_avatarPublisher = nullptr;
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
    QMap<QString, QPushButton*> m_talkButtons;    // userId -> Talk Button (end/get)
    QMap<QString, QLabel*> m_talkOverlays;        // userId -> "通话中" overlay label
    QTimer *m_talkSpinnerTimer = nullptr;
    QMap<QString, int> m_talkSpinnerAngles;
    QMap<QString, QLabel*> m_reselectOverlays;
    QTimer *m_selectionAutoPauseTimer = nullptr;
    QString m_selectionAutoPauseUserId;
    QString m_autoPausedUserId;

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
    QMap<QString, QTimer*> m_hiFpsPublisherTimers;
    QTimer *m_hiFpsWatchdogTimer = nullptr;
    qint64 m_hiFpsLastFrameAtMs = 0;

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
};
