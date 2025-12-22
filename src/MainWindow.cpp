#include "MainWindow.h"
#include "VideoWindow.h"
#include <QSoundEffect>
#include <QUrl>
#include "NewUi/NewUiWindow.h"
#include "video_components/VideoDisplayWidget.h"
#include "ui/ScreenAnnotationWidget.h"
#include "ui/AvatarSettingsWindow.h"
#include "ui/SystemSettingsWindow.h"
#include "ui/FirstLaunchWizard.h"
#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QIcon>
#include <QMessageBox>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QTimer>
#include <QTcpSocket>
#include <QTextStream>
#include <QNetworkInterface>
#include <QHostInfo>
#include <QCryptographicHash>
#include <cstdlib>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
namespace {
    static HANDLE g_hJob = NULL;
    void AddProcessToJob(qint64 pid) {
        if (g_hJob == NULL) {
            g_hJob = CreateJobObject(NULL, NULL);
            if (g_hJob) {
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = { 0 };
                jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                SetInformationJobObject(g_hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
            }
        }
        if (g_hJob && pid) {
            HANDLE hProcess = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, (DWORD)pid);
            if (hProcess) {
                AssignProcessToJobObject(g_hJob, hProcess);
                CloseHandle(hProcess);
            }
        }
    }
}
#endif

#include <QLocalServer>
#include <QLocalSocket>
#include <QDateTime>
#include <QSettings>
#include <QTextEdit>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_centralWidget(nullptr)
    , m_mainLayout(nullptr)
    , m_listWidget(nullptr)
    , m_idLabel(nullptr)
    , m_watchButton(nullptr)
    , m_videoWindow(nullptr)
    , m_transparentImageList(nullptr)
    , m_avatarSettingsWindow(nullptr)
    , m_systemSettingsWindow(nullptr)
    , m_statusLabel(nullptr)
    , m_isStreaming(false)
    , m_captureProcess(nullptr)
    , m_playerProcess(nullptr)
    , m_serverReadyTimer(nullptr)
    , m_serverReadyRetryCount(0)
    , m_loginWebSocket(nullptr)
    , m_isLoggedIn(false)
    , m_watchdogServer(nullptr)
    , m_watchdogTimer(nullptr)
    , m_lastHeartbeatTime(0)
    , m_pendingApproval(false)
{
    
    
    // 初始化随机数种子
    srand(static_cast<unsigned int>(time(nullptr)));
    
    {
        QString cfg = getConfigFilePath();
        QFile f(cfg);
        if (!f.exists()) {
            FirstLaunchWizard w(this);
            if (w.exec() == QDialog::Accepted) {
                QString n = w.userName().trimmed();
                if (!n.isEmpty()) { saveUserNameToConfig(n); m_userName = n; }
                int si = w.screenIndex(); if (si >= 0) saveScreenIndexToConfig(si);
            }
        }
    }

    setupUI();
    
    setupStatusBar();

    if (!m_trayIcon) {
        QString appDir = QCoreApplication::applicationDirPath();
        m_trayIcon = new QSystemTrayIcon(QIcon(QString("%1/maps/logo/iruler.ico").arg(appDir)), this);
        m_trayMenu = new QMenu();
        QAction *showAction = m_trayMenu->addAction(QStringLiteral("显示主窗口"));
        QAction *exitAction = m_trayMenu->addAction(QStringLiteral("退出"));
        connect(showAction, &QAction::triggered, this, [this]() {
            if (m_transparentImageList) { m_transparentImageList->show(); m_transparentImageList->raise(); }
        });
        connect(exitAction, &QAction::triggered, this, &MainWindow::onExitRequested);
        m_trayIcon->setContextMenu(m_trayMenu);
        connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
            if (r == QSystemTrayIcon::DoubleClick) {
                if (m_transparentImageList) { m_transparentImageList->show(); m_transparentImageList->raise(); }
            }
        });
        m_trayIcon->show();
    }

    // 初始化登录系统
    initializeLoginSystem();
    
    // 自动开始流媒体传输
    // QTimer::singleShot(1000, this, &MainWindow::startStreaming);
    
    // 检查并显示更新日志
    QTimer::singleShot(500, this, &MainWindow::checkAndShowUpdateLog);
}

void MainWindow::checkAndShowUpdateLog()
{
    // 获取应用程序版本号（从main.cpp中设置的）
    const QString CURRENT_VERSION = QCoreApplication::applicationVersion();
    
    QSettings settings("ScreenStream", "ScreenStreamApp");
    QString lastVersion = settings.value("app_version", "").toString();
    
    // 如果是首次安装（lastVersion为空）或版本更新
    // 注意：如果是首次安装，也应该弹出日志，让用户知道这个版本的特性
    // 逻辑：lastVersion为空 -> 不等于CURRENT_VERSION -> 进入分支 -> 弹出 -> 写入当前版本
    if (lastVersion != CURRENT_VERSION) {
        // 更新存储的版本号
        settings.setValue("app_version", CURRENT_VERSION);
        
        // 创建大字体的更新日志窗口
        QDialog *logDialog = new QDialog(this);
        logDialog->setWindowTitle(QStringLiteral("更新日志 / Update Log"));
        logDialog->setMinimumSize(600, 400);
        logDialog->setWindowFlags(logDialog->windowFlags() & ~Qt::WindowContextHelpButtonHint); // 移除问号按钮
        
        QVBoxLayout *layout = new QVBoxLayout(logDialog);
        
        QLabel *title = new QLabel(QStringLiteral("✨ 新版本更新说明 ✨"), logDialog);
        QFont titleFont = title->font();
        titleFont.setPointSize(20);
        titleFont.setBold(true);
        title->setFont(titleFont);
        title->setAlignment(Qt::AlignCenter);
        title->setStyleSheet("color: #4CAF50; margin-bottom: 10px;");
        layout->addWidget(title);
        
        QTextEdit *content = new QTextEdit(logDialog);
        content->setReadOnly(true);
        QFont contentFont = content->font();
        contentFont.setPointSize(14); // 大字体
        content->setFont(contentFont);
        
        // 读取外部日志文件内容
        QString html;
        QString logPath = QCoreApplication::applicationDirPath() + "/UpdateLog.html";
        // 开发环境路径兼容（如果需要）
        if (!QFile::exists(logPath)) {
             logPath = QCoreApplication::applicationDirPath() + "/../../src/UpdateLog.html";
        }

        QFile logFile(logPath);
        if (logFile.exists() && logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            html = QString::fromUtf8(logFile.readAll());
            logFile.close();
        } else {
            // 文件读取失败时的默认显示
            html = R"(
                <body style="background-color:#2b2b2b; color:#ffffff;">
                <p>暂无更新说明。</p>
                </body>
            )";
        }
        
        content->setHtml(html);
        content->setStyleSheet("QTextEdit { border: none; background-color: #2b2b2b; }");
        layout->addWidget(content);
        
        QPushButton *btn = new QPushButton(QStringLiteral("我知道了 / Got it"), logDialog);
        btn->setMinimumHeight(50);
        QFont btnFont = btn->font();
        btnFont.setPointSize(12);
        btnFont.setBold(true);
        btn->setFont(btnFont);
        btn->setStyleSheet(
            "QPushButton { "
            "   background-color: #2196F3; "
            "   color: white; "
            "   border-radius: 5px; "
            "   border: none; "
            "}"
            "QPushButton:hover { background-color: #1976D2; }"
            "QPushButton:pressed { background-color: #0D47A1; }"
        );
        connect(btn, &QPushButton::clicked, logDialog, &QDialog::accept);
        layout->addWidget(btn);
        
        // 模态显示
        logDialog->exec();
        delete logDialog;
    }
}

#ifdef _WIN32
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType);
    MSG *msg = static_cast<MSG*>(message);
    if (msg) {
        if (msg->message == WM_POWERBROADCAST) {
            if (msg->wParam == PBT_APMSUSPEND) {
                m_inSystemSuspend = true;
                return true;
            } else if (msg->wParam == PBT_APMRESUMESUSPEND || msg->wParam == PBT_APMRESUMECRITICAL || msg->wParam == PBT_APMRESUMEAUTOMATIC) {
                m_inSystemSuspend = false;
                return true;
            }
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

NewUiWindow* MainWindow::transparentImageList() const
{
    return m_transparentImageList;
}

void MainWindow::sendWatchRequest(const QString& targetDeviceId)
{
    sendWatchRequestWithVideo(targetDeviceId);
}

void MainWindow::sendWatchRequestWithVideo(const QString& targetDeviceId)
{
    m_pendingShowVideoWindow = true;
    m_audioOnlyTargetId.clear();
    sendWatchRequestInternal(targetDeviceId, false);
}

void MainWindow::sendWatchRequestAudioOnly(const QString& targetDeviceId)
{
    m_pendingShowVideoWindow = false;
    m_audioOnlyTargetId = targetDeviceId;
    sendWatchRequestInternal(targetDeviceId, true);
}

void MainWindow::sendWatchRequestInternal(const QString& targetDeviceId, bool audioOnly)
{
    if (!m_loginWebSocket || m_loginWebSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    // 记录当前正在观看的目标设备ID，便于在源切换后重发
    m_currentTargetId = targetDeviceId;
    
    // 构建观看请求消息
    QJsonObject watchRequest;
    watchRequest["type"] = "watch_request";
    if (audioOnly) {
        watchRequest["audio_only"] = true;
        watchRequest["action"] = "audio_only";
    }
    watchRequest["viewer_id"] = getDeviceId();
    watchRequest["target_id"] = targetDeviceId;
    watchRequest["viewer_name"] = m_userName;
    watchRequest["viewer_icon_id"] = loadOrGenerateIconId();
    
    QJsonDocument doc(watchRequest);
    QString message = doc.toJson(QJsonDocument::Compact);
    m_loginWebSocket->sendTextMessage(message);

    if (audioOnly) {
        return;
    }
    
    // 显示非模态等待对话框
    if (m_waitingDialog) {
        m_waitingDialog->close();
        delete m_waitingDialog;
        m_waitingDialog = nullptr;
    }
    m_waitingDialog = new QMessageBox(this);
    m_waitingDialog->setWindowFlags(m_waitingDialog->windowFlags() | Qt::WindowStaysOnTopHint);
    m_waitingDialog->setWindowTitle(QStringLiteral("等待同意"));
    m_waitingDialog->setText(QStringLiteral("已发送请求，等待对方同意..."));
    
    // 添加挂断按钮
    QPushButton *hangupBtn = m_waitingDialog->addButton(QStringLiteral("挂断"), QMessageBox::RejectRole);
    
    connect(hangupBtn, &QPushButton::clicked, this, [this, targetDeviceId]() {
        // 标记主动取消
        m_selfCancelled = true;

        // 发送取消请求
        qDebug() << "Sending watch_request (cancel) to:" << targetDeviceId;
        QJsonObject cancelMsg;
        cancelMsg["type"] = "watch_request_canceled";
        cancelMsg["viewer_id"] = getDeviceId();
        cancelMsg["target_id"] = targetDeviceId;
        cancelMsg["viewer_name"] = m_userName;
        QJsonDocument doc(cancelMsg);
        if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
            m_loginWebSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
        }

        // 关闭对话框
        if (m_waitingDialog) {
            m_waitingDialog->close();
            m_waitingDialog->deleteLater();
            m_waitingDialog = nullptr;
        }
    });

    m_waitingDialog->setModal(false); // 非模态，不阻塞UI事件循环
    m_waitingDialog->show();
    m_waitingDialog->raise();
    m_waitingDialog->activateWindow();
}

void MainWindow::startVideoReceiving(const QString& targetDeviceId)
{
    if (!m_videoWindow) {
        return;
    }
    
    VideoDisplayWidget* videoWidget = m_videoWindow->getVideoDisplayWidget();
    if (!videoWidget) {
        return;
    }
    
    // 构建WebSocket连接URL，包含目标设备ID
    QString serverAddress = getServerAddress();
    QString serverUrl = QString("ws://%1/subscribe/%2").arg(serverAddress, targetDeviceId);
    
    
    // 初始化批注颜色（从配置加载）
    int initialColorId = loadOrGenerateColorId();
    videoWidget->setAnnotationColorId(initialColorId);

    // 传入用户名
    videoWidget->setViewerName(m_userName);
    // 使用VideoDisplayWidget开始接收视频流
    
    videoWidget->startReceiving(serverUrl);
    QString viewerId = getDeviceId();
    videoWidget->setSessionInfo(viewerId, targetDeviceId);

    bool spkEnabled = loadSpeakerEnabledFromConfig();
    bool micEnabled = loadMicEnabledFromConfig();
    if (m_pendingTalkEnabled && m_pendingTalkTargetId == targetDeviceId) {
        micEnabled = true;
    }
    m_videoWindow->setSpeakerChecked(spkEnabled);
    m_videoWindow->setMicCheckedSilently(micEnabled);
    videoWidget->setSpeakerEnabled(spkEnabled);
    videoWidget->setMicSendEnabled(micEnabled);
    videoWidget->setTalkEnabled(micEnabled);
}

void MainWindow::startPlayerProcess(const QString& targetDeviceId)
{
    // 如果播放进程已经在运行，先停止它
    if (m_playerProcess && m_playerProcess->state() != QProcess::NotRunning) {
        m_playerProcess->kill();
        m_playerProcess->waitForFinished(3000);
    }
    
    if (!m_playerProcess) {
        m_playerProcess = new QProcess(this);
        connect(m_playerProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &MainWindow::onPlayerProcessFinished);
        m_playerProcess->setProcessChannelMode(QProcess::SeparateChannels);
    }
    
    QString playerPath = QCoreApplication::applicationDirPath() + "/PlayerProcess.exe";
    QStringList arguments;
    arguments << targetDeviceId;  // 传递目标设备ID作为参数
    
#ifdef _WIN32
    // Windows下将播放进程加入Job Object，确保主进程崩溃时播放进程自动退出
    connect(m_playerProcess, &QProcess::started, this, [this]() {
        if (m_playerProcess) {
             AddProcessToJob(m_playerProcess->processId());
        }
    });
#endif

    m_playerProcess->start(playerPath, arguments);
    
    if (!m_playerProcess->waitForStarted(5000)) {
    } else {
    }
}

MainWindow::~MainWindow()
{
    if (m_loginWebSocket) {
        m_loginWebSocket->close();
        m_loginWebSocket->deleteLater();
    }
    stopProcesses();
}

void MainWindow::onWatchButtonClicked()
{
    
    // 检查是否有选中的用户
    QListWidgetItem *currentItem = m_listWidget->currentItem();
    if (!currentItem) {
        return;
    }
    
    QString selectedUser = currentItem->text();
    
    // 从项目文本中提取设备ID (格式: "用户名 (设备ID)")
    QRegularExpression regex("\\(([^)]+)\\)");
    QRegularExpressionMatch match = regex.match(selectedUser);
    if (match.hasMatch()) {
        QString targetDeviceId = match.captured(1);
        
        // 显示视频窗口
        if (m_videoWindow) {
            m_videoWindow->show();
            m_videoWindow->raise();
            m_videoWindow->activateWindow();
        }
        
        // 发送观看请求并开始视频接收
        sendWatchRequest(targetDeviceId);
        
    } else {
    }
}

void MainWindow::setupUI()
{
    setWindowTitle("屏幕流媒体系统 - 用户列表");
    setMinimumSize(400, 600);
    resize(450, 700);
    
    // 创建中央部件
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    // 创建主垂直布局
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setSpacing(20);
    m_mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // 设置窗口样式
    setStyleSheet(
        "QMainWindow {"
        "    background-color: #1a1a1a;"
        "}"
        "QWidget {"
        "    background-color: #1a1a1a;"
        "}"
    );
    
    // 创建标题标签
    QLabel *titleLabel = new QLabel("在线用户", this);
    titleLabel->setStyleSheet(
        "QLabel {"
        "    color: #ffffff;"
        "    font-size: 20px;"
        "    font-weight: bold;"
        "    padding: 15px;"
        "    background-color: #2a2a2a;"
        "    border-radius: 10px;"
        "    text-align: center;"
        "}"
    );
    titleLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(titleLabel);
    
    // 创建列表组件
    m_listWidget = new QListWidget(this);
    m_listWidget->setMinimumHeight(300);
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listWidget->setStyleSheet(
        "QListWidget {"
        "    background-color: #2a2a2a;"
        "    border: 2px solid #444;"
        "    border-radius: 10px;"
        "    color: white;"
        "    font-size: 14px;"
        "    padding: 10px;"
        "}"
        "QListWidget::item {"
        "    padding: 15px;"
        "    margin: 5px;"
        "    border-radius: 8px;"
        "    background-color: #3a3a3a;"
        "}"
        "QListWidget::item:hover {"
        "    background-color: #4a4a4a;"
        "}"
        "QListWidget::item:selected {"
        "    background-color: #42a5f5;"
        "    color: white;"
        "}"
    );
    
    // 连接右键菜单信号
    connect(m_listWidget, &QListWidget::customContextMenuRequested,
            this, &MainWindow::showContextMenu);
    
    // 连接列表项选择变化信号，用于启用/禁用观看按钮
    connect(m_listWidget, &QListWidget::itemSelectionChanged, this, [this]() {
        bool hasSelection = m_listWidget->currentItem() != nullptr;
        QString currentText = hasSelection ? m_listWidget->currentItem()->text() : "";
        // 只有选中了有效用户（不是"等待连接服务器..."）时才启用按钮
        bool enableButton = hasSelection && !currentText.contains("等待连接") && !currentText.isEmpty();
        m_watchButton->setEnabled(enableButton);
    });
    
    // 添加示例项目
    m_listWidget->addItem("等待连接服务器...");
    
    m_mainLayout->addWidget(m_listWidget, 1);
    
    // 创建观看按钮
    m_watchButton = new QPushButton("开始观看", this);
    m_watchButton->setStyleSheet(
        "QPushButton {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "                stop:0 #4caf50, stop:1 #45a049);"
        "    border: none;"
        "    border-radius: 10px;"
        "    color: white;"
        "    font-size: 16px;"
        "    font-weight: bold;"
        "    padding: 15px;"
        "    min-height: 20px;"
        "}"
        "QPushButton:hover {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "                stop:0 #5cbf60, stop:1 #4caf50);"
        "}"
        "QPushButton:pressed {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "                stop:0 #45a049, stop:1 #3d8b40);"
        "}"
        "QPushButton:disabled {"
        "    background-color: #666;"
        "    color: #999;"
        "}"
    );
    m_watchButton->setEnabled(false); // 初始状态禁用
    connect(m_watchButton, &QPushButton::clicked, this, &MainWindow::onWatchButtonClicked);
    
    m_mainLayout->addWidget(m_watchButton);
    
    // 创建随机ID显示标签
    m_idLabel = new QLabel(this);
    m_idLabel->setMinimumHeight(60);
    
    // 从配置文件加载或生成随机5位数ID
    int randomId = loadOrGenerateRandomId();
    QString idText = QString("我的ID: %1").arg(randomId);
    m_idLabel->setText(idText);
    
    m_idLabel->setStyleSheet(
        "QLabel {"
        "    background-color: #1a4d3a;"
        "    border: 2px solid #4caf50;"
        "    border-radius: 10px;"
        "    color: #4caf50;"
        "    font-size: 16px;"
        "    font-weight: bold;"
        "    padding: 20px;"
        "    text-align: center;"
        "}"
    );
    m_idLabel->setAlignment(Qt::AlignCenter);
    
    m_mainLayout->addWidget(m_idLabel);
    
    
    // 创建视频窗口（但不显示）
    m_videoWindow = new VideoWindow();
    {
        int initialColorId = loadOrGenerateColorId();
        if (auto vd = m_videoWindow->getVideoDisplayWidget()) {
            vd->setAnnotationColorId(initialColorId);
            connect(vd, &VideoDisplayWidget::annotationColorChanged,
                    this, &MainWindow::onAnnotationColorChanged);
            connect(vd, &VideoDisplayWidget::audioOutputSelectionChanged,
                    this, &MainWindow::onAudioOutputSelectionChanged);
            connect(vd, &VideoDisplayWidget::micInputSelectionChanged,
                    this, &MainWindow::onMicInputSelectionChanged);
            connect(vd, &VideoDisplayWidget::avatarUpdateReceived,
                    this, [this](const QString &userId, int iconId) {
                if (m_transparentImageList) {
                     m_transparentImageList->updateUserAvatar(userId, iconId);
                }
            });

            vd->selectAudioOutputFollowSystem();
            bool micFollow = loadMicInputFollowSystemFromConfig();
            QString micId = loadMicInputDeviceIdFromConfig();
            if (micFollow || micId.isEmpty()) { vd->selectMicInputFollowSystem(); }
            else { vd->selectMicInputById(micId); }
            bool spkEnabled = loadSpeakerEnabledFromConfig();
            bool micEnabled = loadMicEnabledFromConfig();
            m_videoWindow->setSpeakerChecked(spkEnabled);
            m_videoWindow->setMicChecked(micEnabled);
            vd->setSpeakerEnabled(spkEnabled);
            vd->setTalkEnabled(micEnabled);
            vd->setMicSendEnabled(micEnabled);
        }
    }
    connect(m_videoWindow, &VideoWindow::micToggled, this, &MainWindow::onMicToggleRequested);
    
    // 创建新UI窗口
    m_transparentImageList = new NewUiWindow();
    
    // 设置当前用户信息
    m_transparentImageList->setMyStreamId(getDeviceId(), m_userName.isEmpty() ? "Me" : m_userName);
    m_transparentImageList->setCaptureScreenIndex(loadScreenIndexFromConfig());
    
    // 连接观看请求信号
    connect(m_transparentImageList, &NewUiWindow::startWatchingRequested,
            this, &MainWindow::sendWatchRequest);
            
    // Connect system settings signal
    connect(m_transparentImageList, &NewUiWindow::systemSettingsRequested,
            this, &MainWindow::onSystemSettingsRequested);
    connect(m_transparentImageList, &NewUiWindow::micToggleRequested,
            this, &MainWindow::onMicToggleRequested);
    connect(m_transparentImageList, &NewUiWindow::clearMarksRequested,
            this, &MainWindow::onClearMarksRequested);
    connect(m_transparentImageList, &NewUiWindow::toggleStreamingIslandRequested,
            this, &MainWindow::onToggleStreamingIsland);

    connect(m_transparentImageList, &NewUiWindow::kickViewerRequested,
            this, [this](const QString &viewerId) {
        if (!m_loginWebSocket) {
            qInfo().noquote() << "[KickDiag] kick_viewer not sent: login socket is null"
                              << " viewer_id=" << viewerId;
            return;
        }
        if (m_loginWebSocket->state() != QAbstractSocket::ConnectedState) {
            qInfo().noquote() << "[KickDiag] kick_viewer not sent: login socket not connected"
                              << " state=" << static_cast<int>(m_loginWebSocket->state())
                              << " viewer_id=" << viewerId;
            return;
        }
        QJsonObject msg;
        msg["type"] = "kick_viewer";
        msg["viewer_id"] = viewerId;
        msg["target_id"] = getDeviceId();
        msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
        QString payload = QJsonDocument(msg).toJson(QJsonDocument::Compact);
        qint64 bytes = m_loginWebSocket->sendTextMessage(payload);
        qInfo().noquote() << "[KickDiag] kick_viewer sent"
                          << " bytes=" << bytes
                          << " payload=" << payload;
        if (m_transparentImageList) {
            m_transparentImageList->removeViewer(viewerId);
            m_transparentImageList->sendKickToSubscribers(viewerId);
        }
    });

    connect(m_transparentImageList, &NewUiWindow::closeRoomRequested,
            this, [this]() {
        qInfo().noquote() << "[KickDiag] close_room requested"
                          << " my_id=" << getDeviceId()
                          << " is_streaming=" << m_isStreaming;
        stopStreaming();
    });

    connect(m_transparentImageList, &NewUiWindow::talkToggleRequested,
            this, [this](const QString &targetId, bool enabled) {
        auto sendViewerMicState = [this](const QString &toTargetId, bool on) {
            if (toTargetId.isEmpty()) return;
            if (!m_loginWebSocket || m_loginWebSocket->state() != QAbstractSocket::ConnectedState) return;
            QJsonObject msg;
            msg["type"] = "viewer_mic_state";
            msg["viewer_id"] = getDeviceId();
            msg["target_id"] = toTargetId;
            msg["enabled"] = on;
            msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
            m_loginWebSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
        };

        auto applyTalk = [this](bool on) {
            if (!m_videoWindow) return;
            auto *vd = m_videoWindow->getVideoDisplayWidget();
            if (!vd) return;
            vd->setTalkEnabled(on);
            vd->setMicSendEnabled(on);
            m_videoWindow->setMicCheckedSilently(on);
        };

        if (enabled) {
            sendViewerMicState(targetId, true);
            m_pendingTalkTargetId = targetId;
            m_pendingTalkEnabled = true;
            bool hasSession = false;
            if (m_videoWindow) {
                if (auto *vd = m_videoWindow->getVideoDisplayWidget()) {
                    hasSession = vd->isReceiving() && (m_currentTargetId == targetId);
                }
            }
            if (hasSession) {
                applyTalk(true);
                if (m_transparentImageList) {
                    m_transparentImageList->setTalkConnected(targetId, true);
                }
            } else {
                m_currentTargetId = targetId;
                m_audioOnlyTargetId = targetId;
                startVideoReceiving(targetId);
                if (m_transparentImageList) {
                    m_transparentImageList->setTalkPending(targetId, true);
                }
                sendWatchRequestAudioOnly(targetId);
            }
        } else {
            sendViewerMicState(targetId, false);
            if (m_transparentImageList) {
                m_transparentImageList->setTalkConnected(targetId, false);
            }
            if (m_pendingTalkTargetId == targetId) {
                m_pendingTalkTargetId.clear();
                m_pendingTalkEnabled = false;
            }
            if (m_currentTargetId == targetId) {
                applyTalk(false);
                if (m_audioOnlyTargetId == targetId) {
                    if (m_videoWindow) {
                        if (auto *vd = m_videoWindow->getVideoDisplayWidget()) {
                            vd->stopReceiving(false);
                        }
                    }
                    m_audioOnlyTargetId.clear();
                }
            }
        }
    });

    // Initialize Streaming Island
    m_islandWidget = new StreamingIslandWidget(nullptr); 
    connect(m_islandWidget, &StreamingIslandWidget::stopStreamingRequested, this, &MainWindow::stopStreaming);
}

void MainWindow::setupStatusBar()
{
    m_statusLabel = new QLabel("就绪", this);
    m_statusLabel->setStyleSheet(
        "QLabel {"
        "    color: #4caf50;"
        "    font-weight: bold;"
        "    padding: 5px;"
        "}"
    );
    statusBar()->addWidget(m_statusLabel);
    
    // 创建状态更新定时器（每秒更新状态显示）
    QTimer *statusTimer = new QTimer(this);
    connect(statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    statusTimer->start(1000);
}

void MainWindow::startStreaming()
{
    
    if (m_isStreaming) {
        return;
    }
    
    // 手动启动时，重置崩溃记录，给用户新的机会
    m_crashTimestamps.clear();
    
    m_statusLabel->setText("正在启动流媒体...");
    m_statusLabel->setStyleSheet(
        "QLabel {"
        "    color: #ff9800;"
        "    font-weight: bold;"
        "    padding: 5px;"
        "}"
    );
    
    startProcesses();
    
    m_isStreaming = true;

    // [User Request] 取消灵动岛与推流的自动关联
    // if (m_islandWidget) {
    //     int screenIndex = loadScreenIndexFromConfig();
    //     const auto screens = QGuiApplication::screens();
    //     if (screenIndex >= 0 && screenIndex < screens.size()) {
    //         m_islandWidget->setTargetScreen(screens[screenIndex]);
    //     }
    //     m_islandWidget->showOnScreen();
    // }
}

void MainWindow::stopStreaming()
{
    if (!m_isStreaming) {
        return;
    }
    
    // [Fix] Do NOT force clear viewers here.
    // Let individual viewer_exit events handle removal.
    // m_transparentImageList->clearViewers(); is removed as per user request.

    m_statusLabel->setText("正在停止流媒体...");
    m_statusLabel->setStyleSheet(
        "QLabel {"
        "    color: #ff9800;"
        "    font-weight: bold;"
        "    padding: 5px;"
        "}"
    );
    
    stopProcesses();
    
    m_isStreaming = false;

    // [User Request] 取消灵动岛与推流的自动关联
    // if (m_islandWidget) {
    //     m_islandWidget->hide();
    // }
    
    m_statusLabel->setText("已停止");
    m_statusLabel->setStyleSheet(
        "QLabel {"
        "    color: #f44336;"
        "    font-weight: bold;"
        "    padding: 5px;"
        "}"
    );
}

void MainWindow::updateStatus()
{
    QString status = QString("设备ID: %1 | 状态: %2")
                         .arg(getDeviceId())
                         .arg(m_isStreaming ? "推流中" : "空闲");
    
    m_statusLabel->setText(status);
}

void MainWindow::startProcesses()
{
    // 启动时间诊断 - 开始计时
    m_startupTimer.start();
    
    QString appDir = QApplication::applicationDirPath();
    
    // 直接启动捕获进程，连接到腾讯云服务器
    
    
    m_captureProcess = new QProcess(this);
    connect(m_captureProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onCaptureProcessFinished);
    // 将子进程标准输出/错误直接转发到当前终端
    m_captureProcess->setProcessChannelMode(QProcess::SeparateChannels);
    
    // -------------------------------------------------------------------------
    // 看门狗设置 (Watchdog Setup)
    // -------------------------------------------------------------------------
    if (m_watchdogServer) {
        m_watchdogServer->close();
        m_watchdogServer->deleteLater();
    }
    m_watchdogServer = new QLocalServer(this);
    // 生成唯一管道名称：IrulerWatchdog_{RandomID}_{TimeStamp}
    QString pipeName = QString("IrulerWatchdog_%1_%2").arg(getDeviceId()).arg(QDateTime::currentMSecsSinceEpoch());
    
    // 如果存在旧管道先移除（Windows下通常自动处理，Linux下可能需要）
    QLocalServer::removeServer(pipeName);
    
    if (m_watchdogServer->listen(pipeName)) {
        connect(m_watchdogServer, &QLocalServer::newConnection, this, &MainWindow::onWatchdogNewConnection);
        // qDebug() << "[Watchdog] Server started, pipe name:" << pipeName;
    } else {
        // qDebug() << "[Watchdog] Failed to start server:" << m_watchdogServer->errorString();
    }
    
    // 初始化心跳时间
    m_lastHeartbeatTime = QDateTime::currentMSecsSinceEpoch();
    
    // 启动看门狗定时器 (每1秒检查一次)
    if (m_watchdogTimer) {
        m_watchdogTimer->stop();
        delete m_watchdogTimer;
    }
    m_watchdogTimer = new QTimer(this);
    connect(m_watchdogTimer, &QTimer::timeout, this, &MainWindow::onWatchdogTimeout);
    m_watchdogTimer->start(1000); 
    // -------------------------------------------------------------------------

    QString captureExe = appDir + "/CaptureProcess.exe";
    
    
    if (!QFile::exists(captureExe)) {
        QMessageBox::warning(this, "错误", "捕获进程文件不存在: " + captureExe);
        return;
    }

#ifdef _WIN32
    // Windows下将子进程加入Job Object，确保主进程崩溃时子进程自动退出
    connect(m_captureProcess, &QProcess::started, this, [this]() {
        if (m_captureProcess) {
             AddProcessToJob(m_captureProcess->processId());
        }
    });
#endif
    
    // 传递看门狗管道名称给子进程
    QStringList args;
    args << "--watchdog" << pipeName;
    m_captureProcess->start(captureExe, args);
    
    // 异步启动捕获进程，不等待启动完成
    
}

void MainWindow::stopProcesses()
{
    // 停止看门狗
    if (m_watchdogTimer) {
        m_watchdogTimer->stop();
        delete m_watchdogTimer;
        m_watchdogTimer = nullptr;
    }
    if (m_watchdogServer) {
        m_watchdogServer->close();
        m_watchdogServer->deleteLater();
        m_watchdogServer = nullptr;
    }

    // 停止捕获进程
    if (m_captureProcess) {
        // [Fix] Use kill() to avoid 3s freeze on Windows where terminate() (WM_CLOSE) is ignored
        m_captureProcess->kill();
        m_captureProcess->waitForFinished(100); 
        m_captureProcess->deleteLater();
        m_captureProcess = nullptr;
    }
    
    // 停止播放进程
    if (m_playerProcess) {
        m_playerProcess->kill();
        m_playerProcess->waitForFinished(100);
        m_playerProcess->deleteLater();
        m_playerProcess = nullptr;
    }
}

void MainWindow::onCaptureProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    // 输出已通过 ForwardedChannels 直接转发到控制台，这里无需读取缓冲
    
    if (m_isStreaming) {
        // 如果是正常退出（通常是用户点击停止），不做特殊处理，stopStreaming() 会处理状态
        // 但如果是 CrashExit（被看门狗杀死或崩溃），我们需要处理
        if (exitStatus == QProcess::CrashExit) {
            
            // 检查熔断机制
            if (checkCrashLoop()) {
                QString errorMsg = "捕获服务启动失败：1分钟内连续崩溃超过3次，已停止重试。";
                m_statusLabel->setText("严重错误：服务无法启动");
                m_statusLabel->setStyleSheet("QLabel { color: #f44336; font-weight: bold; padding: 5px; }");
                
                if (m_trayIcon) {
                    m_trayIcon->showMessage("服务启动失败", errorMsg, QSystemTrayIcon::Critical, 5000);
                }
                
                QMessageBox::critical(this, "严重错误", errorMsg + "\n请检查设备驱动或重新安装程序。");
                
                // 彻底停止
                stopStreaming();
                return;
            }

            // 尝试自动重启
            QString msg = QString("捕获进程异常退出，正在尝试重启... (重试 %1/%2)").arg(m_crashTimestamps.size()).arg(MAX_CRASH_COUNT);
            m_statusLabel->setText(msg);
            m_statusLabel->setStyleSheet("QLabel { color: #ff9800; font-weight: bold; padding: 5px; }");
            
            if (m_trayIcon) {
                m_trayIcon->showMessage("服务异常", "捕获进程异常退出，正在尝试自动恢复...", QSystemTrayIcon::Warning, 2000);
            }
            
            // 重新启动进程（复用 startProcesses 逻辑）
            // 注意：需要先清理旧进程句柄（虽然 finished 信号触发意味着进程已死，但对象还在）
            // stopProcesses 会做清理，但也会停止看门狗，所以我们需要重新调用 startProcesses
            
            // 简单延时一下再重启，避免瞬间频繁重启
            QTimer::singleShot(1000, this, [this]() {
                if (m_isStreaming) { // 确保用户没有在延时期间点了停止
                    // 先清理旧资源
                    stopProcesses(); 
                    // 重新启动
                    startProcesses();
                }
            });
            
        } else {
            m_statusLabel->setText("屏幕捕获进程已退出");
            m_statusLabel->setStyleSheet(
                "QLabel {"
                "    color: #f44336;"
                "    font-weight: bold;"
                "    padding: 5px;"
                "}"
            );
        }
    }
}

// 检查是否触发熔断
bool MainWindow::checkCrashLoop()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_crashTimestamps.append(now);
    
    // 清理超出时间窗口的旧记录
    while (!m_crashTimestamps.isEmpty() && (now - m_crashTimestamps.first() > CRASH_WINDOW_MS)) {
        m_crashTimestamps.removeFirst();
    }
    
    // 检查次数是否超标
    if (m_crashTimestamps.size() > MAX_CRASH_COUNT) {
        return true;
    }
    
    return false;
}

void MainWindow::onPlayerProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    
    
    if (m_playerProcess) {
        QString output = m_playerProcess->readAllStandardOutput();
        QString error = m_playerProcess->readAllStandardError();
        
    }
    
    if (m_isStreaming) {
        m_statusLabel->setText("播放进程已退出");
        m_statusLabel->setStyleSheet(
            "QLabel {"
            "    color: #f44336;"
            "    font-weight: bold;"
            "    padding: 5px;"
            "}"
        );
    }
}

// -------------------------------------------------------------------------
// 看门狗实现 (Watchdog Implementation)
// -------------------------------------------------------------------------

void MainWindow::onWatchdogNewConnection()
{
    if (!m_watchdogServer) return;
    
    QLocalSocket *clientConnection = m_watchdogServer->nextPendingConnection();
    if (!clientConnection) return;
    
    // 保存当前连接
    m_currentWatchdogSocket = clientConnection;
    
    // 如果有待发送的审批指令，立即发送
        if (m_pendingApproval) {
        if (m_currentWatchdogSocket->state() == QLocalSocket::ConnectedState) {
             m_currentWatchdogSocket->write("CMD_APPROVE");
             m_currentWatchdogSocket->flush();
             m_pendingApproval = false;
        }
    }

    // 连接数据读取信号
    connect(clientConnection, &QLocalSocket::readyRead, this, &MainWindow::onWatchdogDataReady);
    connect(clientConnection, &QLocalSocket::disconnected, this, [this, clientConnection]() {
        if (m_currentWatchdogSocket == clientConnection) {
            m_currentWatchdogSocket = nullptr;
        }
        clientConnection->deleteLater();
    });
    
    // qDebug() << "[Watchdog] Client connected";
}

void MainWindow::onWatchdogDataReady()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket*>(sender());
    if (!socket) return;
    
    m_watchdogRxBuffer.append(socket->readAll());
    
    // 更新最后心跳时间
    m_lastHeartbeatTime = QDateTime::currentMSecsSinceEpoch();

    static const QByteArray kViewerExitPrefix("EVT_VIEWER_EXIT:");
    static const QByteArray kViewerMicPrefix("EVT_VIEWER_MIC:");
    int idx = -1;
    while ((idx = m_watchdogRxBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_watchdogRxBuffer.left(idx).trimmed();
        m_watchdogRxBuffer.remove(0, idx + 1);

        if (line.isEmpty()) {
            continue;
        }
        bool allOnes = true;
        for (char c : line) {
            if (c != '1') {
                allOnes = false;
                break;
            }
        }
        if (allOnes) {
            continue;
        }

        if (line.startsWith(kViewerExitPrefix)) {
            const QByteArray viewerBytes = line.mid(kViewerExitPrefix.size()).trimmed();
            const QString viewerId = QString::fromUtf8(viewerBytes);
            if (!viewerId.isEmpty() && m_transparentImageList) {
                m_transparentImageList->removeViewer(viewerId);
                if (m_isStreaming && m_transparentImageList->getViewerCount() <= 0) {
                    if (m_currentWatchdogSocket && m_currentWatchdogSocket->state() == QLocalSocket::ConnectedState) {
                        m_currentWatchdogSocket->write("CMD_SOFT_STOP");
                        m_currentWatchdogSocket->flush();
                    } else {
                        stopStreaming();
                    }
                }
            }
        } else if (line.startsWith(kViewerMicPrefix)) {
            const QByteArray payload = line.mid(kViewerMicPrefix.size()).trimmed();
            const int sep = payload.lastIndexOf(':');
            if (sep > 0) {
                const QByteArray viewerBytes = payload.left(sep).trimmed();
                const QByteArray stateBytes = payload.mid(sep + 1).trimmed();
                const QString viewerId = QString::fromUtf8(viewerBytes);
                const bool enabled = (stateBytes == "1");
                if (!viewerId.isEmpty() && m_transparentImageList) {
                    m_transparentImageList->setViewerMicState(viewerId, enabled);
                }
            }
        }
    }

    if (m_watchdogRxBuffer.size() > 4096) {
        m_watchdogRxBuffer.clear();
    }
}

void MainWindow::onWatchdogTimeout()
{
    // 如果没有在推流，不需要检测
    if (!m_isStreaming || !m_captureProcess || m_captureProcess->state() == QProcess::NotRunning) {
        return;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 diff = now - m_lastHeartbeatTime;
    
    // 宽容度设置：
    // 1. 启动初期允许较长延迟 (前10秒允许10秒超时)
    // 2. 正常运行允许 5秒 超时 (防止偶发卡顿误杀)
    
    // 如果距离启动不足10秒，给予更多宽容
    qint64 startupGracePeriod = 10000; 
    bool inGracePeriod = m_startupTimer.isValid() && m_startupTimer.elapsed() < startupGracePeriod;
    
    qint64 timeoutThreshold = inGracePeriod ? 10000 : 5000; // 启动期10秒，运行时5秒

    if (diff > timeoutThreshold) {
        // qDebug() << "[Watchdog] ALERT! CaptureProcess freeze detected! No heartbeat for" << diff << "ms. Killing...";
        
        // 强制杀死子进程
        if (m_captureProcess) {
            m_captureProcess->kill(); // 直接Kill，不废话
            // 状态会在 onCaptureProcessFinished 中更新
            
            // 更新UI提示用户
            if (m_statusLabel) {
                m_statusLabel->setText(QString("错误: 捕获进程无响应 (%1ms)").arg(diff));
            }
        }
        
        // 重置计时器防止重复触发（直到下一次启动）
        m_lastHeartbeatTime = now; 
        
        // 既然已经kill了，stopProcesses会被自动调用吗？
        // 不会，kill只会触发 finished 信号。
        // onCaptureProcessFinished 会被调用，然后更新UI。
        // 这里不需要额外操作，只要确保 kill 成功即可。
    }
}

// -------------------------------------------------------------------------

QString MainWindow::getConfigFilePath() const
{
    // 使用静态变量缓存配置文件路径，避免重复计算和日志输出
    static QString cachedConfigFilePath;
    static bool initialized = false;
    
    if (!initialized) {
        // 获取应用程序所在目录
        QString appDir = QApplication::applicationDirPath();
        
        // 创建config子目录
        QString configDir = appDir + "/config";
        QDir dir;
        if (!dir.exists(configDir)) {
            dir.mkpath(configDir);
            
        }
        
        // 缓存配置文件完整路径
        cachedConfigFilePath = configDir + "/app_config.txt";
        initialized = true;
    }
    
    return cachedConfigFilePath;
}

int MainWindow::loadOrGenerateRandomId()
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    
    // 尝试读取现有配置文件
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        QString line;
        while (!in.atEnd()) {
            line = in.readLine();
            if (line.startsWith("random_id=")) {
                QString idStr = line.mid(10); // 去掉"random_id="前缀
                bool ok;
                int existingId = idStr.toInt(&ok);
                if (ok && existingId >= 10000 && existingId <= 99999) {
                    configFile.close();
                    return existingId;
                }
            }
        }
        configFile.close();
    }
    
    // 如果文件不存在或读取失败，生成新的随机ID
    // 使用MAC地址生成稳定的ID，确保同一台设备ID固定
    QString macs;
    auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto &netInterface : interfaces) {
        if (netInterface.flags().testFlag(QNetworkInterface::IsUp) && 
            !netInterface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            macs += netInterface.hardwareAddress();
        }
    }

    int newRandomId;
    if (!macs.isEmpty()) {
        QByteArray hash = QCryptographicHash::hash(macs.toUtf8(), QCryptographicHash::Md5);
        quint32 num = 0;
        if (hash.size() >= 4) {
            num = (static_cast<quint8>(hash[0]) << 24) |
                  (static_cast<quint8>(hash[1]) << 16) |
                  (static_cast<quint8>(hash[2]) << 8)  |
                  static_cast<quint8>(hash[3]);
        } else {
            num = static_cast<quint32>(rand());
        }
        newRandomId = 10000 + (num % 90000);
    } else {
        newRandomId = 10000 + (rand() % 90000); // 回退到随机
    }
    
    saveRandomIdToConfig(newRandomId);
    return newRandomId;
}

// 新增：读取或生成icon ID
int MainWindow::loadOrGenerateIconId()
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    
    // 尝试读取现有配置文件
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        QString line;
        while (!in.atEnd()) {
            line = in.readLine();
            if (line.startsWith("icon_id=")) {
                QString idStr = line.mid(8); // 去掉"icon_id="前缀
                bool ok;
                int existingIconId = idStr.toInt(&ok);
                if (ok && existingIconId >= 3 && existingIconId <= 21) {
                    configFile.close();
                    return existingIconId;
                }
            }
        }
        configFile.close();
    }
    
    // 如果文件不存在或读取失败，生成新的icon ID
    // 使用MAC地址生成稳定的头像ID
    QString macs;
    auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto &netInterface : interfaces) {
        if (netInterface.flags().testFlag(QNetworkInterface::IsUp) && 
            !netInterface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            macs += netInterface.hardwareAddress();
        }
    }

    int newIconId;
    if (!macs.isEmpty()) {
        QByteArray hash = QCryptographicHash::hash(macs.toUtf8(), QCryptographicHash::Md5);
        quint32 num = 0;
        if (hash.size() >= 4) {
            // 使用完全不同的哈希计算方式来确保与RandomID差异较大
            // 将哈希值反向并取中间部分
            quint32 h1 = static_cast<quint8>(hash[hash.size()-1]);
            quint32 h2 = static_cast<quint8>(hash[hash.size()-2]);
            quint32 h3 = static_cast<quint8>(hash[hash.size()-3]);
            quint32 h4 = static_cast<quint8>(hash[hash.size()-4]);
            // 混合计算
            num = (h1 * 16777619) ^ (h2 * 65599) ^ (h3 * 257) ^ h4;
        } else {
             num = static_cast<quint32>(rand());
        }
        newIconId = 3 + (num % 19); 
    } else {
        newIconId = 3 + (rand() % 19); // 生成3-21之间的随机数
    }
    
    saveIconIdToConfig(newIconId);
    return newIconId;
}

void MainWindow::saveRandomIdToConfig(int randomId)
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    
    // 首先读取现有配置
    QStringList configLines;
    bool randomIdExists = false;
    
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("random_id=")) {
                configLines << QString("random_id=%1").arg(randomId);
                randomIdExists = true;
            } else if (!line.startsWith("#")) {
                configLines << line;
            }
        }
        configFile.close();
    }
    
    // 如果随机ID不存在，添加它
    if (!randomIdExists) {
        configLines << QString("random_id=%1").arg(randomId);
    }
    
    // 添加注释
    configLines << "# This file stores the application configuration";
    configLines << "# Delete this file to regenerate a new random ID";
    configLines << "# Set server_address to your cloud server IP:port (e.g., 1.2.3.4:8765)";
    
    // 写回配置文件
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString& line : configLines) {
            out << line << "\n";
        }
        configFile.close();
    } else {
    }
}

// 新增：保存icon ID到配置文件
void MainWindow::saveIconIdToConfig(int iconId)
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    
    // 首先读取现有配置
    QStringList configLines;
    bool iconIdExists = false;
    
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("icon_id=")) {
                configLines << QString("icon_id=%1").arg(iconId);
                iconIdExists = true;
            } else if (!line.startsWith("#")) {
                configLines << line;
            }
        }
        configFile.close();
    }
    
    // 如果icon ID不存在，添加它
    if (!iconIdExists) {
        configLines << QString("icon_id=%1").arg(iconId);
    }
    
    // 添加注释
    configLines << "# This file stores the application configuration";
    configLines << "# Delete this file to regenerate a new random ID and icon ID";
    configLines << "# Set server_address to your cloud server IP:port (e.g., 1.2.3.4:8765)";
    
    // 写回配置文件
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString& line : configLines) {
            out << line << "\n";
        }
        configFile.close();
    } else {
    }
}

// 读取或生成批注颜色ID（0-3）。如果不存在则默认0并写入配置
int MainWindow::loadOrGenerateColorId()
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);

    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("color_id=")) {
                bool ok = false;
                int val = line.mid(9).toInt(&ok);
                if (ok && val >= 0 && val <= 3) {
                    configFile.close();
                    return val;
                }
            }
        }
        configFile.close();
    }

    int defaultColorId = 2;
    saveColorIdToConfig(defaultColorId);
    return defaultColorId;
}

// 保存批注颜色ID到配置文件（覆盖或追加），范围约束为0-3
void MainWindow::saveColorIdToConfig(int colorId)
{
    if (colorId < 0) colorId = 0;
    if (colorId > 3) colorId = 3;

    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);

    QStringList configLines;
    bool colorIdExists = false;

    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("color_id=")) {
                configLines << QString("color_id=%1").arg(colorId);
                colorIdExists = true;
            } else if (!line.startsWith("#")) {
                configLines << line;
            }
        }
        configFile.close();
    }

    if (!colorIdExists) {
        configLines << QString("color_id=%1").arg(colorId);
    }

    configLines << "# This file stores the application configuration";
    configLines << "# Delete this file to regenerate IDs and settings";
    configLines << "# Set server_address to your cloud server IP:port (e.g., 1.2.3.4:8765)";

    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString& line : configLines) {
            out << line << "\n";
        }
        configFile.close();
    } else {
    }
}

// 槽：批注颜色变化后持久化到配置
void MainWindow::onAnnotationColorChanged(int colorId)
{
    saveColorIdToConfig(colorId);
}

QString MainWindow::getDeviceId() const
{
    // 使用静态变量缓存设备ID，避免重复读取配置文件
    static QString cachedDeviceId;
    static bool initialized = false;
    
    if (!initialized) {
        // 尝试从配置文件读取random_id作为设备ID
        QString configFilePath = getConfigFilePath();
        QFile configFile(configFilePath);
        
        if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&configFile);
            QString line;
            while (!in.atEnd()) {
                line = in.readLine();
                if (line.startsWith("random_id=")) {
                    QString deviceId = line.mid(10); // 去掉"random_id="前缀
                    if (!deviceId.isEmpty() && deviceId.length() >= 4) {
                        configFile.close();
                        cachedDeviceId = deviceId;
                        initialized = true;
                        return cachedDeviceId;
                    }
                }
            }
            configFile.close();
        }
        
        // 如果配置文件中没有random_id，使用loadOrGenerateRandomId生成
        int randomId = const_cast<MainWindow*>(this)->loadOrGenerateRandomId();
        cachedDeviceId = QString::number(randomId);
        initialized = true;
    }
    
    return cachedDeviceId;
}

QString MainWindow::generateUniqueDeviceId() const
{
    // 生成5位数字ID，与random_id保持一致的格式
    srand(static_cast<unsigned int>(QDateTime::currentMSecsSinceEpoch()));
    int deviceId = 10000 + (rand() % 90000); // 生成10000-99999之间的随机数
    return QString::number(deviceId);
}

void MainWindow::saveDeviceIdToConfig(const QString& deviceId)
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    
    // 首先读取现有配置
    QStringList configLines;
    bool deviceIdExists = false;
    
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("device_id=")) {
                configLines << QString("device_id=%1").arg(deviceId);
                deviceIdExists = true;
            } else if (!line.startsWith("#")) {
                configLines << line;
            }
        }
        configFile.close();
    }
    
    // 如果设备ID不存在，添加它
    if (!deviceIdExists) {
        configLines << QString("device_id=%1").arg(deviceId);
    }
    
    // 写入配置文件
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString &line : configLines) {
            out << line << "\n";
        }
        
        // 添加注释
        if (!deviceIdExists) {
            out << "# This file stores the application configuration\n";
            out << "# device_id is automatically generated and should be unique per device\n";
            out << "# Delete device_id line to regenerate a new unique ID\n";
            out << "# Set server_address to your cloud server IP:port (e.g., 1.2.3.4:8765)\n";
        }
        
        configFile.close();
    } else {
    }
}

QString MainWindow::getServerAddress() const
{
    // 使用静态变量缓存服务器地址，避免重复读取配置文件和日志输出
    static QString cachedServerAddress;
    static bool initialized = false;
    
    if (!initialized) {
        // 从配置文件读取服务器地址
        QString configFilePath = getConfigFilePath();
        QFile configFile(configFilePath);
        
        if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&configFile);
            QString line;
            while (!in.atEnd()) {
                line = in.readLine();
                if (line.startsWith("server_address=")) {
                    QString serverAddress = line.mid(15); // 去掉"server_address="前缀
                    configFile.close();
                    cachedServerAddress = serverAddress;
                    initialized = true;
                    return cachedServerAddress;
                }
            }
            configFile.close();
        }
        
        // 如果读取失败，返回默认的腾讯云地址
        cachedServerAddress = "123.207.222.92:8765";
        initialized = true;
    }
    
    return cachedServerAddress;
}

void MainWindow::saveServerAddressToConfig(const QString& serverAddress)
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    
    // 首先读取现有配置
    QStringList configLines;
    bool serverAddressExists = false;
    
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("server_address=")) {
                configLines << QString("server_address=%1").arg(serverAddress);
                serverAddressExists = true;
            } else {
                configLines << line;
            }
        }
        configFile.close();
    }
    
    // 如果服务器地址不存在，添加它
    if (!serverAddressExists) {
        configLines << QString("server_address=%1").arg(serverAddress);
    }
    
    // 写回配置文件
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString& line : configLines) {
            out << line << "\n";
        }
        configFile.close();
    } else {
    }
}

// 登录系统相关方法实现
void MainWindow::initializeLoginSystem()
{
    QString cfg = getConfigFilePath();
    QFile f(cfg);
    if (!f.exists()) {
        FirstLaunchWizard w(this);
        int res = w.exec();
        if (w.exitRequested()) {
            QTimer::singleShot(0, qApp, &QCoreApplication::quit);
            return;
        }
        if (res == QDialog::Accepted) {
            QString n = w.userName().trimmed();
            if (!n.isEmpty()) {
                saveUserNameToConfig(n);
                m_userName = n;
            }
            int si = w.screenIndex();
            if (si >= 0) saveScreenIndexToConfig(si);
        }
    }
    m_userId = getDeviceId();
    if (m_userName.isEmpty()) {
        QString name = loadUserNameFromConfig();
        if (name.isEmpty()) {
            // 如果没有配置用户名（跳过了向导），优先使用计算机名
            name = QHostInfo::localHostName();
            if (name.isEmpty()) {
                name = QString("用户%1").arg(m_userId);
            }
            saveUserNameToConfig(name);
        }
        m_userName = name;
    }
    
    // [Fix] Update NewUiWindow with the loaded user info immediately
    if (m_transparentImageList) {
        m_transparentImageList->setMyStreamId(m_userId, m_userName);
    }
    m_loginWebSocket = new QWebSocket();
    connect(m_loginWebSocket, &QWebSocket::connected, this, &MainWindow::onLoginWebSocketConnected);
    connect(m_loginWebSocket, &QWebSocket::disconnected, this, &MainWindow::onLoginWebSocketDisconnected);
    connect(m_loginWebSocket, &QWebSocket::textMessageReceived, this, &MainWindow::onLoginWebSocketTextMessageReceived);
    connect(m_loginWebSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, &MainWindow::onLoginWebSocketError);
    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(10000); // 改为10秒
    m_heartbeatTimer->setTimerType(Qt::PreciseTimer);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &MainWindow::sendHeartbeat);

    // 初始化用户列表清理定时器（蓄水池机制）
    m_userCleanupTimer = new QTimer(this);
    m_userCleanupTimer->setInterval(30000); // 30秒清理一次
    connect(m_userCleanupTimer, &QTimer::timeout, this, &MainWindow::onUserCleanupTimerTimeout);
    m_userCleanupTimer->start();

    // 初始化重连定时器
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &MainWindow::connectToLoginServer);

    QTimer::singleShot(3000, this, &MainWindow::connectToLoginServer);
}

void MainWindow::connectToLoginServer()
{
    if (m_loginWebSocket->state() == QAbstractSocket::ConnectedState ||
        m_loginWebSocket->state() == QAbstractSocket::ConnectingState) {
        return;
    }
    QString serverAddress = getServerAddress();
    QString serverUrl = QString("ws://%1/login").arg(serverAddress);  // 使用专门的登录路径
    m_loginWebSocket->open(QUrl(serverUrl));
}

void MainWindow::sendLoginRequest()
{
    if (!m_loginWebSocket || m_loginWebSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    
    QJsonObject loginRequest;
    loginRequest["type"] = "login";
    
    QJsonObject userData;
    userData["id"] = m_userId;
    userData["name"] = m_userName;
    // 同步发送两种字段，兼容服务器不同实现
    userData["icon_id"] = loadOrGenerateIconId();
    userData["viewer_icon_id"] = loadOrGenerateIconId();
    loginRequest["data"] = userData;
    
    QJsonDocument doc(loginRequest);
    QString message = doc.toJson(QJsonDocument::Compact);
    m_loginWebSocket->sendTextMessage(message);
}

void MainWindow::sendHeartbeat()
{
    if (!m_loginWebSocket || m_loginWebSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    if (!m_isLoggedIn) {
        return;
    }
    // 使用轻量级ping
    QJsonObject heartbeat;
    heartbeat["type"] = "ping";
    QJsonDocument doc(heartbeat);
    QString message = doc.toJson(QJsonDocument::Compact);
    m_loginWebSocket->sendTextMessage(message);
}

void MainWindow::updateUserList(const QJsonArray& users)
{
    // 1. 更新服务器在线用户蓄水池，并构建新用户ID集合
    m_serverOnlineUsers.clear();
    QSet<QString> newUserIds;
    for (int i = 0; i < users.size(); ++i) {
        QJsonObject userObj = users[i].toObject();
        if (!userObj.isEmpty()) {
            QString uid = userObj["id"].toString();
            m_serverOnlineUsers.insert(uid);
            newUserIds.insert(uid);
        }
    }

    // 2. 立即清理已离线的用户（不在新列表中的用户）
    // 这样可以消除下线通知的延迟，同时保留蓄水池作为清理僵尸用户的兜底机制
    for (int j = m_listWidget->count() - 1; j >= 0; --j) {
        QListWidgetItem* item = m_listWidget->item(j);
        QString userId = item->data(Qt::UserRole).toString();
        
        // 如果是提示信息项（无UserRole）
        if (userId.isEmpty()) {
            // 如果现在有真实用户了，移除提示信息
            if (!newUserIds.isEmpty()) {
                delete m_listWidget->takeItem(j);
            }
            continue;
        }
        
        // 如果该用户不在新列表中，说明已下线
        if (!newUserIds.contains(userId)) {
            // 从透明头像列表中移除
            if (m_transparentImageList) {
                m_transparentImageList->removeUser(userId);
            }
            // 从主列表中移除
            delete m_listWidget->takeItem(j);
        }
    }
    
    // 如果清理后列表为空且确实没人，显示提示
    if (m_listWidget->count() == 0 && newUserIds.isEmpty()) {
         m_listWidget->addItem("暂无在线用户");
    }

    // 3. 增量添加/更新新用户到UI
    for (int i = 0; i < users.size(); ++i) {
        const QJsonValue& userValue = users[i];
        if (!userValue.isObject()) continue;
        
        QJsonObject userObj = userValue.toObject();
        QString userId = userObj["id"].toString();
        QString userName = userObj["name"].toString();
        
        // 解析icon_id
        int iconId = -1;
        if (userObj.contains("icon_id")) {
             QJsonValue v = userObj["icon_id"];
             iconId = v.isString() ? v.toString().toInt() : v.toInt(-1);
        } else if (userObj.contains("viewer_icon_id")) {
             QJsonValue v = userObj["viewer_icon_id"];
             iconId = v.isString() ? v.toString().toInt() : v.toInt(-1);
        }

        // 检查列表中是否已存在
        bool existsInList = false;
        for(int j=0; j<m_listWidget->count(); ++j) {
            QListWidgetItem* item = m_listWidget->item(j);
            if (item->data(Qt::UserRole).toString() == userId) {
                existsInList = true;
                // 更新名字（如果变了）
                QString newText = QString("%1 (%2)").arg(userName).arg(userId);
                if (item->text() != newText) {
                    item->setText(newText);
                }
                break;
            }
        }
        
        if (!existsInList) {
            // 新增到列表
            QString displayText = QString("%1 (%2)").arg(userName).arg(userId);
            QListWidgetItem* item = new QListWidgetItem(displayText);
            item->setData(Qt::UserRole, userId);
            m_listWidget->addItem(item);
        }
        
        // 新增到透明图片列表 (addUser会自动处理去重和更新)
        // [New UI Integration] Use NewUiWindow integration
        // m_transparentImageList is now NewUiWindow*
        if (m_transparentImageList) {
            m_transparentImageList->addUser(userId, userName, iconId);
        }
    }
    
    // 检查目标用户在线状态
    bool targetOnline = false;
    if (!m_currentTargetId.isEmpty()) {
        for (int i = 0; i < users.size(); ++i) {
            const QJsonValue &uv = users[i];
            if (!uv.isObject()) continue;
            QJsonObject uo = uv.toObject();
            if (uo.value("id").toString() == m_currentTargetId) { targetOnline = true; break; }
        }
        if (m_videoWindow) {
            auto *videoWidget = m_videoWindow->getVideoDisplayWidget();
            if (videoWidget) {
                if (!targetOnline) {
                    videoWidget->notifyTargetOffline(QStringLiteral("对方已离线或退出"));
                } else {
                    videoWidget->clearOfflineReminder();
                }
            }
        }
    }
}

void MainWindow::onUserCleanupTimerTimeout()
{
    // 遍历列表，移除不在蓄水池中的用户
    for (int i = m_listWidget->count() - 1; i >= 0; --i) {
        QListWidgetItem* item = m_listWidget->item(i);
        QString userId = item->data(Qt::UserRole).toString();
        
        // 如果UserRole为空（提示信息）
        if (userId.isEmpty()) {
             if (m_serverOnlineUsers.size() > 0) {
                 delete m_listWidget->takeItem(i);
             }
             continue;
        }
        
        if (!m_serverOnlineUsers.contains(userId)) {
            // 蓄水池里没有这个人，移除
            m_transparentImageList->removeUser(userId);
            delete m_listWidget->takeItem(i);
        }
    }
    
    // 如果移除后列表为空，显示暂无在线用户
    if (m_listWidget->count() == 0) {
        m_listWidget->addItem("暂无在线用户");
    }
}

// 登录系统槽函数实现
void MainWindow::onLoginWebSocketConnected()
{
    // 连接成功，停止重连定时器
    m_reconnectTimer->stop();
    
    m_listWidget->clear();
    m_listWidget->addItem("已连接服务器，正在登录...");
    
    // 连接成功后立即发送登录请求
    sendLoginRequest();
}

void MainWindow::onLoginWebSocketDisconnected()
{
    m_isLoggedIn = false;
    if (m_heartbeatTimer) { m_heartbeatTimer->stop(); }
    
    m_listWidget->clear();
    m_listWidget->addItem("与服务器断开连接");
    
    // 断开连接时，清空在线用户蓄水池和桌面头像
    // 因为相对我而言，所有人都“掉线”了
    m_serverOnlineUsers.clear();
    m_transparentImageList->clearUserList();

    // 5秒后尝试重新连接 (如果定时器已在运行，start会重置它，避免重复)
    m_reconnectTimer->start(5000);
}

void MainWindow::onLoginWebSocketTextMessageReceived(const QString &message)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    
    if (error.error != QJsonParseError::NoError) {
        if (message.contains("kick_viewer")) {
            qInfo().noquote() << "[KickDiag] login ws message parse failed"
                              << " error=" << error.errorString()
                              << " raw=" << message;
        }
        return;
    }
    
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();
    
    if (type == "login_response") {
        bool success = obj["success"].toBool();
        QString responseMessage = obj["message"].toString();
        
        if (success) {
            m_isLoggedIn = true;
            m_listWidget->clear();
            m_listWidget->addItem("登录成功，等待用户列表...");
            if (!m_appReadyEmitted) { emit appReady(); m_appReadyEmitted = true; }
            if (m_heartbeatTimer) { m_heartbeatTimer->start(); }
            sendHeartbeat();
        } else {
            m_listWidget->clear();
            m_listWidget->addItem("登录失败: " + responseMessage);
        }
    } else if (type == "online_users_update" || type == "online_users") {
        
        if (!obj.contains("data")) {
            return;
        }
        
        QJsonValue dataValue = obj["data"];
        
        if (!dataValue.isArray()) {
            return;
        }
        
        QJsonArray users = dataValue.toArray();
        
        // 详细记录每个用户信息
        for (int i = 0; i < users.size(); ++i) {
            const QJsonValue& userValue = users[i];
            
            if (userValue.isObject()) {
                QJsonObject userObj = userValue.toObject();
                QString userId = userObj["id"].toString();
                QString userName = userObj["name"].toString();
            } else {
                // 数据格式错误，不是对象
            }
        }
        updateUserList(users);
        if (!m_appReadyEmitted) { emit appReady(); m_appReadyEmitted = true; }
    } else if (type == "start_streaming_request") {
        QString viewerId = obj["viewer_id"].toString();
        QString targetId = obj["target_id"].toString();

        // [Fix] 检查是否是取消请求
        if (obj.contains("action") && obj["action"].toString() == "cancel") {
            qInfo() << "Received start_streaming_request (cancel) from" << viewerId;

            // [Local Control] 本地通知捕获进程清理状态
            if (m_currentWatchdogSocket && m_currentWatchdogSocket->state() == QLocalSocket::ConnectedState) {
                m_currentWatchdogSocket->write("CMD_REJECT");
                m_currentWatchdogSocket->flush();
            }

            if (m_approvalDialog) {
                m_approvalDialog->close();
                m_approvalDialog->deleteLater();
                m_approvalDialog = nullptr;
                QMessageBox msgBox(this);
                msgBox.setIcon(QMessageBox::Information);
                msgBox.setWindowTitle(QStringLiteral("未接提醒"));
                msgBox.setText(QStringLiteral("用户 %1 已取消观看请求").arg(viewerId));
                msgBox.setWindowFlags(msgBox.windowFlags() | Qt::WindowStaysOnTopHint);
                msgBox.exec();
            } else {
                qInfo() << "No approval dialog to close for canceled request (via action)";
            }
            return;
        }

        // 播放来电提醒音
        if (!m_alertSound) {
            m_alertSound = new QSoundEffect(this);
            m_alertSound->setSource(QUrl::fromLocalFile(QCoreApplication::applicationDirPath() + "/audio/ling2.wav"));
            m_alertSound->setVolume(1.0f);
        }
        m_alertSound->play();

        QString viewerName = obj.contains("viewer_name") ? obj["viewer_name"].toString() : viewerId;
        
        // [Fix] 优先使用本地用户列表中的名字（如果存在），确保显示最新名字
        if (m_listWidget) {
            for (int i = 0; i < m_listWidget->count(); ++i) {
                QListWidgetItem* item = m_listWidget->item(i);
                if (item && item->data(Qt::UserRole).toString() == viewerId) {
                    QString text = item->text();
                    int idx = text.lastIndexOf(" (");
                    if (idx != -1) {
                        viewerName = text.left(idx);
                    }
                    break;
                }
            }
        }
        
        const QString action = obj.value("action").toString();
        const bool audioOnly = obj.value("audio_only").toBool(false) || action == "audio_only";
        bool manualApproval = loadManualApprovalEnabledFromConfig();
        bool isConnected = m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState;

        if (audioOnly && isConnected) {
            QJsonObject accepted;
            accepted["type"] = "watch_request_accepted";
            accepted["viewer_id"] = viewerId;
            accepted["target_id"] = targetId;
            QJsonDocument accDoc(accepted);
            m_loginWebSocket->sendTextMessage(accDoc.toJson(QJsonDocument::Compact));

            if (m_currentWatchdogSocket && m_currentWatchdogSocket->state() == QLocalSocket::ConnectedState) {
                m_currentWatchdogSocket->write("CMD_APPROVE");
                m_currentWatchdogSocket->flush();
            } else {
                m_pendingApproval = true;
            }

            if (!m_isStreaming) {
                startStreaming();
            }

            QJsonObject streamOkResponse;
            streamOkResponse["type"] = "streaming_ok";
            streamOkResponse["viewer_id"] = viewerId;
            streamOkResponse["target_id"] = targetId;
            streamOkResponse["stream_url"] = QString("ws://%1/subscribe/%2").arg(getServerAddress(), targetId);
            QJsonDocument responseDoc(streamOkResponse);
            m_loginWebSocket->sendTextMessage(responseDoc.toJson(QJsonDocument::Compact));
            return;
        }

        if (manualApproval && isConnected) {
            // 1. 发送需要审批的消息给观看端
            QJsonObject approval;
            approval["type"] = "approval_required";
            approval["viewer_id"] = viewerId;
            approval["target_id"] = targetId;
            QJsonDocument doc(approval);
            m_loginWebSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));

            // 2. 弹出非模态确认对话框
            if (m_approvalDialog) {
                m_approvalDialog->close();
                delete m_approvalDialog;
                m_approvalDialog = nullptr;
            }
            
            m_approvalDialog = new QMessageBox(this);
            m_approvalDialog->setWindowTitle(QStringLiteral("观看请求"));
            m_approvalDialog->setText(QStringLiteral("用户 %1 请求观看您的屏幕，是否允许？").arg(viewerName));
            QPushButton *acceptBtn = m_approvalDialog->addButton(QMessageBox::Yes);
            QPushButton *rejectBtn = m_approvalDialog->addButton(QMessageBox::No);
            m_approvalDialog->setDefaultButton(acceptBtn);
            m_approvalDialog->setModal(false); 
            m_approvalDialog->setWindowFlags(m_approvalDialog->windowFlags() | Qt::WindowStaysOnTopHint);

            // 连接同意按钮
            // [Fix] Capture viewerName for use in lambda
            connect(acceptBtn, &QPushButton::clicked, this, [this, viewerId, targetId, viewerName]() {
                if (!m_approvalDialog) return;

                // 同意
                QJsonObject accepted;
                accepted["type"] = "watch_request_accepted";
                accepted["viewer_id"] = viewerId;
                accepted["target_id"] = targetId;
                QJsonDocument accDoc(accepted);
                if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                    m_loginWebSocket->sendTextMessage(accDoc.toJson(QJsonDocument::Compact));
                }

                // [Fix] Add to "My Room" list
                if (m_transparentImageList) {
                    m_transparentImageList->addViewer(viewerId, viewerName);
                }

                // [Local Control] 本地直接通知捕获进程开始推流
                if (m_currentWatchdogSocket && m_currentWatchdogSocket->state() == QLocalSocket::ConnectedState) {
                    m_currentWatchdogSocket->write("CMD_APPROVE");
                    m_currentWatchdogSocket->flush();
                } else {
                    m_pendingApproval = true;
                }

                // 开始推流
                if (!m_isStreaming) {
                    startStreaming();
                }

                // 发送 streaming_ok
                QJsonObject streamOkResponse;
                streamOkResponse["type"] = "streaming_ok";
                streamOkResponse["viewer_id"] = viewerId;
                streamOkResponse["target_id"] = targetId;
                streamOkResponse["stream_url"] = QString("ws://%1/subscribe/%2").arg(getServerAddress(), targetId);
                QJsonDocument responseDoc(streamOkResponse);
                if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                    m_loginWebSocket->sendTextMessage(responseDoc.toJson(QJsonDocument::Compact));
                }

                if (m_approvalDialog) {
                    m_approvalDialog->close();
                    m_approvalDialog->deleteLater();
                    m_approvalDialog = nullptr;
                }
            });

            // 连接拒绝按钮
            connect(rejectBtn, &QPushButton::clicked, this, [this, viewerId, targetId]() {
                // 拒绝
                QJsonObject rejected;
                rejected["type"] = "watch_request_rejected";
                rejected["viewer_id"] = viewerId;
                rejected["target_id"] = targetId;
                QJsonDocument rejDoc(rejected);
                if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                    m_loginWebSocket->sendTextMessage(rejDoc.toJson(QJsonDocument::Compact));
                }

                // [Local Control] 本地通知捕获进程拒绝
                if (m_currentWatchdogSocket && m_currentWatchdogSocket->state() == QLocalSocket::ConnectedState) {
                    m_currentWatchdogSocket->write("CMD_REJECT");
                    m_currentWatchdogSocket->flush();
                    qDebug() << "Sent local rejection command to CaptureProcess";
                }

                if (m_approvalDialog) {
                    m_approvalDialog->close();
                    m_approvalDialog->deleteLater();
                    m_approvalDialog = nullptr;
                }
            });

            m_approvalDialog->show();
            m_approvalDialog->raise();
            m_approvalDialog->activateWindow();
        } else {
            if (!m_isStreaming) {
                startStreaming();
            }
            QJsonObject streamOkResponse;
            streamOkResponse["type"] = "streaming_ok";
            streamOkResponse["viewer_id"] = viewerId;
            streamOkResponse["target_id"] = targetId;
            streamOkResponse["stream_url"] = QString("ws://%1/subscribe/%2").arg(getServerAddress(), targetId);
            if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                QJsonDocument responseDoc(streamOkResponse);
                m_loginWebSocket->sendTextMessage(responseDoc.toJson(QJsonDocument::Compact));
            }
            
            // [Fix] Add to "My Room" list for auto-approve case
            if (m_transparentImageList) {
                m_transparentImageList->addViewer(viewerId, viewerName);
            }
        }
    } else if (type == "watch_request_canceled") {
        QString viewerId = obj["viewer_id"].toString();
        QString targetId = obj["target_id"].toString();
        
        qInfo() << "Received watch_request_canceled from" << viewerId;

        // [Local Control] 本地通知捕获进程清理状态
        if (m_currentWatchdogSocket && m_currentWatchdogSocket->state() == QLocalSocket::ConnectedState) {
            m_currentWatchdogSocket->write("CMD_REJECT");
            m_currentWatchdogSocket->flush();
        }
        
        // 如果有待处理的审批弹窗，关闭它
        if (m_approvalDialog) {
            m_approvalDialog->close();
            m_approvalDialog->deleteLater();
            m_approvalDialog = nullptr;
            
            // 显示未接提醒
            QMessageBox msgBox(this);
            msgBox.setIcon(QMessageBox::Information);
            msgBox.setWindowTitle(QStringLiteral("未接提醒"));
            msgBox.setText(QStringLiteral("用户 %1 已取消观看请求").arg(viewerId));
            msgBox.setWindowFlags(msgBox.windowFlags() | Qt::WindowStaysOnTopHint);
            msgBox.exec();
        } else {
            qInfo() << "No approval dialog to close for canceled request";
        }
    } else if (type == "watch_request_error") {
        QString message = obj["message"].toString();
        QString targetId = obj["target_id"].toString();
        
        // Close the waiting dialog if any
        if (m_waitingDialog) {
            m_waitingDialog->close();
            m_waitingDialog->deleteLater();
            m_waitingDialog = nullptr;
        }

        if (m_transparentImageList && (m_pendingTalkTargetId == targetId || m_audioOnlyTargetId == targetId)) {
            m_transparentImageList->setTalkConnected(targetId, false);
        }
        if (m_pendingTalkTargetId == targetId) {
            m_pendingTalkTargetId.clear();
            m_pendingTalkEnabled = false;
        }
        if (m_audioOnlyTargetId == targetId) {
            m_audioOnlyTargetId.clear();
        }

        if (!targetId.isEmpty() && m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject msg;
            msg["type"] = "viewer_mic_state";
            msg["viewer_id"] = getDeviceId();
            msg["target_id"] = targetId;
            msg["enabled"] = false;
            msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
            m_loginWebSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
        }
        
        QMessageBox::warning(this, QStringLiteral("请求失败"), 
            QStringLiteral("无法连接到目标用户 %1: %2").arg(targetId, message));
            
    } else if (type == "approval_required") {
        // [Fix] 移除重复的等待弹窗
    } else if (type == "watch_request_accepted") {
        QString viewerId = obj["viewer_id"].toString();
        // QString targetId = obj["target_id"].toString();
        if (viewerId == getDeviceId()) {
            // Close the waiting dialog if any
            if (m_waitingDialog) {
                m_waitingDialog->close();
                m_waitingDialog->deleteLater();
                m_waitingDialog = nullptr;
            }
            
            // startVideoReceiving(targetId); // 移除此处调用，等待 streaming_ok 信号再开始接收，避免重复初始化
        }
    } else if (type == "watch_request_rejected") {
        QString viewerId = obj["viewer_id"].toString();
        QString targetId = obj["target_id"].toString();
        if (viewerId == getDeviceId()) {
            // Close the waiting dialog if any
            if (m_waitingDialog) {
                m_waitingDialog->close();
                m_waitingDialog->deleteLater();
                m_waitingDialog = nullptr;
            }
            
            // 如果是自己主动取消的，不显示拒绝弹窗
            if (m_selfCancelled) {
                m_selfCancelled = false;
                return;
            }

            if (m_transparentImageList && !targetId.isEmpty() && (m_pendingTalkTargetId == targetId || m_audioOnlyTargetId == targetId)) {
                m_transparentImageList->setTalkConnected(targetId, false);
            }
            if (m_pendingTalkTargetId == targetId) {
                m_pendingTalkTargetId.clear();
                m_pendingTalkEnabled = false;
            }
            if (m_audioOnlyTargetId == targetId) {
                m_audioOnlyTargetId.clear();
            }

            if (!targetId.isEmpty() && m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                QJsonObject msg;
                msg["type"] = "viewer_mic_state";
                msg["viewer_id"] = getDeviceId();
                msg["target_id"] = targetId;
                msg["enabled"] = false;
                msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
                m_loginWebSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
            }

            QMessageBox msgBox(this);
            msgBox.setIcon(QMessageBox::Information);
            msgBox.setWindowTitle(QStringLiteral("观看被拒绝"));
            msgBox.setText(QStringLiteral("对方拒绝了观看请求"));
            msgBox.setWindowFlags(msgBox.windowFlags() | Qt::WindowStaysOnTopHint);
            msgBox.exec();
        }
    } else if (type == "viewer_mic_state") {
        QString viewerId = obj.value("viewer_id").toString();
        QString targetId = obj.value("target_id").toString();
        const bool enabled = obj.value("enabled").toBool(false);
        if (!targetId.isEmpty() && targetId != getDeviceId()) {
            return;
        }
        if (viewerId.isEmpty()) {
            return;
        }
        if (m_transparentImageList) {
            m_transparentImageList->setViewerMicState(viewerId, enabled);
        }
    } else if (type == "viewer_exit" || type == "viewer_exited" || type == "viewer_left" || type == "stop_streaming") {
        // 处理观众退出或停止观看的通知
        QString viewerId = obj.value("viewer_id").toString();
        if (viewerId.isEmpty() && obj.contains("device_id")) viewerId = obj.value("device_id").toString();
        if (viewerId.isEmpty() && obj.contains("id")) viewerId = obj.value("id").toString();
        if (viewerId.isEmpty() && obj.contains("user_id")) viewerId = obj.value("user_id").toString();
        if (viewerId.isEmpty() && obj.contains("viewer")) viewerId = obj.value("viewer").toString();

        QString targetId = obj.value("target_id").toString();
        if (targetId.isEmpty() && obj.contains("producer_id")) targetId = obj.value("producer_id").toString();
        if (targetId.isEmpty() && obj.contains("host_id")) targetId = obj.value("host_id").toString();

        if (!targetId.isEmpty() && targetId != getDeviceId()) {
            return;
        }
        if (viewerId.isEmpty()) {
            return;
        }
        if (m_transparentImageList) {
            m_transparentImageList->removeViewer(viewerId);
            if (m_isStreaming && m_transparentImageList->getViewerCount() <= 0) {
                if (m_currentWatchdogSocket && m_currentWatchdogSocket->state() == QLocalSocket::ConnectedState) {
                    m_currentWatchdogSocket->write("CMD_SOFT_STOP");
                    m_currentWatchdogSocket->flush();
                } else {
                    stopStreaming();
                }
            }
        }
    } else if (type == "kick_viewer") {
        QString viewerId = obj["viewer_id"].toString();
        QString targetId = obj["target_id"].toString();
        qInfo().noquote() << "[KickDiag] kick_viewer received on login ws"
                          << " viewer_id=" << viewerId
                          << " target_id=" << targetId
                          << " my_id=" << getDeviceId();

        if (viewerId == getDeviceId()) {
            qInfo().noquote() << "[KickDiag] kick_viewer applied on viewer side"
                              << " target_id=" << targetId;
            if (m_waitingDialog) {
                m_waitingDialog->close();
                m_waitingDialog->deleteLater();
                m_waitingDialog = nullptr;
            }

            m_currentTargetId.clear();

            if (m_videoWindow) {
                if (auto *vd = m_videoWindow->getVideoDisplayWidget()) {
                    vd->notifyTargetOffline(QStringLiteral("你已被房主移除"));
                    vd->stopReceiving(false);
                }
                m_videoWindow->hide();
            }
        } else {
            qInfo().noquote() << "[KickDiag] kick_viewer ignored on this client";
        }
    } else if (type == "streaming_ok") {
        // 处理推流OK响应，开始拉流播放
        QString viewerId = obj["viewer_id"].toString();
        QString targetId = obj["target_id"].toString();
        QString streamUrl = obj["stream_url"].toString();
        
        // 检查是否是当前用户的观看请求
        if (viewerId == getDeviceId()) {
            // Close the waiting dialog if any (just in case)
            if (m_waitingDialog) {
                m_waitingDialog->close();
                m_waitingDialog->deleteLater();
                m_waitingDialog = nullptr;
            }
            
            const bool showVideoWindow = m_pendingShowVideoWindow;
            const bool talkWasPending = (m_pendingTalkEnabled && m_pendingTalkTargetId == targetId);
            m_pendingShowVideoWindow = true;
            if (showVideoWindow) {
                if (m_videoWindow) {
                    m_videoWindow->show();
                    m_videoWindow->raise();
                    m_videoWindow->activateWindow();
                }
                m_audioOnlyTargetId.clear();
            }
            startVideoReceiving(targetId);
            if (talkWasPending && m_transparentImageList) {
                m_transparentImageList->setTalkConnected(targetId, true);
            }
            if (talkWasPending) {
                m_pendingTalkTargetId.clear();
                m_pendingTalkEnabled = false;
            }
        } else {
            // 非当前用户的观看请求，忽略
        }
    } else if (type == "avatar_update" || type == "avatar_updated" || type == "user_icon_update") {
        QString userId;
        if (obj.contains("device_id")) userId = obj.value("device_id").toString();
        else if (obj.contains("id")) userId = obj.value("id").toString();
        else if (obj.contains("user_id")) userId = obj.value("user_id").toString();
        
        // [Fix] Handle name updates for "My Room" viewer list
        QString name;
        if (obj.contains("name")) name = obj["name"].toString();
        else if (obj.contains("user_name")) name = obj["user_name"].toString();
        else if (obj.contains("viewer_name")) name = obj["viewer_name"].toString();

        if (!userId.isEmpty() && !name.isEmpty() && m_transparentImageList) {
             m_transparentImageList->updateViewerNameIfExists(userId, name);
        }

        int iconId = -1;
        if (obj.contains("icon_id")) {
            QJsonValue v = obj.value("icon_id");
            iconId = v.isString() ? v.toString().toInt() : v.toInt(-1);
        } else if (obj.contains("viewer_icon_id")) {
            QJsonValue v = obj.value("viewer_icon_id");
            iconId = v.isString() ? v.toString().toInt() : v.toInt(-1);
        } else if (obj.contains("icon")) {
            QJsonValue v = obj.value("icon");
            iconId = v.isString() ? v.toString().toInt() : v.toInt(-1);
        }
        if (!userId.isEmpty() && iconId >= 0 && m_transparentImageList) {
            m_transparentImageList->updateUserAvatar(userId, iconId);
        }
    } else {
        // 未知消息类型，忽略
    }
}
void MainWindow::onLoginWebSocketError(QAbstractSocket::SocketError error)
{
    
    QString errorMessage;
    switch (error) {
    case QAbstractSocket::ConnectionRefusedError:
        errorMessage = "连接被拒绝 - 服务器可能未启动";
        break;
    case QAbstractSocket::RemoteHostClosedError:
        errorMessage = "远程主机关闭连接";
        break;
    case QAbstractSocket::HostNotFoundError:
        errorMessage = "主机未找到";
        break;
    case QAbstractSocket::SocketTimeoutError:
        errorMessage = "连接超时";
        break;
    case QAbstractSocket::NetworkError:
        errorMessage = "网络错误";
        break;
    case QAbstractSocket::SslHandshakeFailedError:
        errorMessage = "SSL握手失败";
        break;
    default:
        errorMessage = QString("未知错误 (代码: %1)").arg(static_cast<int>(error));
        break;
    }
    
    m_listWidget->clear();
    m_listWidget->addItem("连接服务器失败: " + errorMessage);
    
    // 10秒后尝试重新连接 (使用统一的定时器，避免冲突)
    // 如果已经有更短的重连计划，这里会覆盖为10秒，这通常是合理的（出错了多等会儿）
    m_reconnectTimer->start(10000);
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    // 获取点击的项目
    QListWidgetItem *item = m_listWidget->itemAt(pos);
    if (!item) {
        return; // 如果没有点击到项目，不显示菜单
    }
    
    // 创建右键菜单
    QMenu contextMenu(this);
    
    // 添加菜单项
    QAction *option1 = contextMenu.addAction("观看");
    QAction *option2 = contextMenu.addAction("选项二");
    
    // 连接菜单项的信号
    connect(option1, &QAction::triggered, this, &MainWindow::onContextMenuOption1);
    connect(option2, &QAction::triggered, this, &MainWindow::onContextMenuOption2);
    
    // 在鼠标位置显示菜单
    contextMenu.exec(m_listWidget->mapToGlobal(pos));
}

void MainWindow::onContextMenuOption1()
{
    // 获取当前选中的项目
    QListWidgetItem *currentItem = m_listWidget->currentItem();
    if (!currentItem) {
        return;
    }
    
    QString itemText = currentItem->text();
    
    // 从项目文本中提取设备ID (格式: "用户名 (设备ID)")
    QRegularExpression regex("\\(([^)]+)\\)");
    QRegularExpressionMatch match = regex.match(itemText);
    if (match.hasMatch()) {
        QString targetDeviceId = match.captured(1);
        
        // 发送观看请求
        sendWatchRequest(targetDeviceId);
    } else {
    }
}

void MainWindow::onContextMenuOption2()
{
    // 获取当前选中的项目
    QListWidgetItem *currentItem = m_listWidget->currentItem();
    if (currentItem) {
    } else {
    }
}

// 透明图片列表点击事件处理
void MainWindow::onUserImageClicked(const QString &userId, const QString &userName)
{
    
    // 显示视频窗口
    // if (m_videoWindow) {
    //     m_videoWindow->show();
    //     m_videoWindow->raise();
    //     m_videoWindow->activateWindow();
    // }
    
    // 发送观看请求
    sendWatchRequest(userId);
    
    // 启动视频接收 - 移至收到 streaming_ok 后
    // startVideoReceiving(userId);
}

void MainWindow::showMainList()
{
    if (m_transparentImageList) {
        m_transparentImageList->show();
        m_transparentImageList->raise();
    }
}

void MainWindow::onSetAvatarRequested()
{
    
    // 如果头像设置窗口不存在，创建它
    if (!m_avatarSettingsWindow) {
        m_avatarSettingsWindow = new AvatarSettingsWindow(this);
        
        // 连接头像选择信号
        connect(m_avatarSettingsWindow, &AvatarSettingsWindow::avatarSelected,
                this, &MainWindow::onAvatarSelected);
    }
    // 发送观看请求
    // 显示头像设置窗口
    m_avatarSettingsWindow->show();
    // 启动视频接收（使用当前选中的用户ID）
    // QString currentUserId = m_transparentImageList ? m_transparentImageList->getCurrentUserId() : QString();
    // if (!currentUserId.isEmpty()) {
    //     startVideoReceiving(currentUserId);
    // }
    m_avatarSettingsWindow->activateWindow();
}

void MainWindow::onAvatarSelected(int iconId)
{
    
    // 1. 更新配置文件中的icon ID
    saveIconIdToConfig(iconId);
    
    // 2. 立即更新用户头像显示
    if (m_transparentImageList) {
        // 重新加载当前用户的头像
        QString currentUserId = m_transparentImageList->getCurrentUserId();
        if (!currentUserId.isEmpty()) {
            m_transparentImageList->updateUserAvatar(currentUserId, iconId);
        }
    }
    
    // 3. 向服务器发送头像更新消息
    if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject message;
        message["type"] = "avatar_update";
        message["device_id"] = getDeviceId();
        message["icon_id"] = iconId;
        
        QJsonDocument doc(message);
        QString jsonString = doc.toJson(QJsonDocument::Compact);
        
        m_loginWebSocket->sendTextMessage(jsonString);
    }

    {
        QString serverUrl = QString("ws://%1/subscribe/%2").arg(getServerAddress(), getDeviceId());
        QWebSocket *ws = new QWebSocket();
        connect(ws, &QWebSocket::connected, this, [this, ws, iconId]() {
            QJsonObject msg;
            msg["type"] = "avatar_update";
            msg["device_id"] = getDeviceId();
            msg["icon_id"] = iconId;
            ws->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
            QTimer::singleShot(200, ws, [ws]() { ws->close(); });
            QTimer::singleShot(400, ws, [ws]() { ws->deleteLater(); });
        });
        connect(ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, [ws](QAbstractSocket::SocketError) {
            ws->deleteLater();
        });
        ws->open(QUrl(serverUrl));
    }
    
    // 注释：保持头像设置窗口打开，方便用户多次选择
    // if (m_avatarSettingsWindow) {
    //     m_avatarSettingsWindow->hide();
    // }
}

void MainWindow::onSystemSettingsRequested()
{
    if (!m_systemSettingsWindow) {
        m_systemSettingsWindow = new SystemSettingsWindow(this);
        connect(m_systemSettingsWindow, &SystemSettingsWindow::screenSelected,
                this, &MainWindow::onScreenSelected);
        connect(m_systemSettingsWindow, &SystemSettingsWindow::localQualitySelected,
                this, &MainWindow::onLocalQualitySelected);
        connect(m_systemSettingsWindow, &SystemSettingsWindow::userNameChanged,
                this, &MainWindow::onUserNameChanged);
        connect(m_systemSettingsWindow, &SystemSettingsWindow::manualApprovalEnabledChanged,
                this, &MainWindow::onManualApprovalEnabledChanged);
    }
    const bool wasVisible = m_systemSettingsWindow->isVisible();
    const Qt::WindowFlags flags = m_systemSettingsWindow->windowFlags();
    if (!(flags & Qt::WindowStaysOnTopHint)) {
        if (wasVisible) {
            m_systemSettingsWindow->hide();
        }
        m_systemSettingsWindow->setWindowFlags(flags | Qt::WindowStaysOnTopHint);
    }
    m_systemSettingsWindow->show();
    m_systemSettingsWindow->raise();
    m_systemSettingsWindow->activateWindow();
}

void MainWindow::onClearMarksRequested()
{
    // 1. 清理本地所有屏幕上的 ScreenAnnotationWidget 绘制内容
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget *w : widgets) {
        ScreenAnnotationWidget *saw = qobject_cast<ScreenAnnotationWidget*>(w);
        if (saw) {
            saw->clear();
        }
    }

    // 2. 发送网络事件清理远端或消费者端的绘制
    QString serverUrl = QString("ws://%1/subscribe/%2").arg(getServerAddress(), getDeviceId());
    QWebSocket *ws = new QWebSocket();
    connect(ws, &QWebSocket::connected, this, [this, ws]() {
        QJsonObject watch;
        watch["type"] = "watch_request";
        watch["viewer_id"] = getDeviceId();
        watch["target_id"] = getDeviceId();
        watch["viewer_name"] = m_userName;
        ws->sendTextMessage(QJsonDocument(watch).toJson(QJsonDocument::Compact));
        QJsonObject start;
        start["type"] = "start_streaming";
        ws->sendTextMessage(QJsonDocument(start).toJson(QJsonDocument::Compact));
        QJsonObject msg;
        msg["type"] = "annotation_event";
        msg["phase"] = "clear";
        msg["x"] = 0;
        msg["y"] = 0;
        msg["viewer_id"] = getDeviceId();
        msg["target_id"] = getDeviceId();
        msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
        msg["color_id"] = 0;
        ws->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
        QTimer::singleShot(200, ws, [ws]() { ws->close(); });
        QTimer::singleShot(400, ws, [ws]() { ws->deleteLater(); });
    });
    connect(ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, [ws](QAbstractSocket::SocketError) {
        ws->deleteLater();
    });
    ws->open(QUrl(serverUrl));
}

void MainWindow::onExitRequested()
{
    // 尽量优雅地停止推流与相关进程
    stopStreaming();
    if (m_videoWindow) {
        m_videoWindow->hide();
    }
    QCoreApplication::quit();
}

void MainWindow::onHideRequested()
{
    if (m_videoWindow) m_videoWindow->hide();
    if (m_transparentImageList) m_transparentImageList->hide();
    if (m_avatarSettingsWindow) m_avatarSettingsWindow->hide();
    if (m_systemSettingsWindow) m_systemSettingsWindow->hide();
}

void MainWindow::onMicToggleRequested(bool enabled)
{
    saveMicEnabledToConfig(enabled);
    if (m_videoWindow) {
        m_videoWindow->setMicCheckedSilently(enabled);
        auto *vd = m_videoWindow->getVideoDisplayWidget();
        if (vd) {
            vd->setTalkEnabled(enabled);
            vd->setMicSendEnabled(enabled);
        }
    }
    if (m_transparentImageList) {
        m_transparentImageList->setGlobalMicCheckedSilently(enabled);
    }
    {
        QString targetId = m_currentTargetId;
        if (targetId.isEmpty()) targetId = m_pendingTalkTargetId;
        if (targetId.isEmpty()) targetId = m_audioOnlyTargetId;
        if (!targetId.isEmpty() && m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject msg;
            msg["type"] = "viewer_mic_state";
            msg["viewer_id"] = getDeviceId();
            msg["target_id"] = targetId;
            msg["enabled"] = enabled;
            msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
            m_loginWebSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
        }
    }
    if (m_isStreaming && m_currentWatchdogSocket && m_currentWatchdogSocket->state() == QLocalSocket::ConnectedState) {
        const QString cmd = QString("CMD_AUDIO_TOGGLE:%1").arg(enabled ? 1 : 0);
        m_currentWatchdogSocket->write(cmd.toUtf8());
        m_currentWatchdogSocket->flush();
    }
}

void MainWindow::onSpeakerToggleRequested(bool enabled)
{
    saveSpeakerEnabledToConfig(enabled);
    if (m_videoWindow) {
        m_videoWindow->setSpeakerChecked(enabled);
        auto *vd = m_videoWindow->getVideoDisplayWidget();
        if (vd) {
            vd->setSpeakerEnabled(enabled);
        }
    }
}

void MainWindow::onManualApprovalEnabledChanged(bool enabled)
{
    saveManualApprovalEnabledToConfig(enabled);
}

void MainWindow::onScreenSelected(int index)
{
    if (m_transparentImageList) {
        m_transparentImageList->setCaptureScreenIndex(index);
    }

    // [New] Direct control of CaptureProcess when streaming (align with consumer right-click behavior)
    if (m_isStreaming && m_currentWatchdogSocket && m_currentWatchdogSocket->state() == QLocalSocket::ConnectedState) {
        QString cmd = QString("CMD_SWITCH_SCREEN:%1").arg(index);
        m_currentWatchdogSocket->write(cmd.toUtf8());
        m_currentWatchdogSocket->flush();
        qDebug() << "Sent direct screen switch command to CaptureProcess:" << cmd;
        
        // Notify Settings UI
        if (m_systemSettingsWindow) {
            m_systemSettingsWindow->notifySwitchSucceeded();
        }
        
        // Update Dynamic Island
        if (m_islandWidget) {
             const auto screens = QGuiApplication::screens();
             if (index >= 0 && index < screens.size()) {
                 m_islandWidget->setTargetScreen(screens[index]);
                 m_islandWidget->showOnScreen();
             }
        }
        
        saveScreenIndexToConfig(index);
        return;
    }

    bool active = false;
    if (m_videoWindow) {
        auto *videoWidget = m_videoWindow->getVideoDisplayWidget();
        if (videoWidget && videoWidget->isReceiving()) {
            active = true;
        }
    }
    if (m_isStreaming) {
        active = true;
    }
    if (active) {
        if (m_videoWindow) {
            auto *videoWidget = m_videoWindow->getVideoDisplayWidget();
            if (videoWidget) {
                videoWidget->sendSwitchScreenIndex(index);
                m_isScreenSwitching = true;
                if (m_switchFrameConn) {
                    QObject::disconnect(m_switchFrameConn);
                }
                m_switchFrameConn = connect(videoWidget, &VideoDisplayWidget::frameReceived, this, [this]() {
                    if (m_isScreenSwitching) {
                        m_isScreenSwitching = false;
                        if (m_systemSettingsWindow) {
                            m_systemSettingsWindow->notifySwitchSucceeded();
                        }
                        if (m_switchFrameConn) {
                            QObject::disconnect(m_switchFrameConn);
                        }
                    }
                });
            }
        }
        
        // 实时更新灵动岛位置
        if (m_isStreaming && m_islandWidget) {
             const auto screens = QGuiApplication::screens();
             if (index >= 0 && index < screens.size()) {
                 m_islandWidget->setTargetScreen(screens[index]);
                 m_islandWidget->showOnScreen(); // 重新显示以更新位置
             }
        }
    } else {
        saveScreenIndexToConfig(index);
        if (m_systemSettingsWindow) {
            m_systemSettingsWindow->notifySwitchSucceeded();
        }
    }
}

void MainWindow::saveScreenIndexToConfig(int screenIndex)
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);

    QStringList configLines;
    bool exists = false;

    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("screen_index=")) {
                configLines << QString("screen_index=%1").arg(screenIndex);
                exists = true;
            } else if (!line.startsWith("#")) {
                configLines << line;
            }
        }
        configFile.close();
    }

    if (!exists) {
        configLines << QString("screen_index=%1").arg(screenIndex);
    }

    configLines << "# Select which screen to capture: 0-based index";

    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString& line : configLines) {
            out << line << "\n";
        }
        configFile.close();
    } else {
    }
}

int MainWindow::loadScreenIndexFromConfig() const
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("screen_index=")) {
                bool ok;
                int idx = line.mid(13).toInt(&ok); // "screen_index=" is 13 chars
                if (ok && idx >= 0) {
                    configFile.close();
                    return idx;
                }
            }
        }
        configFile.close();
    }
    return 0; // Default to primary screen
}

void MainWindow::onLocalQualitySelected(const QString& quality)
{
    saveLocalQualityToConfig(quality);
    if (m_statusLabel) {
        m_statusLabel->setText(QString("本地质量设置为: %1").arg(quality));
    }
}

void MainWindow::onAudioOutputSelectionChanged(bool followSystem, const QString &deviceId)
{
    
    saveAudioOutputFollowSystemToConfig(followSystem);
    saveAudioOutputDeviceIdToConfig(deviceId);
    if (m_videoWindow) {
        auto *vd = m_videoWindow->getVideoDisplayWidget();
        if (vd) {
            vd->applyAudioOutputSelectionRuntime();
        }
    }
}

void MainWindow::onMicInputSelectionChanged(bool followSystem, const QString &deviceId)
{
    saveMicInputFollowSystemToConfig(followSystem);
    saveMicInputDeviceIdToConfig(deviceId);
}

void MainWindow::saveLocalQualityToConfig(const QString& quality)
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);

    QStringList configLines;
    bool exists = false;
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        exists = true;
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            configLines << in.readLine();
        }
        configFile.close();
    }

    bool replaced = false;
    for (int i = 0; i < configLines.size(); ++i) {
        if (configLines[i].startsWith("local_quality=")) {
            configLines[i] = QString("local_quality=%1").arg(quality);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        configLines << QString("local_quality=%1").arg(quality);
    }

    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString& line : configLines) {
            out << line << "\n";
        }
        configFile.close();
    } else {
    }
}

bool MainWindow::loadAudioOutputFollowSystemFromConfig() const
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("audio_output_follow_system=")) {
                QString v = line.mid(27).trimmed();
                configFile.close();
                return v.compare("true", Qt::CaseInsensitive) == 0;
            }
        }
        configFile.close();
    }
    return true;
}

bool MainWindow::loadSpeakerEnabledFromConfig() const
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("speaker_enabled=")) {
                QString v = line.mid(QString("speaker_enabled=").length()).trimmed();
                configFile.close();
                return v.compare("true", Qt::CaseInsensitive) == 0;
            }
        }
        configFile.close();
    }
    return true;
}

bool MainWindow::loadMicEnabledFromConfig() const
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("mic_enabled=")) {
                QString v = line.mid(QString("mic_enabled=").length()).trimmed();
                configFile.close();
                return v.compare("true", Qt::CaseInsensitive) == 0;
            }
        }
        configFile.close();
    }
    return true;
}

void MainWindow::saveSpeakerEnabledToConfig(bool enabled)
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    QStringList configLines;
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) configLines << in.readLine();
        configFile.close();
    }
    bool replaced = false;
    for (int i = 0; i < configLines.size(); ++i) {
        if (configLines[i].startsWith("speaker_enabled=")) {
            configLines[i] = QString("speaker_enabled=%1").arg(enabled ? "true" : "false");
            replaced = true; break;
        }
    }
    if (!replaced) configLines << QString("speaker_enabled=%1").arg(enabled ? "true" : "false");
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString &line : configLines) out << line << "\n";
        configFile.close();
    }
}

void MainWindow::onToggleStreamingIsland()
{
    if (m_islandWidget) {
        if (m_islandWidget->isVisible()) {
            m_islandWidget->hide();
        } else {
             int screenIndex = loadScreenIndexFromConfig();
             const auto screens = QGuiApplication::screens();
             if (screenIndex >= 0 && screenIndex < screens.size()) {
                 m_islandWidget->setTargetScreen(screens[screenIndex]);
             }
             m_islandWidget->showOnScreen();
        }
    }
}

void MainWindow::saveMicEnabledToConfig(bool enabled)
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    QStringList configLines;
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) configLines << in.readLine();
        configFile.close();
    }
    bool replaced = false;
    for (int i = 0; i < configLines.size(); ++i) {
        if (configLines[i].startsWith("mic_enabled=")) {
            configLines[i] = QString("mic_enabled=%1").arg(enabled ? "true" : "false");
            replaced = true; break;
        }
    }
    if (!replaced) configLines << QString("mic_enabled=%1").arg(enabled ? "true" : "false");
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString &line : configLines) out << line << "\n";
        configFile.close();
    }
}

bool MainWindow::loadManualApprovalEnabledFromConfig() const
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("manual_approval_enabled=")) {
                QString v = line.mid(QString("manual_approval_enabled=").length()).trimmed();
                configFile.close();
                return v.compare("true", Qt::CaseInsensitive) == 0 || v == "1";
            }
        }
        configFile.close();
    }
    // 默认返回 true，确保首次安装后手动同意是开启的
    return true;
}

void MainWindow::saveManualApprovalEnabledToConfig(bool enabled)
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    QStringList configLines;
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) configLines << in.readLine();
        configFile.close();
    }
    bool replaced = false;
    for (int i = 0; i < configLines.size(); ++i) {
        if (configLines[i].startsWith("manual_approval_enabled=")) {
            configLines[i] = QString("manual_approval_enabled=%1").arg(enabled ? "true" : "false");
            replaced = true; break;
        }
    }
    if (!replaced) configLines << QString("manual_approval_enabled=%1").arg(enabled ? "true" : "false");
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString &line : configLines) out << line << "\n";
        configFile.close();
    }
}

QString MainWindow::loadAudioOutputDeviceIdFromConfig() const
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("audio_output_device_id=")) {
                QString v = line.mid(23).trimmed();
                configFile.close();
                return v;
            }
        }
        configFile.close();
    }
    return QString();
}

void MainWindow::saveAudioOutputFollowSystemToConfig(bool followSystem)
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    QStringList configLines;
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) configLines << in.readLine();
        configFile.close();
    }
    bool replaced = false;
    for (int i = 0; i < configLines.size(); ++i) {
        if (configLines[i].startsWith("audio_output_follow_system=")) {
            configLines[i] = QString("audio_output_follow_system=%1").arg(followSystem ? "true" : "false");
            replaced = true; break;
        }
    }
    if (!replaced) configLines << QString("audio_output_follow_system=%1").arg(followSystem ? "true" : "false");
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString &line : configLines) out << line << "\n";
        configFile.close();
    }
}

void MainWindow::saveAudioOutputDeviceIdToConfig(const QString &deviceId)
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    QStringList configLines;
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) configLines << in.readLine();
        configFile.close();
    }
    bool replaced = false;
    for (int i = 0; i < configLines.size(); ++i) {
        if (configLines[i].startsWith("audio_output_device_id=")) {
            configLines[i] = QString("audio_output_device_id=%1").arg(deviceId);
            replaced = true; break;
        }
    }
    if (!replaced) configLines << QString("audio_output_device_id=%1").arg(deviceId);
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString &line : configLines) out << line << "\n";
        configFile.close();
    }
}

bool MainWindow::loadMicInputFollowSystemFromConfig() const
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("mic_input_follow_system=")) {
                QString v = line.mid(24).trimmed();
                configFile.close();
                return v.compare("true", Qt::CaseInsensitive) == 0;
            }
        }
        configFile.close();
    }
    return true;
}

QString MainWindow::loadMicInputDeviceIdFromConfig() const
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("mic_input_device_id=")) {
                QString v = line.mid(20).trimmed();
                configFile.close();
                return v;
            }
        }
        configFile.close();
    }
    return QString();
}

void MainWindow::saveMicInputFollowSystemToConfig(bool followSystem)
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    QStringList configLines;
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) configLines << in.readLine();
        configFile.close();
    }
    bool replaced = false;
    for (int i = 0; i < configLines.size(); ++i) {
        if (configLines[i].startsWith("mic_input_follow_system=")) {
            configLines[i] = QString("mic_input_follow_system=%1").arg(followSystem ? "true" : "false");
            replaced = true; break;
        }
    }
    if (!replaced) configLines << QString("mic_input_follow_system=%1").arg(followSystem ? "true" : "false");
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString &line : configLines) out << line << "\n";
        configFile.close();
    }
}

void MainWindow::saveMicInputDeviceIdToConfig(const QString &deviceId)
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    QStringList configLines;
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) configLines << in.readLine();
        configFile.close();
    }
    bool replaced = false;
    for (int i = 0; i < configLines.size(); ++i) {
        if (configLines[i].startsWith("mic_input_device_id=")) {
            configLines[i] = QString("mic_input_device_id=%1").arg(deviceId);
            replaced = true; break;
        }
    }
    if (!replaced) configLines << QString("mic_input_device_id=%1").arg(deviceId);
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString &line : configLines) out << line << "\n";
        configFile.close();
    }
}

void MainWindow::onUserNameChanged(const QString &name)
{
    QString n = name.trimmed();
    if (n.isEmpty()) return;
    if (n == m_userName) return;
    m_userName = n;
    saveUserNameToConfig(n);
    
    // [Fix] Update NewUiWindow when username changes
    if (m_transparentImageList) {
        m_transparentImageList->setMyStreamId(m_userId, m_userName);
    }
    
    if (m_isLoggedIn) {
        sendLoginRequest();
    }
    if (m_videoWindow) {
        VideoDisplayWidget* videoWidget = m_videoWindow->getVideoDisplayWidget();
        if (videoWidget) {
            videoWidget->setViewerName(m_userName);
        }
    }
}

QString MainWindow::loadUserNameFromConfig() const
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("user_name=")) {
                QString v = line.mid(QString("user_name=").length()).trimmed();
                configFile.close();
                return v;
            }
        }
        configFile.close();
    }
    return QString();
}

void MainWindow::saveUserNameToConfig(const QString &name)
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    QStringList configLines;
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) configLines << in.readLine();
        configFile.close();
    }
    bool replaced = false;
    for (int i = 0; i < configLines.size(); ++i) {
        if (configLines[i].startsWith("user_name=")) {
            configLines[i] = QString("user_name=%1").arg(name);
            replaced = true; break;
        }
    }
    if (!replaced) configLines << QString("user_name=%1").arg(name);
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString &line : configLines) out << line << "\n";
        configFile.close();
    }
}
