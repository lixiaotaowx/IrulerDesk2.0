#include "MainWindow.h"
#include "VideoWindow.h"
#include "ui/TransparentImageList.h"
#include "video_components/VideoDisplayWidget.h"
#include "ui/AvatarSettingsWindow.h"
#include "ui/SystemSettingsWindow.h"
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
{
    
    
    // 初始化随机数种子
    srand(static_cast<unsigned int>(time(nullptr)));
    
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
    QTimer::singleShot(1000, this, &MainWindow::startStreaming);
}

TransparentImageList* MainWindow::transparentImageList() const
{
    return m_transparentImageList;
}

void MainWindow::sendWatchRequest(const QString& targetDeviceId)
{
    if (!m_loginWebSocket || m_loginWebSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    // 记录当前正在观看的目标设备ID，便于在源切换后重发
    m_currentTargetId = targetDeviceId;
    
    // 构建观看请求消息
    QJsonObject watchRequest;
    watchRequest["type"] = "watch_request";
    watchRequest["viewer_id"] = getDeviceId();
    watchRequest["target_id"] = targetDeviceId;
    watchRequest["viewer_name"] = m_userName;
    watchRequest["viewer_icon_id"] = loadOrGenerateIconId();
    
    QJsonDocument doc(watchRequest);
    QString message = doc.toJson(QJsonDocument::Compact);
    m_loginWebSocket->sendTextMessage(message);
    
    // 直接在主窗口的VideoDisplayWidget中开始接收视频流
    startVideoReceiving(targetDeviceId);
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
    bool spkEnabled = loadSpeakerEnabledFromConfig();
    qInfo() << "config.load.speaker_enabled" << spkEnabled;
    bool micEnabled = true;
    m_videoWindow->setSpeakerChecked(spkEnabled);
    m_videoWindow->setMicChecked(micEnabled);
    videoWidget->setSpeakerEnabled(spkEnabled);
    videoWidget->setTalkEnabled(micEnabled);
    videoWidget->setMicSendEnabled(micEnabled);

    
    QString viewerId = getDeviceId();
    videoWidget->sendWatchRequest(viewerId, targetDeviceId);
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
            vd->sendAudioToggle(micEnabled);
        }
    }
    
    // 创建透明图片列表
    m_transparentImageList = new TransparentImageList();
    QString appDir = QCoreApplication::applicationDirPath();
    m_transparentImageList->setDefaultAvatarPath(QString("%1/maps/ii.png").arg(appDir));
    
    // 设置当前用户信息到透明图片列表
    m_transparentImageList->setCurrentUserInfo(getDeviceId(), loadOrGenerateIconId());
    
    // 连接透明图片列表的点击信号
    connect(m_transparentImageList, &TransparentImageList::userImageClicked,
            this, &MainWindow::onUserImageClicked);
    
    // 连接透明图片列表的显示主列表信号
    connect(m_transparentImageList, &TransparentImageList::showMainListRequested,
            this, &MainWindow::showMainList);
    
    // 连接透明图片列表的设置头像信号
    connect(m_transparentImageList, &TransparentImageList::setAvatarRequested,
            this, &MainWindow::onSetAvatarRequested);
    // 连接透明图片列表的系统设置信号
    connect(m_transparentImageList, &TransparentImageList::systemSettingsRequested,
            this, &MainWindow::onSystemSettingsRequested);
    // 新增：连接透明图片列表的清理标记与退出信号
    connect(m_transparentImageList, &TransparentImageList::clearMarksRequested,
            this, &MainWindow::onClearMarksRequested);
    connect(m_transparentImageList, &TransparentImageList::exitRequested,
            this, &MainWindow::onExitRequested);
    connect(m_transparentImageList, &TransparentImageList::hideRequested,
            this, &MainWindow::onHideRequested);
    connect(m_transparentImageList, &TransparentImageList::micToggleRequested,
            this, &MainWindow::onMicToggleRequested);
    connect(m_transparentImageList, &TransparentImageList::speakerToggleRequested,
            this, &MainWindow::onSpeakerToggleRequested);
    
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
}

void MainWindow::stopStreaming()
{
    if (!m_isStreaming) {
        return;
    }
    
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
    
    m_captureProcess->start(captureExe);
    
    // 异步启动捕获进程，不等待启动完成
    
}

void MainWindow::stopProcesses()
{
    
    // 停止捕获进程
    if (m_captureProcess) {
        m_captureProcess->terminate();
        if (!m_captureProcess->waitForFinished(3000)) {
            m_captureProcess->kill();
        }
        m_captureProcess->deleteLater();
        m_captureProcess = nullptr;
    }
    
    // 停止播放进程
    if (m_playerProcess) {
        m_playerProcess->terminate();
        if (!m_playerProcess->waitForFinished(3000)) {
            m_playerProcess->kill();
        }
        m_playerProcess->deleteLater();
        m_playerProcess = nullptr;
    }
}

void MainWindow::onCaptureProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    
    
    // 输出已通过 ForwardedChannels 直接转发到控制台，这里无需读取缓冲
    
    if (m_isStreaming) {
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
    int newRandomId = 10000 + (rand() % 90000); // 生成10000-99999之间的随机数
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
    int newIconId = 3 + (rand() % 19); // 生成3-21之间的随机数
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
    // 获取用户ID和名称
    m_userId = getDeviceId();
    {
        QString name = loadUserNameFromConfig();
        if (name.isEmpty()) {
            name = QString("用户%1").arg(m_userId);
            saveUserNameToConfig(name);
        }
        m_userName = name;
    }
    
    // 创建WebSocket连接
    m_loginWebSocket = new QWebSocket();
    
    // 连接信号槽
    connect(m_loginWebSocket, &QWebSocket::connected, this, &MainWindow::onLoginWebSocketConnected);
    connect(m_loginWebSocket, &QWebSocket::disconnected, this, &MainWindow::onLoginWebSocketDisconnected);
    connect(m_loginWebSocket, &QWebSocket::textMessageReceived, this, &MainWindow::onLoginWebSocketTextMessageReceived);
    connect(m_loginWebSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), 
            this, &MainWindow::onLoginWebSocketError);
    
    // 延迟3秒后连接到登录服务器，等待服务器启动
    QTimer::singleShot(3000, this, &MainWindow::connectToLoginServer);
}

void MainWindow::connectToLoginServer()
{
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

void MainWindow::updateUserList(const QJsonArray& users)
{
    
    // 清空现有列表
    m_listWidget->clear();
    
    // 准备透明图片列表的用户ID列表
    QStringList onlineUserIds;
    
    if (users.isEmpty()) {
        m_listWidget->addItem("暂无在线用户");
        // 清空透明图片列表
        m_transparentImageList->updateUserList(onlineUserIds);
        return;
    }
    
    // 添加在线用户到列表
    for (int i = 0; i < users.size(); ++i) {
        const QJsonValue& userValue = users[i];
        
        if (!userValue.isObject()) {
            // 非对象数据，跳过
            continue;
        }
        
        QJsonObject userObj = userValue.toObject();
        QString userId = userObj["id"].toString();
        QString userName = userObj["name"].toString();
        
        
        QString displayText = QString("%1 (%2)").arg(userName).arg(userId);
        m_listWidget->addItem(displayText);
        
        // 添加所有用户到透明图片列表（包含自己）
        onlineUserIds.append(userId);
        
        // 检查是否是当前用户
        if (userId == m_userId) {
            // 当前用户在列表中
        } else {
            // 其他用户
        }
    }
    
    // 更新透明图片列表 - 使用新的JSON格式
    m_transparentImageList->updateUserList(users);
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

// 登录系统槽函数实现
void MainWindow::onLoginWebSocketConnected()
{
    
    m_listWidget->clear();
    m_listWidget->addItem("已连接服务器，正在登录...");
    
    // 连接成功后立即发送登录请求
    sendLoginRequest();
}

void MainWindow::onLoginWebSocketDisconnected()
{
    m_isLoggedIn = false;
    
    m_listWidget->clear();
    m_listWidget->addItem("与服务器断开连接");
    
    // 5秒后尝试重新连接
    QTimer::singleShot(5000, this, &MainWindow::connectToLoginServer);
}

void MainWindow::onLoginWebSocketTextMessageReceived(const QString &message)
{
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    
    if (error.error != QJsonParseError::NoError) {
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
        // 处理推流请求
        QString viewerId = obj["viewer_id"].toString();
        QString targetId = obj["target_id"].toString();
        
        // 开始推流
        if (!m_isStreaming) {
            startStreaming();
        }
        
        // 发送推流OK响应给观看者
        QJsonObject streamOkResponse;
        streamOkResponse["type"] = "streaming_ok";
        streamOkResponse["viewer_id"] = viewerId;
        streamOkResponse["target_id"] = targetId;
        streamOkResponse["stream_url"] = QString("ws://%1/subscribe/%2").arg(getServerAddress(), targetId);
        
        // 查找观看者的连接并发送响应
        // 注意：这里需要通过服务器转发，因为我们不直接连接到观看者
        // 服务器会处理这个响应的转发
        if (m_loginWebSocket && m_loginWebSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonDocument responseDoc(streamOkResponse);
            m_loginWebSocket->sendTextMessage(responseDoc.toJson(QJsonDocument::Compact));
        }
    } else if (type == "streaming_ok") {
        // 处理推流OK响应，开始拉流播放
        QString viewerId = obj["viewer_id"].toString();
        QString targetId = obj["target_id"].toString();
        QString streamUrl = obj["stream_url"].toString();
        
        // 检查是否是当前用户的观看请求
        if (viewerId == getDeviceId()) {
            
            // 开始视频接收和播放
            startVideoReceiving(targetId);
        } else {
            // 非当前用户的观看请求，忽略
        }
    } else if (type == "avatar_update" || type == "avatar_updated" || type == "user_icon_update") {
        QString userId;
        if (obj.contains("device_id")) userId = obj.value("device_id").toString();
        else if (obj.contains("id")) userId = obj.value("id").toString();
        else if (obj.contains("user_id")) userId = obj.value("user_id").toString();
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
    
    // 10秒后尝试重新连接
    QTimer::singleShot(10000, this, &MainWindow::connectToLoginServer);
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
    if (m_videoWindow) {
        m_videoWindow->show();
        m_videoWindow->raise();
        m_videoWindow->activateWindow();
    }
    
    // 发送观看请求
    sendWatchRequest(userId);
    
    // 启动视频接收
    startVideoReceiving(userId);
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
    QString currentUserId = m_transparentImageList ? m_transparentImageList->getCurrentUserId() : QString();
    if (!currentUserId.isEmpty()) {
        startVideoReceiving(currentUserId);
    } else {
    }
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
    }
    m_systemSettingsWindow->show();
}

void MainWindow::onClearMarksRequested()
{
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
        m_videoWindow->setMicChecked(enabled);
        auto *vd = m_videoWindow->getVideoDisplayWidget();
        if (vd) {
            vd->setTalkEnabled(enabled);
            vd->setMicSendEnabled(enabled);
        }
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

void MainWindow::onScreenSelected(int index)
{

    // 改为热切换：通过播放器端WebSocket发送按索引切屏消息，不修改配置、不重启采集进程
    if (m_videoWindow) {
        auto *videoWidget = m_videoWindow->getVideoDisplayWidget();
        if (videoWidget) {
            // 视频窗口提示“切换中...”，并发送索引切换请求（会话不断流）
            videoWidget->sendSwitchScreenIndex(index);
        }
    }

    // 等待首帧到达后通知系统设置窗口关闭（沿用原有首帧确认逻辑）
    if (m_videoWindow) {
        auto *videoWidget = m_videoWindow->getVideoDisplayWidget();
        if (videoWidget) {
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
