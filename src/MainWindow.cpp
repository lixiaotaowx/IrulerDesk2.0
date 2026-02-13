#include "MainWindow.h"
#include "VideoWindow.h"
#include <QSoundEffect>
#include <QUrl>
#include "NewUi/NewUiWindow.h"
#include "video_components/VideoDisplayWidget.h"
#include "ui/ScreenAnnotationWidget.h"
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
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QCloseEvent>
#include <QSet>
#include <QGraphicsDropShadowEffect>
#include <cstdlib>
#include <ctime>
#include "common/AppConfig.h"
#include "common/AutoUpdater.h"
#include "ui/DateTimePickerDialog.h"
#include "common/TaskManager.h"

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
                    // [Fix] Use nullptr parent to ensure it's a top-level window with taskbar icon
                    FirstLaunchWizard w(nullptr);
                    if (w.exec() == QDialog::Accepted) {
                        QString n = w.userName().trimmed();
                if (!n.isEmpty()) { saveUserNameToConfig(n); m_userName = n; }
                int si = w.screenIndex(); if (si >= 0) saveScreenIndexToConfig(si);
            }
        }
    }

    setupUI();
    
    setupStatusBar();
    startLanDiscoveryListener();

    if (!m_trayIcon) {
        QString appDir = QCoreApplication::applicationDirPath();
        m_trayIcon = new QSystemTrayIcon(QIcon(QString("%1/maps/logo/iruler.ico").arg(appDir)), this);
        m_trayMenu = new QMenu();
        QAction *showAction = m_trayMenu->addAction(QStringLiteral("显示主窗口"));
        QAction *exitAction = m_trayMenu->addAction(QStringLiteral("退出"));
        connect(showAction, &QAction::triggered, this, [this]() {
            showMainList();
        });
        connect(exitAction, &QAction::triggered, this, &MainWindow::onExitRequested);
        m_trayIcon->setContextMenu(m_trayMenu);
        connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
            if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) {
                showMainList();
            }
        });
        m_trayIcon->show();
    }

    QTimer::singleShot(0, this, [this]() {
        if (!m_appReadyEmitted) { emit appReady(); m_appReadyEmitted = true; }
    });

    // 初始化登录系统
    initializeLoginSystem();
    
    // 自动开始流媒体传输
    // QTimer::singleShot(1000, this, &MainWindow::startStreaming);
    
    // 检查并显示更新日志
    QTimer::singleShot(500, this, &MainWindow::checkAndShowUpdateLog);

    // Task Reminder connection
    connect(&TaskManager::instance(), &TaskManager::taskReminder, this, [this](const QString &content, const QString &taskId) {
        Q_UNUSED(taskId);
        // Show persistent toast
        showNoticeToast(content, "任务提醒", QDateTime::currentDateTime().toString("HH:mm"));
    });

    // 自动更新初始化
    m_autoUpdater = new AutoUpdater(this);
    connect(m_autoUpdater, &AutoUpdater::updateAvailable, this, &MainWindow::onUpdateAvailable);
    connect(m_autoUpdater, &AutoUpdater::downloadProgress, this, &MainWindow::onUpdateDownloadProgress);
    connect(m_autoUpdater, &AutoUpdater::errorOccurred, this, &MainWindow::onUpdateError);

    // 启动后延时检查更新
    QTimer::singleShot(3000, this, &MainWindow::checkForUpdates);
}

void MainWindow::checkForUpdates()
{
    // 这里使用 GitHub raw 地址获取 version.json
    // 如果有国内镜像源也可以替换为镜像源地址
    const QString updateUrl = "https://raw.githubusercontent.com/lixiaotaowx/IrulerDesk2.0/main/version.json";
    qInfo() << "[Update] Checking for updates from:" << updateUrl;
    if (m_autoUpdater) {
        m_autoUpdater->checkUpdate(updateUrl);
    }
}

void MainWindow::onUpdateAvailable(const QString &version, const QString &downloadUrl, const QString &description, bool force)
{
    QString msg = QStringLiteral("发现新版本: %1\n\n%2\n\n是否立即更新？").arg(version, description);
    
    if (force) {
        QMessageBox::warning(this, QStringLiteral("强制更新"), QStringLiteral("发现重要版本 %1，必须更新后才能继续使用。\n\n%2").arg(version, description));
        m_autoUpdater->downloadAndInstall();
        
        // 创建进度对话框
        m_updateProgressDialog = new QProgressDialog(QStringLiteral("正在下载更新..."), QStringLiteral("取消"), 0, 100, this);
        m_updateProgressDialog->setWindowModality(Qt::WindowModal);
        m_updateProgressDialog->setAutoClose(false); // 下载完不要自动关闭，等待安装
        m_updateProgressDialog->setAutoReset(false);
        m_updateProgressDialog->setMinimumDuration(0);
        // 强制更新不允许取消
        m_updateProgressDialog->setCancelButton(nullptr); 
        m_updateProgressDialog->show();
    } else {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, QStringLiteral("发现新版本"), msg, QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            m_autoUpdater->downloadAndInstall();

            // 创建进度对话框
            m_updateProgressDialog = new QProgressDialog(QStringLiteral("正在下载更新..."), QStringLiteral("取消"), 0, 100, this);
            m_updateProgressDialog->setWindowModality(Qt::WindowModal);
            m_updateProgressDialog->setAutoClose(false);
            m_updateProgressDialog->setAutoReset(false);
            m_updateProgressDialog->setMinimumDuration(0);
            
            // 支持取消下载
            connect(m_updateProgressDialog, &QProgressDialog::canceled, this, [this]() {
                if (m_autoUpdater) {
                    m_autoUpdater->cancel();
                }
                m_updateProgressDialog = nullptr;
            });
            m_updateProgressDialog->show();
        }
    }
}

void MainWindow::onUpdateDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (m_updateProgressDialog && bytesTotal > 0) {
        m_updateProgressDialog->setMaximum(100);
        m_updateProgressDialog->setValue(static_cast<int>(bytesReceived * 100 / bytesTotal));
        
        double receivedMB = bytesReceived / 1024.0 / 1024.0;
        double totalMB = bytesTotal / 1024.0 / 1024.0;
        m_updateProgressDialog->setLabelText(QStringLiteral("正在下载更新... %1 MB / %2 MB").arg(QString::number(receivedMB, 'f', 2), QString::number(totalMB, 'f', 2)));
    }
}

void MainWindow::onUpdateError(const QString &error)
{
    qWarning() << "[Update] Error:" << error;
    
    // 只有在显示了进度条（意味着用户同意更新或强制更新中）时才弹窗报错
    if (m_updateProgressDialog) {
        m_updateProgressDialog->close();
        m_updateProgressDialog->deleteLater();
        m_updateProgressDialog = nullptr;
        
        // 如果是取消操作导致的错误（虽然我们在 AutoUpdater 过滤了 OperationCanceledError，但双重保险），不弹窗
        // 但由于过滤了，这里收到的一定是真错误
        QMessageBox::warning(this, QStringLiteral("更新失败"), QStringLiteral("更新过程中发生错误：\n%1").arg(error));
    }
}

void MainWindow::startLanDiscoveryListener()
{
    if (!AppConfig::lanDiscoveryEnabled()) {
        return;
    }
    if (m_lanDiscoverySocket) {
        return;
    }

    m_lanDiscoverySocket = new QUdpSocket(this);
    const quint16 port = static_cast<quint16>(AppConfig::lanDiscoveryPort());
    const bool ok = m_lanDiscoverySocket->bind(QHostAddress::AnyIPv4, port,
                                               QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (!ok) {
        // qWarning().noquote() << "[KickDiag][LanDiscovery] bind_failed"
        //                      << " port=" << port
        //                      << " err=" << m_lanDiscoverySocket->errorString();
        m_lanDiscoverySocket->deleteLater();
        m_lanDiscoverySocket = nullptr;
        return;
    }

    // qInfo().noquote() << "[KickDiag][LanDiscovery] listening"
    //                   << " port=" << port;

    connect(m_lanDiscoverySocket, &QUdpSocket::readyRead, this, [this]() {
        if (!m_lanDiscoverySocket) return;

        while (m_lanDiscoverySocket->hasPendingDatagrams()) {
            QHostAddress sender;
            quint16 senderPort = 0;
            QByteArray datagram;
            datagram.resize(static_cast<int>(m_lanDiscoverySocket->pendingDatagramSize()));
            const qint64 n = m_lanDiscoverySocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
            if (n <= 0) continue;

            if (sender.protocol() != QAbstractSocket::IPv4Protocol) {
                continue;
            }

            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(datagram, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                continue;
            }

            const QJsonObject obj = doc.object();
            const QString type = obj.value(QStringLiteral("type")).toString();
            if (type != QStringLiteral("lan_announce")) {
                continue;
            }

            const QString targetId = obj.value(QStringLiteral("device_id")).toString().trimmed();
            if (targetId.isEmpty() || targetId == getDeviceId()) {
                continue;
            }

            const QString userName = obj.value(QStringLiteral("user_name")).toString().trimmed();
            if (!userName.isEmpty()) {
                AppConfig::setLanUserNameForTarget(targetId, userName);
                if (m_transparentImageList) {
                    m_transparentImageList->updateViewerNameIfExists(targetId, userName);
                    // [Fix] Ensure LAN users are added to the list immediately
                    m_transparentImageList->addUser(targetId, userName);
                }
            }

            int wsPort = obj.value(QStringLiteral("ws_port")).toInt(AppConfig::lanWsPort());
            if (wsPort <= 0 || wsPort > 65535) {
                wsPort = AppConfig::lanWsPort();
            }

            const QString base = QStringLiteral("ws://%1:%2").arg(sender.toString()).arg(wsPort);

            if (m_lanDiscoveredBaseByTarget.value(targetId) != base) {
                m_lanDiscoveredBaseByTarget.insert(targetId, base);
                // // qInfo().noquote() << "[KickDiag][LanDiscovery] discovered"
                //                   << " target_id=" << targetId
                //                   << " base=" << base
                //                   << " udp_port=" << senderPort;
            }

            QStringList existing = AppConfig::lanBaseUrlsForTarget(targetId);
            QStringList merged;
            merged.reserve(existing.size() + 1);
            merged.append(base);
            for (const QString &it : existing) {
                if (it.isEmpty()) continue;
                if (it == base) continue;
                merged.append(it);
                if (merged.size() >= 4) break;
            }
            merged.removeDuplicates();
            AppConfig::setLanBaseUrlsForTarget(targetId, merged);
        }
    });
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_exitRequested) {
        QMainWindow::closeEvent(event);
        return;
    }
    onHideRequested();
    hide();
    event->ignore();
}

NewUiWindow* MainWindow::transparentImageList() const
{
    return m_transparentImageList;
}

void MainWindow::sendWatchRequest(const QString& targetDeviceId, const QString& targetName)
{
    Q_UNUSED(targetName);
    sendWatchRequestWithVideo(targetDeviceId);
}

void MainWindow::sendWatchRequestWithVideo(const QString& targetDeviceId)
{
    m_pendingShowVideoWindow = true;
    m_audioOnlyTargetId.clear();
    if (m_pendingTalkTargetId == targetDeviceId) {
        m_pendingTalkTargetId.clear();
        m_pendingTalkEnabled = false;
    }
    if (m_transparentImageList && !targetDeviceId.isEmpty() && targetDeviceId != getDeviceId()) {
        m_transparentImageList->setWatchingTarget(targetDeviceId);
        m_transparentImageList->enterEmbeddedWatchingUi(targetDeviceId);
    }
    sendWatchRequestInternal(targetDeviceId, false);
}

void MainWindow::sendWatchRequestInternal(const QString& targetDeviceId, bool audioOnly)
{
    const QString myId = getDeviceId();
    if (!targetDeviceId.isEmpty() && targetDeviceId == myId) {
        if (m_waitingDialog) {
            m_waitingDialog->close();
            m_waitingDialog->deleteLater();
            m_waitingDialog = nullptr;
        }

        m_currentTargetId = targetDeviceId;
        if (audioOnly) {
            m_pendingShowVideoWindow = false;
            m_audioOnlyTargetId = targetDeviceId;
        } else {
            m_pendingShowVideoWindow = true;
            m_audioOnlyTargetId.clear();
        }

        if (!m_isStreaming) {
            startStreaming();
        }
        if (m_currentWatchdogSocket && m_currentWatchdogSocket->state() == QLocalSocket::ConnectedState) {
            m_currentWatchdogSocket->write("CMD_APPROVE");
            m_currentWatchdogSocket->flush();
        } else {
            m_pendingApproval = true;
        }

        if (!audioOnly && m_transparentImageList) {
            const QString serverUrl = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), targetDeviceId);
            const int initialColorId = loadOrGenerateColorId();
            m_transparentImageList->startEmbeddedReceiving(myId, targetDeviceId, m_userName, serverUrl, initialColorId);
        }
        return;
    }

    if (!m_loginWebSocket || m_loginWebSocket->state() != QAbstractSocket::ConnectedState) {
        m_currentTargetId = targetDeviceId;
        m_deferredWatchTargetId = targetDeviceId;
        m_deferredWatchAudioOnly = audioOnly;
        if (m_loginWebSocket) {
            if (m_deferredWatchConn) {
                QObject::disconnect(m_deferredWatchConn);
                m_deferredWatchConn = QMetaObject::Connection();
            }
            m_deferredWatchConn = connect(m_loginWebSocket, &QWebSocket::connected, this, [this]() {
                const QString targetId = m_deferredWatchTargetId;
                const bool audioOnly = m_deferredWatchAudioOnly;
                if (targetId.isEmpty()) {
                    return;
                }
                sendWatchRequestInternal(targetId, audioOnly);
            }, Qt::SingleShotConnection);
        }
        connectToLoginServer();
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
    watchRequest["viewer_id"] = myId;
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
        if (m_transparentImageList && m_transparentImageList->getCurrentUserId() != targetDeviceId) {
            m_transparentImageList->setWatchingTarget(QString());
            if (m_transparentImageList->isEmbeddedWatchingTarget(targetDeviceId)) {
                m_transparentImageList->stopEmbeddedWatching();
            }
        }
        if (m_pendingTalkTargetId == targetDeviceId) {
            m_pendingTalkTargetId.clear();
            m_pendingTalkEnabled = false;
        }
        if (!targetDeviceId.isEmpty() && m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject msg;
            msg["type"] = "viewer_mic_state";
            msg["viewer_id"] = getDeviceId();
            msg["target_id"] = targetDeviceId;
            msg["enabled"] = false;
            msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
            m_loginWebSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
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

void MainWindow::startVideoReceiving(const QString& targetDeviceId, const QString &serverUrlOverride)
{
    if (!m_transparentImageList) {
        return;
    }
    const QString serverUrl = serverUrlOverride.isEmpty()
        ? QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), targetDeviceId)
        : serverUrlOverride;
    const int initialColorId = loadOrGenerateColorId();
    const QString viewerId = getDeviceId();
    m_transparentImageList->startEmbeddedReceiving(viewerId, targetDeviceId, m_userName, serverUrl, initialColorId);
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

        sendWatchRequest(targetDeviceId);
        
    } else {
    }
}

void MainWindow::setupUI()
{
    setWindowTitle("屏幕流媒体系统 - 用户列表");
    setMinimumSize(400, 600);
    resize(450, 700);
    setObjectName("MainWindow");
    
    // 创建中央部件
    m_centralWidget = new QWidget(this);
    m_centralWidget->setObjectName("MainRoot");
    setCentralWidget(m_centralWidget);
    
    // 创建主垂直布局
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setSpacing(20);
    m_mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // 设置窗口样式
    setStyleSheet(
        "QMainWindow#MainWindow {"
        "    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "        stop:0 rgba(18,20,26,255),"
        "        stop:1 rgba(10,11,14,255)"
        "    );"
        "}"
        "QWidget#MainRoot { background: transparent; }"
        "QFrame[glassCard=\"true\"] {"
        "    background-color: rgba(35, 38, 45, 170);"
        "    border: 1px solid rgba(255, 255, 255, 18);"
        "    border-radius: 16px;"
        "}"
        "QLabel#TitleLabel {"
        "    color: rgba(255,255,255,235);"
        "    font-size: 18px;"
        "    font-weight: 700;"
        "    background: transparent;"
        "    padding: 14px 16px;"
        "}"
        "QListWidget {"
        "    background: transparent;"
        "    border: none;"
        "    color: rgba(255,255,255,220);"
        "    font-size: 14px;"
        "    padding: 6px;"
        "    outline: none;"
        "}"
        "QListWidget::item {"
        "    padding: 12px;"
        "    margin: 6px;"
        "    border-radius: 12px;"
        "    background-color: rgba(255,255,255,10);"
        "    border: 1px solid rgba(255,255,255,8);"
        "}"
        "QListWidget::item:hover {"
        "    background-color: rgba(255,255,255,16);"
        "    border-color: rgba(255,255,255,14);"
        "}"
        "QListWidget::item:selected {"
        "    background-color: rgba(0, 120, 212, 70);"
        "    border-color: rgba(0, 120, 212, 120);"
        "    color: rgba(255,255,255,245);"
        "}"
        "QPushButton {"
        "    background-color: rgba(255,255,255,12);"
        "    border: 1px solid rgba(255,255,255,20);"
        "    border-radius: 14px;"
        "    color: rgba(255,255,255,235);"
        "    font-size: 15px;"
        "    font-weight: 700;"
        "    padding: 14px 16px;"
        "}"
        "QPushButton:hover {"
        "    background-color: rgba(255,255,255,16);"
        "    border-color: rgba(255,255,255,28);"
        "}"
        "QPushButton:pressed {"
        "    background-color: rgba(255,255,255,10);"
        "}"
        "QPushButton:disabled {"
        "    background-color: rgba(255,255,255,6);"
        "    border-color: rgba(255,255,255,10);"
        "    color: rgba(255,255,255,90);"
        "}"
        "QLabel#IdLabel {"
        "    color: rgba(130, 220, 170, 240);"
        "    font-size: 15px;"
        "    font-weight: 700;"
        "    background: transparent;"
        "    padding: 16px;"
        "}"
    );
    
    auto applyGlassShadow = [](QWidget *w) {
        if (!w) return;
        auto *fx = new QGraphicsDropShadowEffect(w);
        fx->setBlurRadius(28);
        fx->setOffset(0, 10);
        fx->setColor(QColor(0, 0, 0, 120));
        w->setGraphicsEffect(fx);
    };

    QFrame *titleCard = new QFrame(m_centralWidget);
    titleCard->setProperty("glassCard", true);
    QVBoxLayout *titleCardLayout = new QVBoxLayout(titleCard);
    titleCardLayout->setContentsMargins(0, 0, 0, 0);
    titleCardLayout->setSpacing(0);
    applyGlassShadow(titleCard);

    // 创建标题标签
    QLabel *titleLabel = new QLabel("在线用户", titleCard);
    titleLabel->setObjectName("TitleLabel");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleCardLayout->addWidget(titleLabel);
    m_mainLayout->addWidget(titleCard);
    
    // 创建列表组件
    QFrame *listCard = new QFrame(m_centralWidget);
    listCard->setProperty("glassCard", true);
    QVBoxLayout *listCardLayout = new QVBoxLayout(listCard);
    listCardLayout->setContentsMargins(8, 8, 8, 8);
    listCardLayout->setSpacing(0);
    applyGlassShadow(listCard);

    m_listWidget = new QListWidget(listCard);
    m_listWidget->setMinimumHeight(300);
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    listCardLayout->addWidget(m_listWidget);
    
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
    
    m_mainLayout->addWidget(listCard, 1);
    
    // 创建观看按钮
    m_watchButton = new QPushButton("开始观看", this);
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
    
    m_idLabel->setObjectName("IdLabel");
    m_idLabel->setAlignment(Qt::AlignCenter);
    
    QFrame *idCard = new QFrame(m_centralWidget);
    idCard->setProperty("glassCard", true);
    QVBoxLayout *idCardLayout = new QVBoxLayout(idCard);
    idCardLayout->setContentsMargins(0, 0, 0, 0);
    idCardLayout->setSpacing(0);
    applyGlassShadow(idCard);
    idCardLayout->addWidget(m_idLabel);
    m_mainLayout->addWidget(idCard);
    
    
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
            vd->setTalkEnabled(false);
            vd->setMicSendEnabled(false);
        }
    }
    connect(m_videoWindow, &VideoWindow::micToggled, this, &MainWindow::onMicToggleRequested);
    
    // 创建新UI窗口
    m_transparentImageList = new NewUiWindow();
    
    // 设置当前用户信息
    m_transparentImageList->setMyStreamId(getDeviceId(), m_userName.isEmpty() ? "Me" : m_userName);
    m_transparentImageList->setCaptureScreenIndex(loadScreenIndexFromConfig());

    connect(m_videoWindow, &VideoWindow::audioCallRestoreClicked, this, [this]() {
        if (m_transparentImageList) {
            m_transparentImageList->restoreAudioCallUi();
        }
    });

    connect(m_transparentImageList, &NewUiWindow::audioCallRestoreAvailableChanged, this, [this](bool available) {
        if (m_videoWindow) {
            m_videoWindow->setAudioCallRestoreVisible(available);
        }
    });
    
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

    connect(m_transparentImageList, &NewUiWindow::broadcastRequested,
            this, &MainWindow::sendBroadcastNotice);
    connect(m_transparentImageList, &NewUiWindow::localActivityStateChanged,
            this, &MainWindow::sendActivityStateBroadcast);

    connect(m_transparentImageList, &NewUiWindow::stopWatchingRequested, this, [this](const QString &targetId) {
        if (targetId.isEmpty()) {
            return;
        }
        const QString activePeer = m_transparentImageList ? m_transparentImageList->activeAudioCallPeerId() : QString();
        const bool shouldHangupAudio = (m_pendingTalkTargetId == targetId) || (activePeer == targetId);
        if (m_pendingTalkTargetId == targetId) {
            m_pendingTalkTargetId.clear();
            m_pendingTalkEnabled = false;
        }
        if (!m_currentTargetId.isEmpty() && m_currentTargetId == targetId) {
            m_currentTargetId.clear();
        }
        if (m_audioOnlyTargetId == targetId) {
            m_audioOnlyTargetId.clear();
        }
        if (shouldHangupAudio && m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject msg;
            msg["type"] = "viewer_mic_state";
            msg["viewer_id"] = getDeviceId();
            msg["target_id"] = targetId;
            msg["enabled"] = false;
            msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
            m_loginWebSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
        }
    });

    connect(m_transparentImageList, &NewUiWindow::kickViewerRequested,
            this, [this](const QString &viewerId) {
        if (!m_loginWebSocket) {
            // qInfo().noquote() << "[KickDiag] kick_viewer not sent: login socket is null"
            //                   << " viewer_id=" << viewerId;
            return;
        }
        if (m_loginWebSocket->state() != QAbstractSocket::ConnectedState) {
            // // qInfo().noquote() << "[KickDiag] kick_viewer not sent: login socket not connected"
            //                   << " state=" << static_cast<int>(m_loginWebSocket->state())
            //                   << " viewer_id=" << viewerId;
            return;
        }
        QJsonObject msg;
        msg["type"] = "kick_viewer";
        msg["viewer_id"] = viewerId;
        msg["target_id"] = getDeviceId();
        msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
        QString payload = QJsonDocument(msg).toJson(QJsonDocument::Compact);
        qint64 bytes = m_loginWebSocket->sendTextMessage(payload);
        // qInfo().noquote() << "[KickDiag] kick_viewer sent"
        //                   << " bytes=" << bytes
        //                   << " payload=" << payload;
        if (m_transparentImageList) {
            m_transparentImageList->removeViewer(viewerId);
            m_transparentImageList->sendKickToSubscribers(viewerId);
        }
    });

    connect(m_transparentImageList, &NewUiWindow::closeRoomRequested,
            this, [this]() {
        // qInfo().noquote() << "[KickDiag] close_room requested"
        //                   << " my_id=" << getDeviceId()
        //                   << " is_streaming=" << m_isStreaming;
        stopStreaming();
    });

    connect(m_transparentImageList, &NewUiWindow::talkToggleRequested,
            this, [this](const QString &targetId, bool enabled) {
        auto sendViewerMicState = [this](const QString &tid, bool on) {
            if (tid.isEmpty()) {
                return;
            }
            if (!m_loginWebSocket || m_loginWebSocket->state() != QAbstractSocket::ConnectedState) {
                return;
            }
            QJsonObject msg;
            msg["type"] = "viewer_mic_state";
            msg["viewer_id"] = getDeviceId();
            msg["target_id"] = tid;
            msg["enabled"] = on;
            msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
            m_loginWebSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
        };

        // qInfo().noquote() << "[KickDiag] talk_toggle"
        //                   << " viewer_id=" << getDeviceId()
        //                   << " target_id=" << targetId
        //                   << " enabled=" << (enabled ? "true" : "false");
        if (enabled) {
            m_pendingTalkTargetId = targetId;
            m_pendingTalkEnabled = true;
            m_pendingShowVideoWindow = false;
            m_audioOnlyTargetId = targetId;
            if (m_transparentImageList) {
                m_transparentImageList->setTalkPending(targetId, true);
                // m_transparentImageList->janusSwitchToUserRoom(targetId); // Removed: Wait for acceptance
                // m_transparentImageList->setTalkPending(targetId, false); // Removed
                // m_transparentImageList->setTalkConnected(targetId, true); // Removed
            }
            // sendViewerMicState(targetId, true); // Removed: Wait for acceptance
            
            // Send Audio Call Request
            if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                QJsonObject req;
                req["type"] = "watch_request";
                req["viewer_id"] = getDeviceId();
                req["target_id"] = targetId;
                req["audio_only"] = true;
                req["action"] = "audio_only";
                req["viewer_name"] = m_userName.isEmpty() ? getDeviceId() : m_userName;
                
                m_loginWebSocket->sendTextMessage(QJsonDocument(req).toJson(QJsonDocument::Compact));
                
                // Optional: Show waiting dialog? 
                // The spinner on the button (setTalkPending) might be enough.
            }
        } else {
            const bool keepWatchingVideo = m_transparentImageList && m_transparentImageList->isEmbeddedWatchingTarget(targetId);
            auto sendKickViewer = [this](const QString &viewerId) {
                if (viewerId.isEmpty()) {
                    return;
                }
                if (!m_loginWebSocket || m_loginWebSocket->state() != QAbstractSocket::ConnectedState) {
                    return;
                }
                QJsonObject msg;
                msg["type"] = "kick_viewer";
                msg["viewer_id"] = viewerId;
                msg["target_id"] = getDeviceId();
                msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
                m_loginWebSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
            };

            if (m_transparentImageList) {
                m_transparentImageList->janusStop();
                m_transparentImageList->setTalkConnected(targetId, false);
                if (keepWatchingVideo) {
                    m_transparentImageList->stopEmbeddedWatching();
                }
            }
            if (m_pendingTalkTargetId == targetId) {
                m_pendingTalkTargetId.clear();
                m_pendingTalkEnabled = false;
            }
            if (m_audioOnlyTargetId == targetId) {
                m_audioOnlyTargetId.clear();
            }
            sendViewerMicState(targetId, false);

            if (m_isStreaming && m_transparentImageList) {
                const QStringList viewerIds = m_transparentImageList->getViewerIds();
                for (const QString &viewerId : viewerIds) {
                    sendKickViewer(viewerId);
                    m_transparentImageList->removeViewer(viewerId);
                    m_transparentImageList->sendKickToSubscribers(viewerId);
                }
            }

            if (!m_currentTargetId.isEmpty() && m_currentTargetId == targetId && m_videoWindow) {
                m_videoWindow->setMicCheckedSilently(false);
                if (auto *vd = m_videoWindow->getVideoDisplayWidget()) {
                    vd->setTalkEnabled(false);
                    vd->setMicSendEnabled(false);
                    if (vd->isReceiving()) {
                        vd->stopReceiving(false);
                    }
                }
                m_videoWindow->hide();
                m_audioOnlyTargetId.clear();
                m_currentTargetId.clear();
            }
        }
    });

    connect(m_videoWindow, &VideoWindow::closeClicked, this, [this]() {
        bool hangupSent = false;
        if (m_transparentImageList) {
            const QString peerId = m_transparentImageList->activeAudioCallPeerId();
            if (!peerId.isEmpty()) {
                m_transparentImageList->talkToggleRequested(peerId, false);
                hangupSent = true;
            } else if (!m_pendingTalkTargetId.isEmpty()) {
                m_transparentImageList->talkToggleRequested(m_pendingTalkTargetId, false);
                hangupSent = true;
            }
            if (!hangupSent) {
                m_transparentImageList->janusStop();
            }
        }

        if (m_videoWindow) {
            m_videoWindow->setMicCheckedSilently(false);
            if (auto *vd = m_videoWindow->getVideoDisplayWidget()) {
                vd->setTalkEnabled(false);
                vd->setMicSendEnabled(false);
                if (vd->isReceiving()) {
                    vd->stopReceiving(false);
                }
            }
        }

        m_pendingTalkTargetId.clear();
        m_pendingTalkEnabled = false;
        m_audioOnlyTargetId.clear();
        m_currentTargetId.clear();
    });

    connect(m_videoWindow, &VideoWindow::receivingStopped, this, [this](const QString &, const QString &targetId) {
        if (!m_transparentImageList) {
            return;
        }
        if (targetId.isEmpty()) {
            return;
        }
        m_transparentImageList->restartUserStreamSubscription(targetId);
        m_transparentImageList->onVideoReceivingStopped(targetId);
        if (!(m_pendingTalkEnabled && m_pendingTalkTargetId == targetId)) {
            const QString peerId = m_transparentImageList->activeAudioCallPeerId();
            if (!peerId.isEmpty() && peerId == targetId) {
                m_transparentImageList->talkToggleRequested(peerId, false);
            } else {
                m_transparentImageList->janusStop();
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


    if (m_islandWidget) {
        int screenIndex = loadScreenIndexFromConfig();
        const auto screens = QGuiApplication::screens();
        if (screenIndex >= 0 && screenIndex < screens.size()) {
            m_islandWidget->setTargetScreen(screens[screenIndex]);
        }
        m_islandWidget->showOnScreen();
    }
    // broadcastStatusIfChanged();
}

void MainWindow::stopStreaming()
{
    if (!m_isStreaming) {
        return;
    }

    m_isStreaming = false;
    if (m_noViewerSoftStopTimer) {
        m_noViewerSoftStopTimer->stop();
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


    if (m_islandWidget) {
        m_islandWidget->hide();
    }
    
    m_statusLabel->setText("已停止");
    m_statusLabel->setStyleSheet(
        "QLabel {"
        "    color: #f44336;"
        "    font-weight: bold;"
        "    padding: 5px;"
        "}"
    );
    // broadcastStatusIfChanged();
}

void MainWindow::scheduleNoViewerSoftStop()
{
    if (!m_isStreaming) {
        return;
    }
    if (!m_transparentImageList) {
        return;
    }

    if (m_transparentImageList->getViewerCount() > 0) {
        if (m_noViewerSoftStopTimer) {
            m_noViewerSoftStopTimer->stop();
        }
        return;
    }

    constexpr int kDelayMs = 20000;
    if (!m_noViewerSoftStopTimer) {
        m_noViewerSoftStopTimer = new QTimer(this);
        m_noViewerSoftStopTimer->setSingleShot(true);
        connect(m_noViewerSoftStopTimer, &QTimer::timeout, this, [this]() {
            if (!m_isStreaming) {
                return;
            }
            if (!m_transparentImageList) {
                return;
            }
            const int viewerCount = m_transparentImageList->getViewerCount();
            if (viewerCount > 0) {
                return;
            }
            qInfo().noquote() << "[NoViewerSoftStop] timeout fired, viewers=" << viewerCount;
            if (m_currentWatchdogSocket && m_currentWatchdogSocket->state() == QLocalSocket::ConnectedState) {
                m_currentWatchdogSocket->write("CMD_SOFT_STOP");
                m_currentWatchdogSocket->flush();
            } else {
                stopStreaming();
            }
        });
    }
    qInfo().noquote() << "[NoViewerSoftStop] scheduled in" << kDelayMs << "ms";
    m_noViewerSoftStopTimer->start(kDelayMs);
}

void MainWindow::updateStatus()
{
    QString status = QString("设备ID: %1 | 状态: %2")
                         .arg(getDeviceId())
                         .arg(m_isStreaming ? "推流中" : "空闲");
    
    m_statusLabel->setText(status);
    // broadcastStatusIfChanged();
}

QString MainWindow::buildStatusBroadcastContent() const
{
    const QString streamState = m_isStreaming ? QStringLiteral("推流中") : QStringLiteral("空闲");
    const bool connected = m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState && m_isLoggedIn;
    const QString connState = connected ? QStringLiteral("在线") : QStringLiteral("离线");
    return QStringLiteral("状态: %1 | 连接: %2").arg(streamState, connState);
}

void MainWindow::broadcastStatusIfChanged()
{
    if (!m_loginWebSocket || m_loginWebSocket->state() != QAbstractSocket::ConnectedState || !m_isLoggedIn) {
        return;
    }
    const QString content = buildStatusBroadcastContent();
    if (content == m_lastBroadcastStatus) {
        return;
    }
    m_lastBroadcastStatus = content;
    sendBroadcastNotice(content);
}

void MainWindow::sendActivityStateBroadcast(bool active)
{
    if (!m_loginWebSocket || m_loginWebSocket->state() != QAbstractSocket::ConnectedState || !m_isLoggedIn) {
        return;
    }
    QJsonObject payload;
    payload["kind"] = "activity_state";
    payload["user_id"] = getDeviceId();
    payload["active"] = active;
    payload["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    const QString content = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    sendBroadcastNotice(content);
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
    m_captureProcess->setProperty("stdout_buf", QString());
    m_captureProcess->setProperty("stderr_buf", QString());

    auto forwardProcessOutput = [this](const QByteArray &chunk, const char *propName, bool isErr) {
        if (!m_captureProcess) {
            return;
        }

        QString buffer = m_captureProcess->property(propName).toString();
        buffer += QString::fromUtf8(chunk);

        while (true) {
            const int newlinePos = buffer.indexOf('\n');
            if (newlinePos < 0) {
                break;
            }
            QString line = buffer.left(newlinePos);
            buffer.remove(0, newlinePos + 1);
            if (!line.isEmpty() && line.endsWith('\r')) {
                line.chop(1);
            }

            if (line.isEmpty()) {
                continue;
            }

            if (isErr) {
                qWarning().noquote() << "[CaptureProcess]" << line;
            } else {
                const bool important =
                    line.contains(QStringLiteral("[KickDiag]"), Qt::CaseInsensitive) ||
                    line.contains(QStringLiteral("lan_offer"), Qt::CaseInsensitive) ||
                    line.contains(QStringLiteral("lan "), Qt::CaseInsensitive) ||
                    line.contains(QStringLiteral(" lan_"), Qt::CaseInsensitive) ||
                    line.contains(QStringLiteral("[CaptureProcess]"), Qt::CaseInsensitive) ||
                    line.contains(QStringLiteral("VP9"), Qt::CaseInsensitive) ||
                    line.contains(QStringLiteral("subscribe"), Qt::CaseInsensitive) ||
                    line.contains(QStringLiteral("publish"), Qt::CaseInsensitive) ||
                    line.contains(QStringLiteral("ws://"), Qt::CaseInsensitive) ||
                    line.contains(QStringLiteral("wss://"), Qt::CaseInsensitive);

                if (important) {
                    qInfo().noquote() << "[CaptureProcess]" << line;
                }
            }
        }

        m_captureProcess->setProperty(propName, buffer);
    };

    connect(m_captureProcess, &QProcess::readyReadStandardOutput, this, [this, forwardProcessOutput]() {
        if (!m_captureProcess) {
            return;
        }
        const QByteArray chunk = m_captureProcess->readAllStandardOutput();
        if (!chunk.isEmpty()) {
            forwardProcessOutput(chunk, "stdout_buf", false);
        }
    });
    connect(m_captureProcess, &QProcess::readyReadStandardError, this, [this, forwardProcessOutput]() {
        if (!m_captureProcess) {
            return;
        }
        const QByteArray chunk = m_captureProcess->readAllStandardError();
        if (!chunk.isEmpty()) {
            forwardProcessOutput(chunk, "stderr_buf", true);
        }
    });
    
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
    if (m_isStreaming) {
        // 如果是正常退出（通常是用户点击停止），不做特殊处理，stopStreaming() 会处理状态
        // 但如果是 CrashExit（被看门狗杀死或崩溃），我们需要处理
        if (exitStatus == QProcess::CrashExit) {
            
            // 检查熔断机制
            if (checkCrashLoop()) {
                QString errorMsg = "捕获服务启动失败：1分钟内连续崩溃超过3次，已停止重试。";
                m_statusLabel->setText("严重错误：服务无法启动");
                m_statusLabel->setStyleSheet("QLabel { color: #f44336; font-weight: bold; padding: 5px; }");
                
                Q_UNUSED(m_trayIcon);
                
                QMessageBox::critical(this, "严重错误", errorMsg + "\n请检查设备驱动或重新安装程序。");
                
                // 彻底停止
                stopStreaming();
                return;
            }

            // 尝试自动重启
            QString msg = QString("捕获进程异常退出，正在尝试重启... (重试 %1/%2)").arg(m_crashTimestamps.size()).arg(MAX_CRASH_COUNT);
            m_statusLabel->setText(msg);
            m_statusLabel->setStyleSheet("QLabel { color: #ff9800; font-weight: bold; padding: 5px; }");
            
            Q_UNUSED(m_trayIcon);
            
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
    static const QByteArray kViewerJoinedPrefix("EVT_VIEWER_JOINED:");
    static const QByteArray kViewerNamePrefix("EVT_VIEWER_NAME:");
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
                    scheduleNoViewerSoftStop();
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
        } else if (line.startsWith(kViewerJoinedPrefix)) {
            const QByteArray payload = line.mid(kViewerJoinedPrefix.size()).trimmed();
            const int sep = payload.indexOf(':');
            QString viewerId;
            QString viewerName;
            if (sep > 0) {
                viewerId = QString::fromUtf8(payload.left(sep));
                viewerName = QString::fromUtf8(payload.mid(sep + 1));
            } else {
                viewerId = QString::fromUtf8(payload);
            }
            if (!viewerId.isEmpty() && m_transparentImageList) {
                m_transparentImageList->addViewer(viewerId, viewerName);
            }
        } else if (line.startsWith(kViewerNamePrefix)) {
            const QByteArray payload = line.mid(kViewerNamePrefix.size()).trimmed();
            const int sep = payload.indexOf(':');
            if (sep > 0) {
                const QString viewerId = QString::fromUtf8(payload.left(sep));
                const QString viewerName = QString::fromUtf8(payload.mid(sep + 1));
                if (!viewerId.isEmpty() && m_transparentImageList) {
                    m_transparentImageList->updateViewerNameIfExists(viewerId, viewerName);
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
    return AppConfig::configFilePathInAppDir();
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
    return AppConfig::serverAddress();
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

    QTimer::singleShot(0, this, &MainWindow::connectToLoginServer);
}

void MainWindow::connectToLoginServer()
{
    if (m_loginWebSocket->state() == QAbstractSocket::ConnectedState ||
        m_loginWebSocket->state() == QAbstractSocket::ConnectingState) {
        return;
    }
    QString serverUrl = QString("%1/login").arg(AppConfig::wsBaseUrl());  // 使用专门的登录路径
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
    const QSet<QString> previousUserIds = m_serverOnlineUsers;
    QHash<QString, QString> previousUserNameById;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem *item = m_listWidget->item(i);
        if (!item) continue;
        const QString uid = item->data(Qt::UserRole).toString();
        if (uid.isEmpty()) continue;
        QString name = item->text();
        int idx = name.lastIndexOf(" (");
        if (idx != -1) name = name.left(idx);
        previousUserNameById.insert(uid, name);
    }
    m_serverOnlineUsers.clear();
    QSet<QString> newUserIds;
    QHash<QString, QString> newUserNameById;
    QHash<QString, int> newUserIconById;
    for (int i = 0; i < users.size(); ++i) {
        QJsonObject userObj = users[i].toObject();
        if (!userObj.isEmpty()) {
            QString uid = userObj["id"].toString();
            if (!uid.isEmpty()) {
                newUserNameById.insert(uid, userObj["name"].toString());

                int iconId = -1;
                if (userObj.contains("icon_id")) {
                    QJsonValue v = userObj.value("icon_id");
                    iconId = v.isString() ? v.toString().toInt() : v.toInt(-1);
                } else if (userObj.contains("viewer_icon_id")) {
                    QJsonValue v = userObj.value("viewer_icon_id");
                    iconId = v.isString() ? v.toString().toInt() : v.toInt(-1);
                }
                newUserIconById.insert(uid, iconId);
            }
            m_serverOnlineUsers.insert(uid);
            newUserIds.insert(uid);
        }
    }

    const bool toastEnabled = loadOnlineNotificationEnabledFromConfig();
    const bool shouldToastThisRound = toastEnabled && m_userListInitialized;
    if (shouldToastThisRound) {
        QSet<QString> delta = newUserIds;
        delta.subtract(previousUserIds);
        for (const QString &uid : delta) {
            if (uid.isEmpty() || uid == m_userId) continue;
            const QString name = newUserNameById.value(uid);
            showUserOnlineToast(uid, name, newUserIconById.value(uid, -1));
        }

        QSet<QString> left = previousUserIds;
        left.subtract(newUserIds);
        for (const QString &uid : left) {
            if (uid.isEmpty() || uid == m_userId) continue;
            const QString name = previousUserNameById.value(uid);
            showUserOfflineToast(uid, name, -1);
        }
    }
    m_userListInitialized = true;

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

static QPixmap buildSquarePixmapForToast(const QPixmap &src, int size)
{
    const int s = qMax(8, size);
    if (src.isNull()) {
        return QPixmap();
    }

    const QPixmap scaled = src.scaled(s, s, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = qMax(0, (scaled.width() - s) / 2);
    const int y = qMax(0, (scaled.height() - s) / 2);
    return scaled.copy(x, y, s, s);
}

static QPixmap loadAvatarPixmapForToast(const QString &userId, int iconId, int size)
{
    const QString appDir = QCoreApplication::applicationDirPath();

    if (!userId.isEmpty()) {
        const QString cachedPath = QDir(appDir).filePath(QStringLiteral("avatars/%1.png").arg(userId));
        QPixmap cached(cachedPath);
        if (!cached.isNull()) {
            return buildSquarePixmapForToast(cached, size);
        }
    }

    if (iconId >= 0) {
        const QString iconPath = QDir(appDir).filePath(QStringLiteral("maps/icon/%1.png").arg(iconId));
        QPixmap icon(iconPath);
        if (!icon.isNull()) {
            return buildSquarePixmapForToast(icon, size);
        }
    }

    const QString candidate1 = QDir(appDir).filePath(QStringLiteral("maps/logo/head.png"));
    const QString candidate2 = QDir::current().filePath(QStringLiteral("src/maps/logo/head.png"));
    const QString avatarPath = QFileInfo::exists(candidate1) ? candidate1 : candidate2;
    QPixmap head(avatarPath);
    if (!head.isNull()) {
        return buildSquarePixmapForToast(head, size);
    }

    QPixmap fallback(size, size);
    fallback.fill(QColor(220, 220, 220));
    return buildSquarePixmapForToast(fallback, size);
}

void MainWindow::showInviteNotification(const QString &inviterId, const QString &inviterName, const QString &type)
{
    if (m_activeInviteNotification) {
        m_onlineToasts.removeAll(m_activeInviteNotification);
        m_activeInviteNotification->deleteLater();
        m_activeInviteNotification = nullptr;
    }

    QWidget *toast = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint);
    m_activeInviteNotification = toast;
    toast->setObjectName("InviteNotification");
    toast->setAttribute(Qt::WA_TranslucentBackground);
    toast->setAttribute(Qt::WA_ShowWithoutActivating);

    QWidget *body = new QWidget(toast);
    // Dark background for invite
    body->setStyleSheet("background-color: rgba(40, 40, 45, 230); border: 1px solid rgba(0, 200, 83, 100); border-radius: 18px;");
    body->setMinimumSize(420, 120);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(body);
    mainLayout->setContentsMargins(20, 18, 20, 18);
    mainLayout->setSpacing(10);

    // Title & Content
    QHBoxLayout *contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(14);
    
    QLabel *avatar = new QLabel(body);
    avatar->setFixedSize(56, 56);
    // Use generic avatar or try to load user avatar if possible
    QString appDir = QCoreApplication::applicationDirPath();
    QPixmap pix(appDir + "/maps/logo/log.png");
    if (pix.isNull()) {
        pix = QPixmap(56, 56);
        pix.fill(Qt::transparent);
    } else {
        pix = pix.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    avatar->setPixmap(pix); // Ideally we should load user avatar here
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet("background: transparent; border: none;");
    contentLayout->addWidget(avatar);

    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setSpacing(4);
    
    QLabel *titleLabel = new QLabel(QStringLiteral("会议邀请"), body);
    titleLabel->setStyleSheet("color: #e0e0e0; font-size: 12px; font-weight: normal; background: transparent; border: none;");
    textLayout->addWidget(titleLabel);

    QLabel *msgLabel = new QLabel(QStringLiteral("%1 邀请您加入视频通话").arg(inviterName), body);
    msgLabel->setStyleSheet("color: #ffffff; font-size: 16px; font-weight: 800; background: transparent; border: none;");
    msgLabel->setWordWrap(true);
    textLayout->addWidget(msgLabel);
    
    contentLayout->addLayout(textLayout, 1);
    mainLayout->addLayout(contentLayout);

    // Buttons
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);

    QPushButton *rejectBtn = new QPushButton(QStringLiteral("拒绝"), body);
    rejectBtn->setCursor(Qt::PointingHandCursor);
    rejectBtn->setFixedSize(80, 32);
    rejectBtn->setStyleSheet(
        "QPushButton { background-color: rgba(255, 255, 255, 0.1); color: #fff; border-radius: 4px; border: none; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.2); }"
    );

    QPushButton *acceptBtn = new QPushButton(QStringLiteral("接受"), body);
    acceptBtn->setCursor(Qt::PointingHandCursor);
    acceptBtn->setFixedSize(80, 32);
    acceptBtn->setStyleSheet(
        "QPushButton { background-color: #00C853; color: #fff; border-radius: 4px; border: none; font-weight: bold; }"
        "QPushButton:hover { background-color: #00E676; }"
    );

    btnLayout->addStretch();
    btnLayout->addWidget(rejectBtn);
    btnLayout->addWidget(acceptBtn);
    mainLayout->addLayout(btnLayout);

    QVBoxLayout *root = new QVBoxLayout(toast);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(body);

    toast->adjustSize();
    body->adjustSize();

    // Add to toasts list for positioning
    m_onlineToasts.append(toast);
    
    // Auto-cleanup on destroy
    connect(toast, &QObject::destroyed, this, [this, toast]() {
        m_onlineToasts.removeAll(toast);
        if (m_activeInviteNotification == toast) {
            m_activeInviteNotification = nullptr;
        }
        repositionOnlineToasts();
    });

    // 30s Timer for auto-reject/expire
    QTimer *timer = new QTimer(toast);
    timer->setSingleShot(true);
    timer->setInterval(30000);
    
    connect(timer, &QTimer::timeout, this, [this, inviterName, inviterId, toast]() {
        // Close current toast
        toast->deleteLater();
        
        // Show expired notification
        showExpiredInviteNotification(inviterName);
        
        // Auto-reject: Send CANCELED to server with reason="timeout"
        QJsonObject rejectMsg;
        rejectMsg["type"] = "watch_request_canceled";
        rejectMsg["viewer_id"] = getDeviceId(); // Me
        rejectMsg["target_id"] = inviterId;
        rejectMsg["reason"] = "timeout";
        rejectMsg["viewer_name"] = m_userName.isEmpty() ? getDeviceId() : m_userName;
        
        if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
            m_loginWebSocket->sendTextMessage(QJsonDocument(rejectMsg).toJson(QJsonDocument::Compact));
        }
    });
    timer->start();

    // Button Actions
    connect(rejectBtn, &QPushButton::clicked, this, [this, inviterId, toast]() {
        toast->deleteLater();
        
        QJsonObject rejectMsg;
        rejectMsg["type"] = "watch_request_canceled";
        rejectMsg["viewer_id"] = getDeviceId();
        rejectMsg["target_id"] = inviterId;
        rejectMsg["reason"] = "user_action";
        rejectMsg["viewer_name"] = m_userName.isEmpty() ? getDeviceId() : m_userName;
        
        if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
            m_loginWebSocket->sendTextMessage(QJsonDocument(rejectMsg).toJson(QJsonDocument::Compact));
        }
    });

    connect(acceptBtn, &QPushButton::clicked, this, [this, inviterId, type, toast]() {
        toast->deleteLater();

        // [Fix] Handle Audio Call Accept (Viewer-Initiated)
        if (type == QStringLiteral("audio_call")) {
            // 1. Send watch_request_accepted
            QJsonObject accepted;
            accepted["type"] = "watch_request_accepted";
            accepted["viewer_id"] = inviterId; // Alice
            accepted["target_id"] = getDeviceId(); // Me (Bob)
            
            if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                m_loginWebSocket->sendTextMessage(QJsonDocument(accepted).toJson(QJsonDocument::Compact));
            }

            // 2. Send streaming_ok
            QJsonObject okMsg;
            okMsg["type"] = "streaming_ok";
            okMsg["viewer_id"] = inviterId;
            okMsg["target_id"] = getDeviceId();
            okMsg["stream_url"] = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), getDeviceId());
            
            if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                m_loginWebSocket->sendTextMessage(QJsonDocument(okMsg).toJson(QJsonDocument::Compact));
            }

            // 3. Join Janus Room (My Room) and Show UI
            if (m_transparentImageList) {
                m_transparentImageList->show();
                m_transparentImageList->janusSwitchToMyRoom();
                m_transparentImageList->janusSetIgnoreAlone(true);
                m_transparentImageList->showAudioCallUiForSession(inviterId, true);
            }
            
            // 4. Ensure Streaming (CaptureProcess) is started if needed (for heartbeat/status)
            if (!m_isStreaming) {
                startStreaming();
            }
            return;
        }

        // Standard Accept Logic (for Screen Share / Invite Response)
        // 1. Send watch_request with action="invite_response"
        QJsonObject req;
        req["type"] = "watch_request";
        req["viewer_id"] = getDeviceId();
        req["target_id"] = inviterId;
        req["action"] = "invite_response";
        
        if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
            m_loginWebSocket->sendTextMessage(QJsonDocument(req).toJson(QJsonDocument::Compact));
        }
        
        // 2. Open NewUiWindow and prepare
        if (m_transparentImageList) {
            m_transparentImageList->show();
        }
    });

    repositionOnlineToasts();
    toast->show();
    toast->raise();
}

void MainWindow::showToastNotification(const QString &message, bool isWarning, const QString &userId)
{
    QWidget *toast = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint);
    toast->setAttribute(Qt::WA_TranslucentBackground);
    toast->setAttribute(Qt::WA_ShowWithoutActivating);

    QWidget *body = new QWidget(toast);
    // Style: Red for warning (timeout), Blue for info (reject) - Consistent with Offline/Notice style
    QString bg = isWarning ? "rgba(140, 70, 70, 255)" : "rgba(70, 90, 120, 255)";
    body->setStyleSheet(QString("background-color: %1; border: none; border-radius: 18px;").arg(bg));
    body->setMinimumSize(420, 96);
    
    QHBoxLayout *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(20, 18, 20, 18);
    bodyLayout->setSpacing(14);

    const int avatarSize = 56;
    QLabel *avatar = new QLabel(body);
    avatar->setFixedSize(avatarSize, avatarSize);
    // Use userId if provided to load avatar, otherwise default
    avatar->setPixmap(loadAvatarPixmapForToast(userId, -1, avatarSize));
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet("background: transparent;");
    bodyLayout->addWidget(avatar);

    QLabel *label = new QLabel(message, body);
    // Use slightly smaller font than "Online" (24px) to accommodate longer messages
    label->setStyleSheet("color: #ffffff; font-size: 18px; font-weight: 800; background: transparent; border: none;");
    label->setWordWrap(true);
    bodyLayout->addWidget(label, 1);

    QPushButton *closeBtn = new QPushButton("×", body);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton { color: #ccc; background: transparent; border: none; font-size: 20px; font-weight: bold; margin-top: -10px; }"
        "QPushButton:hover { color: #fff; }"
    );
    connect(closeBtn, &QPushButton::clicked, toast, &QWidget::deleteLater);
    
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->addWidget(closeBtn);
    rightLayout->addStretch();
    bodyLayout->addLayout(rightLayout);

    QVBoxLayout *root = new QVBoxLayout(toast);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(body);

    toast->adjustSize();
    body->adjustSize();

    m_onlineToasts.append(toast);

    connect(toast, &QObject::destroyed, this, [this, toast]() {
        m_onlineToasts.removeAll(toast);
        repositionOnlineToasts();
    });

    // Auto-close after 5 seconds
    QTimer::singleShot(5000, toast, &QWidget::deleteLater);

    repositionOnlineToasts();
    toast->show();
    toast->raise();
}

void MainWindow::showExpiredInviteNotification(const QString &inviterName)
{
    QWidget *toast = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint);
    toast->setAttribute(Qt::WA_TranslucentBackground);
    toast->setAttribute(Qt::WA_ShowWithoutActivating);

    QWidget *body = new QWidget(toast);
    // Dark background consistent with invite
    body->setStyleSheet("background-color: rgba(40, 40, 45, 230); border: 1px solid rgba(255, 80, 80, 100); border-radius: 18px;");
    body->setMinimumSize(420, 96);
    
    QHBoxLayout *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(20, 18, 20, 18);
    bodyLayout->setSpacing(14);

    QLabel *icon = new QLabel(body);
    icon->setFixedSize(56, 56);
    icon->setStyleSheet("background: transparent; border: none; font-size: 30px;");
    icon->setText("⏰"); 
    icon->setAlignment(Qt::AlignCenter);
    bodyLayout->addWidget(icon);

    QLabel *label = new QLabel(body);
    label->setStyleSheet("color: #cccccc; font-size: 16px; font-weight: bold; background: transparent; border: none;");
    label->setWordWrap(true);
    bodyLayout->addWidget(label, 1);

    QPushButton *closeBtn = new QPushButton("×", body);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton { color: #999; background: transparent; border: none; font-size: 20px; font-weight: bold; margin-top: -10px; }"
        "QPushButton:hover { color: #fff; }"
    );
    connect(closeBtn, &QPushButton::clicked, toast, &QWidget::deleteLater);
    
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->addWidget(closeBtn);
    rightLayout->addStretch();
    bodyLayout->addLayout(rightLayout);

    QVBoxLayout *root = new QVBoxLayout(toast);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(body);

    toast->adjustSize();
    body->adjustSize();

    m_onlineToasts.append(toast);

    connect(toast, &QObject::destroyed, this, [this, toast]() {
        m_onlineToasts.removeAll(toast);
        repositionOnlineToasts();
    });

    // Persistent timer to update elapsed time
    QDateTime startTime = QDateTime::currentDateTime();
    QTimer *updateTimer = new QTimer(toast);
    auto updateFunc = [label, inviterName, startTime]() {
        if (!label) return;
        qint64 diff = startTime.secsTo(QDateTime::currentDateTime());
        int h = diff / 3600;
        int m = (diff % 3600) / 60;
        int s = diff % 60;
        label->setText(QStringLiteral("用户%1的视频邀请已过时%2小时%3分钟%4秒")
                       .arg(inviterName).arg(h).arg(m).arg(s));
    };
    
    updateFunc(); // Initial update
    connect(updateTimer, &QTimer::timeout, toast, updateFunc);
    updateTimer->start(1000);

    repositionOnlineToasts();
    toast->show();
    toast->raise();
}

void MainWindow::showUserOnlineToast(const QString& userId, const QString& userName, int iconId)
{
    QWidget *toast = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint);
    toast->setAttribute(Qt::WA_TranslucentBackground);
    toast->setAttribute(Qt::WA_ShowWithoutActivating);

    QWidget *body = new QWidget(toast);
    body->setStyleSheet("background-color: rgba(255, 255, 255, 255); border: none; border-radius: 18px;");
    body->setMinimumSize(420, 96);
    QHBoxLayout *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(20, 18, 20, 18);
    bodyLayout->setSpacing(14);

    const int avatarSize = 56;
    QLabel *avatar = new QLabel(body);
    avatar->setFixedSize(avatarSize, avatarSize);
    avatar->setPixmap(loadAvatarPixmapForToast(userId, iconId, avatarSize));
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet("background: transparent;");
    bodyLayout->addWidget(avatar);

    const QString display = userName.isEmpty() ? userId : userName;
    QLabel *label = new QLabel(QStringLiteral("%1已上线 😊").arg(display), body);
    label->setStyleSheet("color: #111111; font-size: 24px; font-weight: 800; background: transparent;");
    bodyLayout->addWidget(label);

    QPushButton *closeBtn = new QPushButton("×", body);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton { color: #999; background: transparent; border: none; font-size: 20px; font-weight: bold; margin-top: -10px; }"
        "QPushButton:hover { color: #333; }"
    );
    connect(closeBtn, &QPushButton::clicked, toast, &QWidget::deleteLater);
    
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->addWidget(closeBtn);
    rightLayout->addStretch();
    bodyLayout->addLayout(rightLayout);

    QVBoxLayout *root = new QVBoxLayout(toast);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(body);

    toast->adjustSize();
    body->adjustSize();

    m_onlineToasts.append(toast);

    connect(toast, &QObject::destroyed, this, [this, toast]() {
        m_onlineToasts.removeAll(toast);
        repositionOnlineToasts();
    });

    repositionOnlineToasts();
    toast->show();
    toast->raise();

    QTimer::singleShot(5000, toast, &QWidget::deleteLater);
}

void MainWindow::showUserOfflineToast(const QString& userId, const QString& userName, int iconId)
{
    QWidget *toast = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint);
    toast->setAttribute(Qt::WA_TranslucentBackground);
    toast->setAttribute(Qt::WA_ShowWithoutActivating);

    QWidget *body = new QWidget(toast);
    // Low saturation blue
    body->setStyleSheet("background-color: rgba(70, 90, 120, 255); border: none; border-radius: 18px;");
    body->setMinimumSize(420, 96);
    QHBoxLayout *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(20, 18, 20, 18);
    bodyLayout->setSpacing(14);

    const int avatarSize = 56;
    QLabel *avatar = new QLabel(body);
    avatar->setFixedSize(avatarSize, avatarSize);
    avatar->setPixmap(loadAvatarPixmapForToast(userId, iconId, avatarSize));
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet("background: transparent;");
    bodyLayout->addWidget(avatar);

    const QString display = userName.isEmpty() ? userId : userName;
    QLabel *label = new QLabel(QStringLiteral("%1已下班").arg(display), body);
    label->setStyleSheet("color: #ffffff; font-size: 24px; font-weight: 800; background: transparent;");
    bodyLayout->addWidget(label);

    QPushButton *closeBtn = new QPushButton("×", body);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton { color: #ccc; background: transparent; border: none; font-size: 20px; font-weight: bold; margin-top: -10px; }"
        "QPushButton:hover { color: #fff; }"
    );
    connect(closeBtn, &QPushButton::clicked, toast, &QWidget::deleteLater);
    
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->addWidget(closeBtn);
    rightLayout->addStretch();
    bodyLayout->addLayout(rightLayout);

    QVBoxLayout *root = new QVBoxLayout(toast);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(body);

    toast->adjustSize();
    body->adjustSize();

    m_onlineToasts.append(toast);

    connect(toast, &QObject::destroyed, this, [this, toast]() {
        m_onlineToasts.removeAll(toast);
        repositionOnlineToasts();
    });

    repositionOnlineToasts();
    toast->show();
    toast->raise();

    QTimer::singleShot(5000, toast, &QWidget::deleteLater);
}

void MainWindow::repositionOnlineToasts()
{
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const QRect avail = screen->availableGeometry();
    const int rightMargin = 20;
    const int bottomMargin = 20;
    const int gap = 10;

    int y = avail.bottom() - bottomMargin + 1;
    for (int i = m_onlineToasts.size() - 1; i >= 0; --i) {
        QWidget *toast = m_onlineToasts[i];
        if (!toast) continue;
        toast->adjustSize();
        const int x = avail.right() - rightMargin - toast->width() + 1;
        y -= toast->height();
        toast->move(x, y);
        y -= gap;
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
    m_lastBroadcastStatus.clear();
    if (m_heartbeatTimer) { m_heartbeatTimer->stop(); }
    
    m_listWidget->clear();
    m_listWidget->addItem("与服务器断开连接");
    
    // 断开连接时，清空在线用户蓄水池和桌面头像
    // 因为相对我而言，所有人都“掉线”了
    m_serverOnlineUsers.clear();
    m_userListInitialized = false;
    m_transparentImageList->clearUserList();
    const QList<QWidget*> toasts = m_onlineToasts;
    for (QWidget *toast : toasts) {
        if (toast) {
            toast->deleteLater();
        }
    }
    m_onlineToasts.clear();

    // 5秒后尝试重新连接 (如果定时器已在运行，start会重置它，避免重复)
    m_reconnectTimer->start(5000);
}

void MainWindow::onLoginWebSocketTextMessageReceived(const QString &message)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    
    if (error.error != QJsonParseError::NoError) {
        if (message.contains("kick_viewer")) {
            /*
            // qInfo().noquote() << "[KickDiag] login ws message parse failed"
                              << " error=" << error.errorString()
                              << " raw=" << message;
            */
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
            // broadcastStatusIfChanged();
            if (m_transparentImageList) {
                sendActivityStateBroadcast(m_transparentImageList->localActivityActive());
            }
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
    } else if (type == "broadcast_notice") {
        QString content = obj["content"].toString();
        QJsonParseError contentError;
        QJsonDocument contentDoc = QJsonDocument::fromJson(content.toUtf8(), &contentError);
        if (contentError.error == QJsonParseError::NoError && contentDoc.isObject()) {
            QJsonObject payload = contentDoc.object();
            if (payload.value("kind").toString() == QStringLiteral("activity_state")) {
                const QString userId = payload.value("user_id").toString();
                if (!userId.isEmpty() && userId != getDeviceId() && m_transparentImageList) {
                    const bool active = payload.value("active").toBool(true);
                    m_transparentImageList->setRemoteActivityState(userId, active);
                }
                return;
            }
        }
        if (content.startsWith(QStringLiteral("设备ID:")) &&
            content.contains(QStringLiteral(" | 状态:")) &&
            content.contains(QStringLiteral(" | 连接:"))) {
            return;
        }
        QString sender = obj["sender_name"].toString();
        QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        showNoticeToast(content, sender, timeStr);
    } else if (type == "start_streaming_request") {
        QString viewerId = obj["viewer_id"].toString();
        QString targetId = obj["target_id"].toString();
        const QString action = obj.value("action").toString();

        // [Fix] 处理邀请回应自动同意
        if (action == "invite_response") {
            bool isConnected = m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState;
            if (isConnected) {
                // 1. 发送接受消息
                QJsonObject accepted;
                accepted["type"] = "watch_request_accepted";
                accepted["viewer_id"] = viewerId;
                accepted["target_id"] = targetId;
                QJsonDocument accDoc(accepted);
                m_loginWebSocket->sendTextMessage(accDoc.toJson(QJsonDocument::Compact));

                // 2. 确保推流开启
                if (m_currentWatchdogSocket && m_currentWatchdogSocket->state() == QLocalSocket::ConnectedState) {
                    m_currentWatchdogSocket->write("CMD_APPROVE");
                    m_currentWatchdogSocket->flush();
                } else {
                    m_pendingApproval = true;
                }

                if (!m_isStreaming) {
                    startStreaming();
                }

                // 3. 发送Streaming OK
                QJsonObject okMsg;
                okMsg["type"] = "streaming_ok";
                okMsg["viewer_id"] = viewerId;
                okMsg["target_id"] = targetId;
                okMsg["stream_url"] = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), targetId);
                QJsonDocument okDoc(okMsg);
                m_loginWebSocket->sendTextMessage(okDoc.toJson(QJsonDocument::Compact));

                // 4. 显示语音通话窗口 (For Producer)
                if (m_transparentImageList) {
                    m_transparentImageList->janusSwitchToMyRoom();
                    m_transparentImageList->janusSetIgnoreAlone(true);
                    m_transparentImageList->showAudioCallUiForSession(targetId, true);
                }
            }
            return;
        }

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
                
                QString viewerName = obj.value("viewer_name").toString();
                if (viewerName.isEmpty() && m_listWidget) {
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
                if (viewerName.isEmpty()) {
                    viewerName = QStringLiteral("访客");
                }
                
                showToastNotification(QStringLiteral("用户 %1 已取消观看请求").arg(viewerName), true, viewerId);
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

        QString viewerName = obj.value("viewer_name").toString();
        
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
        if (viewerName.isEmpty()) {
            viewerName = QStringLiteral("访客");
        }
        
        // const QString action = obj.value("action").toString(); // Already defined above
        const bool audioOnly = obj.value("audio_only").toBool(false) || action == "audio_only";
        bool manualApproval = loadManualApprovalEnabledFromConfig() || audioOnly;
        bool isConnected = m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState;

        /* [Removed] Auto-accept for audio calls is disabled to allow manual approval
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
            streamOkResponse["stream_url"] = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), targetId);
            QJsonDocument responseDoc(streamOkResponse);
            m_loginWebSocket->sendTextMessage(responseDoc.toJson(QJsonDocument::Compact));
            return;
        }
        */

        if (manualApproval && isConnected) {
            // 1. 发送需要审批的消息给观看端
            QJsonObject approval;
            approval["type"] = "approval_required";
            approval["viewer_id"] = viewerId;
            approval["target_id"] = targetId;
            QJsonDocument doc(approval);
            m_loginWebSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));

            // 2. 弹出右下角标准通知 Toast (Standard Bottom-Right)
            if (m_approvalDialog) {
                m_approvalDialog->close();
                m_approvalDialog->deleteLater();
                m_approvalDialog = nullptr;
            }
            
            QWidget *toast = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint);
            toast->setAttribute(Qt::WA_TranslucentBackground);
            toast->setAttribute(Qt::WA_ShowWithoutActivating);

            QWidget *body = new QWidget(toast);
            // Blue background for requests
            body->setStyleSheet("background-color: rgba(70, 90, 120, 255); border: none; border-radius: 18px;");
            body->setMinimumSize(420, 110);
            
            QHBoxLayout *bodyLayout = new QHBoxLayout(body);
            bodyLayout->setContentsMargins(20, 15, 20, 15);
            bodyLayout->setSpacing(15);

            // Avatar
            const int avatarSize = 56;
            QLabel *avatar = new QLabel(body);
            avatar->setFixedSize(avatarSize, avatarSize);
            avatar->setPixmap(loadAvatarPixmapForToast(viewerId, -1, avatarSize));
            avatar->setStyleSheet("background: transparent;");
            bodyLayout->addWidget(avatar);

            // Content (Text + Buttons)
            QVBoxLayout *contentLayout = new QVBoxLayout();
            contentLayout->setSpacing(8);
            
            QLabel *label = new QLabel((audioOnly ? QStringLiteral("用户 %1 请求语音通话") : QStringLiteral("用户 %1 请求观看您的屏幕")).arg(viewerName), body);
            label->setStyleSheet("color: #ffffff; font-size: 16px; font-weight: bold; background: transparent; border: none;");
            label->setWordWrap(true);
            contentLayout->addWidget(label);
            
            QHBoxLayout *btnLayout = new QHBoxLayout();
            btnLayout->setSpacing(10);
            
            QPushButton *rejectBtn = new QPushButton(QStringLiteral("拒绝"), body);
            rejectBtn->setCursor(Qt::PointingHandCursor);
            rejectBtn->setFixedSize(80, 30);
            rejectBtn->setStyleSheet(
                "QPushButton { background-color: rgba(255, 255, 255, 30); color: #ffffff; border-radius: 15px; font-size: 13px; border: none; font-weight: bold; }"
                "QPushButton:hover { background-color: rgba(255, 255, 255, 50); }"
            );
            
            QPushButton *acceptBtn = new QPushButton(QStringLiteral("允许"), body);
            acceptBtn->setCursor(Qt::PointingHandCursor);
            acceptBtn->setFixedSize(80, 30);
            acceptBtn->setStyleSheet(
                "QPushButton { background-color: #00C853; color: #ffffff; border-radius: 15px; font-size: 13px; border: none; font-weight: bold; }"
                "QPushButton:hover { background-color: #00E676; }"
            );
            
            btnLayout->addWidget(rejectBtn);
            btnLayout->addWidget(acceptBtn);
            btnLayout->addStretch();
            
            contentLayout->addLayout(btnLayout);
            bodyLayout->addLayout(contentLayout, 1);
            
            // Close Button (Top Right)
            QVBoxLayout *rightLayout = new QVBoxLayout();
            QPushButton *closeBtn = new QPushButton("×", body);
            closeBtn->setFixedSize(24, 24);
            closeBtn->setStyleSheet(
                "QPushButton { color: #ccc; background: transparent; border: none; font-size: 20px; font-weight: bold; margin-top: -5px; }"
                "QPushButton:hover { color: #fff; }"
            );
            connect(closeBtn, &QPushButton::clicked, toast, &QWidget::close);
            rightLayout->addWidget(closeBtn);
            rightLayout->addStretch();
            bodyLayout->addLayout(rightLayout);

            QVBoxLayout *root = new QVBoxLayout(toast);
            root->setContentsMargins(0, 0, 0, 0);
            root->addWidget(body);
            
            m_approvalDialog = toast;
            m_onlineToasts.append(toast);
            
            connect(toast, &QObject::destroyed, this, [this, toast]() {
                if (m_approvalDialog == toast) m_approvalDialog = nullptr;
                m_onlineToasts.removeAll(toast);
                repositionOnlineToasts();
            });

            // Connect Logic
            connect(acceptBtn, &QPushButton::clicked, this, [this, toast, viewerId, targetId, viewerName, audioOnly]() {
                // [Fix] Handle Audio Call Accept (Viewer-Initiated)
                if (audioOnly) {
                    // 1. Send watch_request_accepted
                    QJsonObject accepted;
                    accepted["type"] = "watch_request_accepted";
                    accepted["viewer_id"] = viewerId;
                    accepted["target_id"] = targetId;
                    accepted["audio_only"] = true;
                    
                    if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                        m_loginWebSocket->sendTextMessage(QJsonDocument(accepted).toJson(QJsonDocument::Compact));
                    }

                    // 2. Join Janus Room (My Room) and Show UI
                    if (m_transparentImageList) {
                        m_transparentImageList->janusSwitchToMyRoom();
                        m_transparentImageList->janusSetIgnoreAlone(true);
                        m_transparentImageList->showAudioCallUiForSession(viewerId, true);
                    }
                    
                    toast->close();
                    return;
                }

                // Standard Accept Logic (for Screen Share / Invite Response)
                // 同意
                QJsonObject accepted;
                accepted["type"] = "watch_request_accepted";
                accepted["viewer_id"] = viewerId;
                accepted["target_id"] = targetId;
                QJsonDocument accDoc(accepted);
                if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                    m_loginWebSocket->sendTextMessage(accDoc.toJson(QJsonDocument::Compact));
                }

                if (m_transparentImageList) {
                    m_transparentImageList->addViewer(viewerId, viewerName);
                    m_transparentImageList->janusSwitchToMyRoom();
                    m_transparentImageList->janusSetIgnoreAlone(true);
                    m_transparentImageList->showAudioCallUiForSession(viewerId, true);
                    m_transparentImageList->showAudioCallMiniBar();
                    scheduleNoViewerSoftStop();
                }

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
                streamOkResponse["stream_url"] = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), targetId);
                QJsonDocument responseDoc(streamOkResponse);
                if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                    m_loginWebSocket->sendTextMessage(responseDoc.toJson(QJsonDocument::Compact));
                }

                toast->close();
            });
            
            connect(rejectBtn, &QPushButton::clicked, this, [this, toast, viewerId, targetId]() {
                // 拒绝
                QJsonObject rejected;
                rejected["type"] = "watch_request_rejected";
                rejected["viewer_id"] = viewerId;
                rejected["target_id"] = targetId;
                QJsonDocument rejDoc(rejected);
                if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                    m_loginWebSocket->sendTextMessage(rejDoc.toJson(QJsonDocument::Compact));
                }

                if (m_currentWatchdogSocket && m_currentWatchdogSocket->state() == QLocalSocket::ConnectedState) {
                    m_currentWatchdogSocket->write("CMD_REJECT");
                    m_currentWatchdogSocket->flush();
                    qDebug() << "Sent local rejection command to CaptureProcess";
                }
                
                toast->close();
            });

            repositionOnlineToasts();
            toast->show();
            toast->raise();
        } else {
            bool neededStart = !m_isStreaming;
            if (neededStart) {
                startStreaming();
            }
            QJsonObject streamOkResponse;
            streamOkResponse["type"] = "streaming_ok";
            streamOkResponse["viewer_id"] = viewerId;
            streamOkResponse["target_id"] = targetId;
            streamOkResponse["stream_url"] = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), targetId);
            
            auto sendOk = [this, streamOkResponse]() {
                if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                    m_loginWebSocket->sendTextMessage(QJsonDocument(streamOkResponse).toJson(QJsonDocument::Compact));
                }
            };

            if (neededStart) {
                QTimer::singleShot(1500, this, sendOk);
            } else {
                sendOk();
            }
            
            // [Fix] Add to "My Room" list for auto-approve case
            if (m_transparentImageList) {
                m_transparentImageList->addViewer(viewerId, viewerName);
                m_transparentImageList->janusSwitchToMyRoom();
                m_transparentImageList->janusSetIgnoreAlone(true);
                m_transparentImageList->showAudioCallUiForSession(viewerId, true);
                m_transparentImageList->showAudioCallMiniBar();
                scheduleNoViewerSoftStop();
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
            QString viewerName = obj.value("viewer_name").toString();
            if (viewerName.isEmpty() && m_listWidget) {
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
            if (viewerName.isEmpty()) {
                viewerName = QStringLiteral("访客");
            }
            showToastNotification(QStringLiteral("用户 %1 已取消观看请求").arg(viewerName), true, viewerId);
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
        if (m_transparentImageList && m_transparentImageList->getCurrentUserId() != targetId) {
            m_transparentImageList->setWatchingTarget(QString());
            if (m_transparentImageList->isEmbeddedWatchingTarget(targetId)) {
                m_transparentImageList->stopEmbeddedWatching();
            }
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
        QString targetId = obj["target_id"].toString();

        // [New Logic] Handle Producer-Initiated Invite
        if (obj.value("is_invite").toBool()) {
            if (viewerId == getDeviceId()) {
                QString inviterName = obj.value("inviter_name").toString();
                QString displayName = inviterName.isEmpty() ? targetId : inviterName;
                
                showInviteNotification(targetId, displayName, QStringLiteral("watch_request_accepted"));
            }
            return;
        }

        // [Fix] Handle Audio Call Acceptance
        if (obj.value("audio_only").toBool()) {
            if (viewerId == getDeviceId()) {
                if (m_waitingDialog) {
                    m_waitingDialog->close();
                    m_waitingDialog->deleteLater();
                    m_waitingDialog = nullptr;
                }
                
                if (!targetId.isEmpty() && m_transparentImageList) {
                    m_transparentImageList->janusSwitchToUserRoom(targetId);
                    m_transparentImageList->setTalkPending(targetId, false);
                    m_transparentImageList->setTalkConnected(targetId, true);
                    
                    // Send mic state enabled
                    QJsonObject msg;
                    msg["type"] = "viewer_mic_state";
                    msg["viewer_id"] = getDeviceId();
                    msg["target_id"] = targetId;
                    msg["enabled"] = true;
                    msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
                    if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                        m_loginWebSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
                    }
                }
            }
            return;
        }

        if (viewerId == getDeviceId()) {
            // Close the waiting dialog if any
            if (m_waitingDialog) {
                m_waitingDialog->close();
                m_waitingDialog->deleteLater();
                m_waitingDialog = nullptr;
            }
            
            // startVideoReceiving(targetId); // 移除此处调用，等待 streaming_ok 信号再开始接收，避免重复初始化
            if (!targetId.isEmpty()) {
                QTimer::singleShot(1200, this, [this, targetId]() {
                    if (m_currentTargetId != targetId) {
                        return;
                    }
                    if (!m_transparentImageList) return;
                    auto *vd = m_transparentImageList->embeddedVideoWidget();
                    if (vd && vd->isReceiving()) return;
                    startVideoReceiving(targetId);
                });
            }
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
            if (m_transparentImageList && m_transparentImageList->getCurrentUserId() != targetId) {
                m_transparentImageList->setWatchingTarget(QString());
                if (m_transparentImageList->isEmbeddedWatchingTarget(targetId)) {
                    m_transparentImageList->stopEmbeddedWatching();
                }
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

            // Suppress "Watching Rejected" popup as requested
            // QMessageBox msgBox(this);
            // msgBox.setIcon(QMessageBox::Information);
            // msgBox.setWindowTitle(QStringLiteral("观看被拒绝"));
            // msgBox.setText(QStringLiteral("对方拒绝了观看请求"));
            // msgBox.setWindowFlags(msgBox.windowFlags() | Qt::WindowStaysOnTopHint);
            // msgBox.exec();
        }
    } else if (type == "watch_request_canceled") {
        QString viewerId = obj.value("viewer_id").toString();
        QString targetId = obj.value("target_id").toString();
        QString reason = obj.value("reason").toString();
        QString viewerName = obj.value("viewer_name").toString();
        if (viewerName.isEmpty()) viewerName = viewerId;

        // If I am the target (Host) and the viewer canceled (rejected my invite)
        if (targetId == getDeviceId()) {
                 if (reason == "timeout") {
                     showToastNotification(QStringLiteral("用户%1邀请过期，自动拒绝").arg(viewerName), true, viewerId);
                 } else {
                     showToastNotification(QStringLiteral("用户%1拒绝").arg(viewerName), false, viewerId);
                 }
            }
    } else if (type == "viewer_mic_state") {
        QString viewerId = obj.value("viewer_id").toString();
        QString targetId = obj.value("target_id").toString();
        if (targetId.isEmpty() && obj.contains("producer_id")) targetId = obj.value("producer_id").toString();
        if (targetId.isEmpty() && obj.contains("host_id")) targetId = obj.value("host_id").toString();
        if (targetId.isEmpty()) targetId = getDeviceId();
        const bool enabled = obj.value("enabled").toBool(false);
        qInfo().noquote() << "[KickDiag] viewer_mic_state recv"
                          << " viewer_id=" << viewerId
                          << " target_id=" << targetId
                          << " enabled=" << (enabled ? "true" : "false")
                          << " my_id=" << getDeviceId();
        if (targetId != getDeviceId()) {
            return;
        }
        if (viewerId.isEmpty()) {
            return;
        }
        if (m_transparentImageList) {
            m_transparentImageList->setViewerMicState(viewerId, enabled);
            
            if (enabled) {
                if (m_transparentImageList->activeAudioCallPeerId() == viewerId) {
                     m_transparentImageList->setTalkRemoteActive(viewerId, true);
                     m_transparentImageList->showAudioCallUiForSession(viewerId, true);
                } else {
                     m_transparentImageList->setTalkRemoteActive(viewerId, true);
                }
            } else {
                m_transparentImageList->setTalkRemoteActive(viewerId, false);
            }
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
                scheduleNoViewerSoftStop();
            }
        }
    } else if (type == "kick_viewer") {
        QString viewerId = obj["viewer_id"].toString();
        QString targetId = obj["target_id"].toString();
        /*
        // qInfo().noquote() << "[KickDiag] kick_viewer received on login ws"
                          << " viewer_id=" << viewerId
                          << " target_id=" << targetId
                          << " my_id=" << getDeviceId();
        */

        if (viewerId == getDeviceId()) {
            /*
            // qInfo().noquote() << "[KickDiag] kick_viewer applied on viewer side"
                              << " target_id=" << targetId;
            */
            if (m_waitingDialog) {
                m_waitingDialog->close();
                m_waitingDialog->deleteLater();
                m_waitingDialog = nullptr;
            }

            if (m_pendingTalkTargetId == targetId) {
                m_pendingTalkTargetId.clear();
                m_pendingTalkEnabled = false;
            }
            if (m_audioOnlyTargetId == targetId) {
                m_audioOnlyTargetId.clear();
            }
            if (m_currentTargetId == targetId) {
                m_currentTargetId.clear();
            }

            if (m_transparentImageList && !targetId.isEmpty()) {
                m_transparentImageList->setTalkConnected(targetId, false);
                m_transparentImageList->setTalkRemoteActive(targetId, false);
                m_transparentImageList->janusStop();
            }
            if (m_transparentImageList) {
                if (auto *vd = m_transparentImageList->embeddedVideoWidget()) {
                    vd->setTalkEnabled(false);
                    vd->setMicSendEnabled(false);
                    vd->notifyTargetOffline(QStringLiteral("你已被房主移除"));
                }
                if (!targetId.isEmpty() && m_transparentImageList->isEmbeddedWatchingTarget(targetId)) {
                    m_transparentImageList->stopEmbeddedWatching();
                }
            }
        } else {
            /*
            // qInfo().noquote() << "[KickDiag] kick_viewer ignored on this client";
            */
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
            
            const bool audioOnlySession = (m_audioOnlyTargetId == targetId);
            const bool showVideoWindow = m_pendingShowVideoWindow && !audioOnlySession;
            const bool talkWasPending = (m_pendingTalkEnabled && m_pendingTalkTargetId == targetId);
            if (audioOnlySession) {
                if (m_transparentImageList) {
                    if (talkWasPending) {
                        m_transparentImageList->setTalkPending(targetId, false);
                    }
                    if (m_transparentImageList->activeAudioCallPeerId() != targetId) {
                        m_transparentImageList->janusSwitchToUserRoom(targetId);
                        m_transparentImageList->setTalkConnected(targetId, true);
                    }
                }
                if (talkWasPending) {
                    m_pendingTalkTargetId.clear();
                    m_pendingTalkEnabled = false;
                }
                return;
            }
            m_pendingShowVideoWindow = true;
            if (showVideoWindow) {
                m_audioOnlyTargetId.clear();
                if (m_transparentImageList) {
                    m_transparentImageList->enterEmbeddedWatchingUi(targetId);
                }
            }
            startVideoReceiving(targetId, streamUrl);
            if (showVideoWindow && m_transparentImageList) {
                m_transparentImageList->setWatchingTarget(targetId);
            }
            if (m_transparentImageList) {
                const QString activePeer = m_transparentImageList->activeAudioCallPeerId();
                if (activePeer.isEmpty() || activePeer == targetId) {
                    m_transparentImageList->janusSwitchToUserRoom(targetId);
                    m_transparentImageList->showAudioCallUiForSession(targetId, true);
                    m_transparentImageList->showAudioCallMiniBar();
                }
            }
            if (talkWasPending) {
                m_pendingTalkTargetId.clear();
                m_pendingTalkEnabled = false;
            }
            if (!targetId.isEmpty() && m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
                QJsonObject msg;
                msg["type"] = "viewer_mic_state";
                msg["viewer_id"] = getDeviceId();
                msg["target_id"] = targetId;
                msg["enabled"] = true;
                msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
                m_loginWebSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
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
        if (m_transparentImageList->windowState() & Qt::WindowMinimized) {
            m_transparentImageList->setWindowState(m_transparentImageList->windowState() & ~Qt::WindowMinimized);
        }
        m_transparentImageList->show();
        m_transparentImageList->raise();
        m_transparentImageList->activateWindow();
    }
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
        connect(m_systemSettingsWindow, &SystemSettingsWindow::onlineNotificationEnabledChanged,
                this, &MainWindow::onOnlineNotificationEnabledChanged);
        connect(m_systemSettingsWindow, &SystemSettingsWindow::storyboardUrlChanged,
                this, &MainWindow::onStoryboardUrlChanged);
        connect(m_systemSettingsWindow, &SystemSettingsWindow::function2UrlChanged,
                this, &MainWindow::onFunction2UrlChanged);
        connect(m_systemSettingsWindow, &SystemSettingsWindow::function3UrlChanged,
            this, &MainWindow::onFunction3UrlChanged);
        connect(m_systemSettingsWindow, &SystemSettingsWindow::userGuideRequested,
                m_transparentImageList, &NewUiWindow::showUserGuide);
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
    QString serverUrl = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), getDeviceId());
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
    m_exitRequested = true;
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
    if (m_systemSettingsWindow) m_systemSettingsWindow->hide();
}

void MainWindow::onMicToggleRequested(bool enabled)
{
    saveMicEnabledToConfig(enabled);
    if (m_videoWindow) {
        m_videoWindow->setMicCheckedSilently(enabled);
    }
    if (m_transparentImageList) {
        m_transparentImageList->setGlobalMicCheckedSilently(enabled);
        m_transparentImageList->janusSetMuted(!enabled);
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

void MainWindow::onOnlineNotificationEnabledChanged(bool enabled)
{
    saveOnlineNotificationEnabledToConfig(enabled);
}

void MainWindow::onStoryboardUrlChanged(const QString& url)
{
    saveStoryboardUrlToConfig(url.trimmed());
}

void MainWindow::onFunction2UrlChanged(const QString& url)
{
    saveFunction2UrlToConfig(url.trimmed());
}

void MainWindow::onFunction3UrlChanged(const QString& url)
{
    saveFunction3UrlToConfig(url.trimmed());
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

void MainWindow::onSetStreamingIslandVisible(bool visible)
{
    if (!m_islandWidget) {
        return;
    }

    if (visible) {
        if (!m_islandWidget->isVisible()) {
             int screenIndex = loadScreenIndexFromConfig();
             const auto screens = QGuiApplication::screens();
             if (screenIndex >= 0 && screenIndex < screens.size()) {
                 m_islandWidget->setTargetScreen(screens[screenIndex]);
             }
             m_islandWidget->showOnScreen();
        }
    } else {
        if (m_islandWidget->isVisible()) {
            m_islandWidget->hide();
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

bool MainWindow::loadOnlineNotificationEnabledFromConfig() const
{
    QString configFilePath = getConfigFilePath();
    QFile configFile(configFilePath);
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.startsWith("online_notification_enabled=")) {
                const QString v = line.mid(QString("online_notification_enabled=").length()).trimmed();
                configFile.close();
                return v.compare("true", Qt::CaseInsensitive) == 0 || v == "1";
            }
        }
        configFile.close();
    }
    return true;
}

void MainWindow::saveOnlineNotificationEnabledToConfig(bool enabled)
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
        if (configLines[i].startsWith("online_notification_enabled=")) {
            configLines[i] = QString("online_notification_enabled=%1").arg(enabled ? "true" : "false");
            replaced = true;
            break;
        }
    }
    if (!replaced) configLines << QString("online_notification_enabled=%1").arg(enabled ? "true" : "false");

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

void MainWindow::saveStoryboardUrlToConfig(const QString &url)
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
        if (configLines[i].startsWith("storyboard_url=")) {
            configLines[i] = QString("storyboard_url=%1").arg(url);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        configLines << QString("storyboard_url=%1").arg(url);
    }
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString &line : configLines) out << line << "\n";
        configFile.close();
    }
}

void MainWindow::saveFunction2UrlToConfig(const QString &url)
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
        if (configLines[i].startsWith("function2_url=")) {
            configLines[i] = QString("function2_url=%1").arg(url);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        configLines << QString("function2_url=%1").arg(url);
    }
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString &line : configLines) out << line << "\n";
        configFile.close();
    }
}

void MainWindow::saveFunction3UrlToConfig(const QString &url)
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
        if (configLines[i].startsWith("function3_url=")) {
            configLines[i] = QString("function3_url=%1").arg(url);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        configLines << QString("function3_url=%1").arg(url);
    }
    if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        for (const QString &line : configLines) out << line << "\n";
        configFile.close();
    }
}

void MainWindow::sendBroadcastNotice(const QString& content, const QStringList &targets)
{
    if (!m_loginWebSocket || m_loginWebSocket->state() != QAbstractSocket::ConnectedState) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("未连接到服务器，无法发布公告"));
        return;
    }

    QJsonObject msg;
    msg["type"] = "broadcast_notice";
    msg["content"] = content;
    msg["sender"] = m_userName.isEmpty() ? m_userId : m_userName;
    msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    
    if (!targets.isEmpty()) {
        QJsonArray targetArray;
        for (const QString &t : targets) {
            targetArray.append(t);
        }
        msg["targets"] = targetArray;
    }
    
    m_loginWebSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void MainWindow::showNoticeToast(const QString& content, const QString& sender, const QString& timeStr)
{
    QWidget *toast = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint);
    toast->setAttribute(Qt::WA_TranslucentBackground);
    toast->setAttribute(Qt::WA_ShowWithoutActivating);

    QWidget *body = new QWidget(toast);
    // Original offline red color
    body->setStyleSheet("background-color: rgba(140, 70, 70, 255); border: none; border-radius: 18px;");
    body->setMinimumSize(420, 96);
    QHBoxLayout *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(20, 18, 20, 18);
    bodyLayout->setSpacing(14);

    QLabel *avatar = new QLabel(body);
    avatar->setFixedSize(56, 56);
    
    QString appDir = QCoreApplication::applicationDirPath();
    QPixmap pix(appDir + "/maps/logo/log.png");
    if (pix.isNull()) {
        pix = QPixmap(56, 56);
        pix.fill(Qt::transparent);
    } else {
        pix = pix.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    avatar->setPixmap(pix);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet("background: transparent;");
    bodyLayout->addWidget(avatar);

    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setSpacing(4);
    
    QLabel *titleLabel = new QLabel(QStringLiteral("%1  %2").arg(sender, timeStr), body);
    titleLabel->setStyleSheet("color: #e0e0e0; font-size: 12px; font-weight: normal; background: transparent;");
    textLayout->addWidget(titleLabel);

    QLabel *contentLabel = new QLabel(content, body);
    contentLabel->setStyleSheet("color: #ffffff; font-size: 16px; font-weight: 800; background: transparent;");
    contentLabel->setWordWrap(true);
    textLayout->addWidget(contentLabel);
    
    bodyLayout->addLayout(textLayout, 1);

    QPushButton *closeBtn = new QPushButton("×", body);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton { color: #ccc; background: transparent; border: none; font-size: 20px; font-weight: bold; margin-top: -10px; }"
        "QPushButton:hover { color: #fff; }"
    );
    connect(closeBtn, &QPushButton::clicked, toast, &QWidget::deleteLater);
    
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->addWidget(closeBtn);

    QPushButton *addTaskBtn = new QPushButton("加入到任务", body);
    addTaskBtn->setCursor(Qt::PointingHandCursor);
    addTaskBtn->setToolTip("加入到任务列表");
    addTaskBtn->setStyleSheet(
        "QPushButton { color: #ccc; background: transparent; border: 1px solid #ccc; border-radius: 4px; padding: 2px 6px; font-size: 12px; font-weight: bold; margin-top: 10px; }"
        "QPushButton:hover { color: #fff; border-color: #fff; background-color: rgba(255,255,255,0.1); }"
    );
    connect(addTaskBtn, &QPushButton::clicked, [this, content, toast]() {
        DateTimePickerDialog dlg(content, toast);
        dlg.setWindowFlags(dlg.windowFlags() | Qt::WindowStaysOnTopHint);
        if (dlg.exec() == QDialog::Accepted) {
            TaskManager::instance().addTask(dlg.taskContent(), dlg.selectedDateTime());
        }
    });
    rightLayout->addWidget(addTaskBtn);

    rightLayout->addStretch();
    bodyLayout->addLayout(rightLayout);

    QVBoxLayout *root = new QVBoxLayout(toast);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(body);

    toast->adjustSize();
    body->adjustSize();

    m_onlineToasts.append(toast);

    connect(toast, &QObject::destroyed, this, [this, toast]() {
        m_onlineToasts.removeAll(toast);
        repositionOnlineToasts();
    });

    repositionOnlineToasts();
    toast->show();
    toast->raise();
}
v o i d   M a i n W i n d o w : : o n C l o s e R o o m R e q u e s t e d ( )   { } 
 
 v o i d   M a i n W i n d o w : : o n K i c k V i e w e r R e q u e s t e d ( c o n s t   Q S t r i n g   & v i e w e r I d )   {   Q _ U N U S E D ( v i e w e r I d ) ;   } 
 
 
