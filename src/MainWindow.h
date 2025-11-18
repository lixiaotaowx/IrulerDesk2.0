#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
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

// 前向声明
class VideoWindow;
class TransparentImageList;
class AvatarSettingsWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

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
    void onScreenSelected(int index); // 新增：屏幕选择槽
    void onAvatarSelected(int iconId);  // 新增：头像选择完成槽函数
    void onClearMarksRequested(); // 新增：清理标记槽函数
    void onExitRequested();       // 新增：退出槽函数
    void onAnnotationColorChanged(int colorId); // 新增：批注颜色变化持久化

private:
    void setupUI();
    void setupStatusBar();
    void startProcesses();
    void stopProcesses();
    
    // 配置文件相关方法
    QString getConfigFilePath() const;
public slots:
    void onLocalQualitySelected(const QString& quality);
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
    
    // 登录系统相关方法
    void initializeLoginSystem();
    void connectToLoginServer();
    void sendLoginRequest();
    void updateUserList(const QJsonArray& users);
    void sendWatchRequest(const QString& targetDeviceId);
    void startVideoReceiving(const QString& targetDeviceId);
    void startPlayerProcess(const QString& targetDeviceId);  // 启动播放进程
private:
    
    // UI组件
    QWidget *m_centralWidget;
    QVBoxLayout *m_mainLayout;  // 改为垂直布局
    QListWidget *m_listWidget;
    QLabel *m_idLabel;          // 显示随机ID的标签
    QPushButton *m_watchButton; // 观看按钮
    
    // 视频窗口
    VideoWindow *m_videoWindow;
    
    // 透明图片列表
    TransparentImageList *m_transparentImageList;
    
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
    
    // 启动时间诊断
    QElapsedTimer m_startupTimer;
    
    // WebSocket服务器就绪检测
    QTimer *m_serverReadyTimer;
    int m_serverReadyRetryCount;
    
    // 登录系统相关成员
    QWebSocket *m_loginWebSocket;
    QString m_userId;
    QString m_userName;
    bool m_isLoggedIn;
    // 当前正在观看的目标设备ID（用于在源切换后重发watch_request）
    QString m_currentTargetId;
    bool m_appReadyEmitted = false;
};

#endif // MAINWINDOW_H