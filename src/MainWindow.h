#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include "ui/StreamingIslandWidget.h"
#include <QLabel>
#include <QStatusBar>
#include <QTimer>
#include <QProcess>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QListWidget>
#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMenu>
#include <QAction>
#include <QRegularExpression>
#include <QLocalSocket>
#include <QLocalServer>
#include <QList>
#include <QSet>
#include <QHash>
#include <QUdpSocket>
#include "common/AutoUpdater.h"

// 前向声明
class VideoWindow;
class NewUiWindow;
class AvatarSettingsWindow;
class QSoundEffect;
class QCloseEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    NewUiWindow* transparentImageList() const;

signals:
    void appInitialized();
    void appReady();

private slots:
    void startStreaming();
    void stopStreaming();
    void updateStatus();
    void onCaptureProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onPlayerProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    
    // 登录系统相关槽函数
    void onLoginWebSocketConnected();
    void onLoginWebSocketDisconnected();
    void onLoginWebSocketTextMessageReceived(const QString &message);
    void onLoginWebSocketError(QAbstractSocket::SocketError error);
    void onUserCleanupTimerTimeout(); // 蓄水池清理定时器槽
    
    // 右键菜单相关槽函数
    void showContextMenu(const QPoint &pos);
    void onContextMenuOption1();
    void onContextMenuOption2();
    
    // 观看按钮点击事件
    void onWatchButtonClicked();
    
    // 透明图片列表相关槽函数
    void onUserImageClicked(const QString &userId, const QString &userName);
    void showMainList();
    void onSetAvatarRequested();  // 新增：设置头像槽函数
    void onSystemSettingsRequested(); // 新增：打开系统设置
    void onToggleStreamingIsland(); // 新增：切换灵动岛显示
    void onScreenSelected(int index); // 新增：屏幕选择槽
    void onAvatarSelected(int iconId);  // 新增：头像选择完成槽函数
    void onClearMarksRequested(); // 新增：清理标记槽函数
    void onExitRequested();       // 新增：退出槽函数
    void onHideRequested();
    void onAnnotationColorChanged(int colorId); // 新增：批注颜色变化持久化
    void onUserNameChanged(const QString &name);
    void onMicToggleRequested(bool enabled);
    void onSpeakerToggleRequested(bool enabled);
    
    // 自动更新槽函数
    void onUpdateAvailable(const QString &version, const QString &downloadUrl, const QString &description, bool force);
    void onUpdateDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onUpdateError(const QString &error);

private:
    void setupUI();
    void setupStatusBar();
    void startProcesses();
    void stopProcesses();
protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void closeEvent(QCloseEvent *event) override;
    
    // 配置文件相关方法
    QString getConfigFilePath() const;
public slots:
    void onLocalQualitySelected(const QString& quality);
    void onAudioOutputSelectionChanged(bool followSystem, const QString &deviceId);
    void onMicInputSelectionChanged(bool followSystem, const QString &deviceId);
    void onManualApprovalEnabledChanged(bool enabled);
    void onOnlineNotificationEnabledChanged(bool enabled);
private:
    void saveLocalQualityToConfig(const QString& quality);
    int loadOrGenerateRandomId();
    void saveRandomIdToConfig(int randomId);
    int loadOrGenerateIconId();  // 新增：读取或生成icon ID
    void saveIconIdToConfig(int iconId);  // 新增：保存icon ID到配置文件
    int loadOrGenerateColorId();  // 新增：读取或生成批注颜色ID
    void saveColorIdToConfig(int colorId); // 新增：保存批注颜色ID到配置文件
    QString getDeviceId() const;  // 获取设备ID字符串
    QString generateUniqueDeviceId() const;  // 生成唯一设备ID
    void saveDeviceIdToConfig(const QString& deviceId);  // 保存设备ID到配置文件
    QString getServerAddress() const;  // 获取服务器地址
    void saveServerAddressToConfig(const QString& serverAddress);  // 保存服务器地址到配置文件
    void saveScreenIndexToConfig(int screenIndex); // 新增：保存屏幕索引到配置
    int loadScreenIndexFromConfig() const; // 新增：读取屏幕索引
    QString loadUserNameFromConfig() const;
    void saveUserNameToConfig(const QString &name);
    bool loadAudioOutputFollowSystemFromConfig() const;
    QString loadAudioOutputDeviceIdFromConfig() const;
    void saveAudioOutputFollowSystemToConfig(bool followSystem);
    void saveAudioOutputDeviceIdToConfig(const QString &deviceId);
    bool loadMicInputFollowSystemFromConfig() const;
    QString loadMicInputDeviceIdFromConfig() const;
    void saveMicInputFollowSystemToConfig(bool followSystem);
    void saveMicInputDeviceIdToConfig(const QString &deviceId);
    bool loadSpeakerEnabledFromConfig() const;
    bool loadMicEnabledFromConfig() const;
    void saveSpeakerEnabledToConfig(bool enabled);
    void saveMicEnabledToConfig(bool enabled);
    bool loadManualApprovalEnabledFromConfig() const;
    void saveManualApprovalEnabledToConfig(bool enabled);
    bool loadOnlineNotificationEnabledFromConfig() const;
    void saveOnlineNotificationEnabledToConfig(bool enabled);
    
    // 登录系统相关方法
    void initializeLoginSystem();
    void connectToLoginServer();
    void sendLoginRequest();
    void sendHeartbeat();
    void updateUserList(const QJsonArray& users);
    void sendWatchRequest(const QString& targetDeviceId);
    void startVideoReceiving(const QString& targetDeviceId);
    void startPlayerProcess(const QString& targetDeviceId);  // 启动播放进程
    void showUserOnlineToast(const QString& userId, const QString& userName, int iconId);
    void showUserOfflineToast(const QString& userId, const QString& userName, int iconId);
    void repositionOnlineToasts();

    // 显示更新日志
    void checkAndShowUpdateLog();
    
    // 自动更新相关
    void performUpdateCheck();
    void onUpdateAvailable(const QString &version, const QString &url, const QString &desc, bool force);

private:
    void sendWatchRequestInternal(const QString& targetDeviceId, bool audioOnly);
    void sendWatchRequestWithVideo(const QString& targetDeviceId);
    void sendWatchRequestAudioOnly(const QString& targetDeviceId);
    
    // UI组件
    QWidget *m_centralWidget;
    QVBoxLayout *m_mainLayout;  // 改为垂直布局
    QListWidget *m_listWidget;
    QLabel *m_idLabel;          // 显示随机ID的标签
    QPushButton *m_watchButton; // 观看按钮
    
    // 视频窗口
    VideoWindow *m_videoWindow;
    
    NewUiWindow *m_transparentImageList;
    
    // 头像设置窗口
    AvatarSettingsWindow *m_avatarSettingsWindow;
    class SystemSettingsWindow* m_systemSettingsWindow; // 前向声明：系统设置窗口
    
    // 状态管理
    QLabel *m_statusLabel;
    bool m_isStreaming;
    bool m_isScreenSwitching = false; // 屏幕切换进行中标记
    QMetaObject::Connection m_switchFrameConn; // 首帧到达监听连接
    
    // 进程管理
    QProcess *m_captureProcess;
    QProcess *m_playerProcess;

    // 看门狗相关
    class QLocalServer *m_watchdogServer;
    QTimer *m_watchdogTimer;
    qint64 m_lastHeartbeatTime;
    QLocalSocket *m_currentWatchdogSocket = nullptr; // 当前连接的捕获进程Socket
    QByteArray m_watchdogRxBuffer;
    bool m_pendingApproval = false; // 是否有待发送的本地审批指令
    void onWatchdogNewConnection();
    void onWatchdogDataReady();
    void onWatchdogTimeout();
    
    // 熔断机制相关
    QList<qint64> m_crashTimestamps; // 记录最近崩溃的时间戳
    const int MAX_CRASH_COUNT = 3;   // 最大允许崩溃次数
    const int CRASH_WINDOW_MS = 60000; // 时间窗口（1分钟）
    bool checkCrashLoop(); // 检查是否触发熔断
    
    // 启动时间诊断
    QElapsedTimer m_startupTimer;
    
    // WebSocket服务器就绪检测
    QTimer *m_serverReadyTimer;
    int m_serverReadyRetryCount;
    QTimer *m_heartbeatTimer;
    
    // 登录系统相关成员
    QWebSocket *m_loginWebSocket;
    QString m_userId;
    QString m_userName;
    bool m_isLoggedIn;
    // 在线用户蓄水池
    QSet<QString> m_serverOnlineUsers;
    bool m_userListInitialized = false;
    QList<QWidget*> m_onlineToasts;
    QTimer *m_userCleanupTimer;
    QTimer *m_reconnectTimer; // 统一的重连定时器
    
    // 当前正在观看的目标设备ID（用于在源切换后重发watch_request）
    QString m_currentTargetId;
    QString m_pendingTalkTargetId;
    bool m_pendingTalkEnabled = false;
    bool m_pendingShowVideoWindow = true;
    QString m_audioOnlyTargetId;
    
    // 推流状态灵动岛
    StreamingIslandWidget *m_islandWidget = nullptr;

    // 等待同意弹窗
    class QMessageBox *m_waitingDialog = nullptr;
    // 收到请求时的审批弹窗
    class QMessageBox *m_approvalDialog = nullptr;
    
    // 标记是否主动取消了请求
    bool m_selfCancelled = false;

    bool m_appReadyEmitted = false;
    class QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    bool m_inSystemSuspend = false;
    bool m_exitRequested = false;

    // 提示音
    QSoundEffect *m_alertSound = nullptr;

    QUdpSocket *m_lanDiscoverySocket = nullptr;
    QHash<QString, QString> m_lanDiscoveredBaseByTarget;

    AutoUpdater *m_autoUpdater = nullptr;
    class QProgressDialog *m_updateProgressDialog = nullptr;
};

#endif // MAINWINDOW_H
