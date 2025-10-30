#include "MainWindow.h"
#include "VideoWindow.h"
#include "ui/TransparentImageList.h"
#include "ui/AvatarSettingsWindow.h"
#include <QApplication>
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
    , m_statusLabel(nullptr)
    , m_isStreaming(false)
    , m_captureProcess(nullptr)
    , m_playerProcess(nullptr)
    , m_serverReadyTimer(nullptr)
    , m_serverReadyRetryCount(0)
    , m_loginWebSocket(nullptr)
    , m_isLoggedIn(false)
{
    qDebug() << "[MainWindow] ========== 主窗口构造开始 ==========";
    
    // 初始化随机数种子
    srand(static_cast<unsigned int>(time(nullptr)));
    
    qDebug() << "[MainWindow] 设置UI界面...";
    setupUI();
    qDebug() << "[MainWindow] UI界面设置完成";
    
    qDebug() << "[MainWindow] 设置状态栏...";
    setupStatusBar();
    qDebug() << "[MainWindow] 状态栏设置完成";
    
    // 初始化登录系统
    qDebug() << "[MainWindow] 初始化登录系统...";
    initializeLoginSystem();
    qDebug() << "[MainWindow] 登录系统初始化完成";
    
    // 自动开始流媒体传输
    qDebug() << "[MainWindow] 设置1秒后自动启动流媒体...";
    QTimer::singleShot(1000, this, &MainWindow::startStreaming);
    
    qDebug() << "[MainWindow] ========== 主窗口构造完成 ==========";
}

void MainWindow::sendWatchRequest(const QString& targetDeviceId)
{
    if (!m_loginWebSocket || m_loginWebSocket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << "[WatchRequest] 登录WebSocket未连接，无法发送观看请求";
        return;
    }
    
    // 构建观看请求消息
    QJsonObject watchRequest;
    watchRequest["type"] = "watch_request";
    watchRequest["viewer_id"] = getDeviceId();
    watchRequest["target_id"] = targetDeviceId;
    watchRequest["viewer_icon_id"] = loadOrGenerateIconId();  // 添加观看者的icon ID
    
    QJsonDocument doc(watchRequest);
    QString message = doc.toJson(QJsonDocument::Compact);
    
    qDebug() << "[WatchRequest] 发送观看请求:" << message;
    m_loginWebSocket->sendTextMessage(message);
    
    // 直接在主窗口的VideoDisplayWidget中开始接收视频流
    startVideoReceiving(targetDeviceId);
}

void MainWindow::startVideoReceiving(const QString& targetDeviceId)
{
    if (!m_videoWindow) {
        qDebug() << "[VideoReceiving] VideoWindow未初始化";
        return;
    }
    
    VideoDisplayWidget* videoWidget = m_videoWindow->getVideoDisplayWidget();
    if (!videoWidget) {
        qDebug() << "[VideoReceiving] VideoDisplayWidget未初始化";
        return;
    }
    
    // 构建WebSocket连接URL，包含目标设备ID
    QString serverAddress = getServerAddress();
    QString serverUrl = QString("ws://%1/subscribe/%2").arg(serverAddress, targetDeviceId);
    
    qDebug() << "[VideoReceiving] 开始在视频窗口接收视频流，目标设备ID:" << targetDeviceId;
    qDebug() << "[VideoReceiving] 连接URL:" << serverUrl;
    
    // 使用VideoDisplayWidget开始接收视频流
    videoWidget->startReceiving(serverUrl);
    
    // 注意：不再通过VideoDisplayWidget发送watch_request
    // watch_request已经通过登录通道发送，避免重复发送
}

void MainWindow::startPlayerProcess(const QString& targetDeviceId)
{
    // 如果播放进程已经在运行，先停止它
    if (m_playerProcess && m_playerProcess->state() != QProcess::NotRunning) {
        qDebug() << "[PlayerProcess] 停止现有播放进程";
        m_playerProcess->kill();
        m_playerProcess->waitForFinished(3000);
    }
    
    if (!m_playerProcess) {
        m_playerProcess = new QProcess(this);
        connect(m_playerProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &MainWindow::onPlayerProcessFinished);
    }
    
    QString playerPath = QCoreApplication::applicationDirPath() + "/PlayerProcess.exe";
    QStringList arguments;
    arguments << targetDeviceId;  // 传递目标设备ID作为参数
    
    qDebug() << "[PlayerProcess] 启动播放进程，目标设备ID:" << targetDeviceId;
    qDebug() << "[PlayerProcess] 执行路径:" << playerPath;
    qDebug() << "[PlayerProcess] 参数:" << arguments;
    
    m_playerProcess->start(playerPath, arguments);
    
    if (!m_playerProcess->waitForStarted(5000)) {
        qDebug() << "[PlayerProcess] 播放进程启动失败:" << m_playerProcess->errorString();
    } else {
        qDebug() << "[PlayerProcess] 播放进程启动成功";
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
    qDebug() << "[MainWindow] 观看按钮被点击";
    
    // 检查是否有选中的用户
    QListWidgetItem *currentItem = m_listWidget->currentItem();
    if (!currentItem) {
        qDebug() << "[MainWindow] 没有选中的用户";
        return;
    }
    
    QString selectedUser = currentItem->text();
    qDebug() << "[MainWindow] 选中的用户:" << selectedUser;
    
    // 从项目文本中提取设备ID (格式: "用户名 (设备ID)")
    QRegularExpression regex("\\(([^)]+)\\)");
    QRegularExpressionMatch match = regex.match(selectedUser);
    if (match.hasMatch()) {
        QString targetDeviceId = match.captured(1);
        qDebug() << "[MainWindow] 提取到目标设备ID:" << targetDeviceId;
        
        // 显示视频窗口
        if (m_videoWindow) {
            m_videoWindow->show();
            m_videoWindow->raise();
            m_videoWindow->activateWindow();
            qDebug() << "[MainWindow] 视频窗口已显示";
        }
        
        // 发送观看请求并开始视频接收
        sendWatchRequest(targetDeviceId);
        
    } else {
        qDebug() << "[MainWindow] 无法从项目文本中提取设备ID:" << selectedUser;
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
    qDebug() << "[MainWindow] 创建列表组件...";
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
        qDebug() << "[MainWindow] 列表选择变化，启用观看按钮:" << enableButton;
    });
    
    // 添加示例项目
    m_listWidget->addItem("等待连接服务器...");
    qDebug() << "[MainWindow] 列表组件项目数量:" << m_listWidget->count();
    
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
    qDebug() << "[MainWindow] 创建随机ID标签...";
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
    qDebug() << "[MainWindow] 随机ID标签创建完成:" << idText;
    
    m_mainLayout->addWidget(m_idLabel);
    
    qDebug() << "[MainWindow] UI组件已添加到布局";
    
    // 创建视频窗口（但不显示）
    m_videoWindow = new VideoWindow();
    
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
    
    qDebug() << "[MainWindow] ========== UI设置完成 ==========";
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
    qDebug() << "[MainWindow] startStreaming() 被调用";
    
    if (m_isStreaming) {
        qDebug() << "[MainWindow] 流媒体已在运行，跳过启动";
        return;
    }
    
    qDebug() << "[MainWindow] 开始启动流媒体系统";
    
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
    qDebug() << "[MainWindow] 流媒体启动完成，状态设为运行中";
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
    qDebug() << "[MainWindow] ========== 启动时间诊断开始 ==========";
    qDebug() << "[MainWindow] startProcesses() 开始启动积木式组件";
    
    QString appDir = QApplication::applicationDirPath();
    qDebug() << "[MainWindow] 应用程序目录:" << appDir;
    qDebug() << "[MainWindow] [诊断] 初始化耗时:" << m_startupTimer.elapsed() << "ms";
    
    // 直接启动捕获进程，连接到腾讯云服务器
    qDebug() << "[MainWindow] 启动屏幕捕获进程（连接到腾讯云服务器）";
    qDebug() << "[MainWindow] [诊断] 开始创建捕获进程，当前耗时:" << m_startupTimer.elapsed() << "ms";
    
    m_captureProcess = new QProcess(this);
    connect(m_captureProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onCaptureProcessFinished);
    
    QString captureExe = appDir + "/CaptureProcess.exe";
    qDebug() << "[MainWindow] 捕获进程路径:" << captureExe;
    qDebug() << "[MainWindow] [诊断] 进程对象创建完成，耗时:" << m_startupTimer.elapsed() << "ms";
    
    if (!QFile::exists(captureExe)) {
        qCritical() << "[MainWindow] 捕获进程文件不存在:" << captureExe;
        QMessageBox::warning(this, "错误", "捕获进程文件不存在: " + captureExe);
        return;
    }
    
    qDebug() << "[MainWindow] [诊断] 开始启动捕获进程，耗时:" << m_startupTimer.elapsed() << "ms";
    m_captureProcess->start(captureExe);
    
    // 异步启动捕获进程，不等待启动完成
    qDebug() << "[MainWindow] [超快启动] 异步启动捕获进程，不等待启动完成";
    qDebug() << "[MainWindow] [诊断] 捕获进程异步启动完成，耗时:" << m_startupTimer.elapsed() << "ms";
    
    qDebug() << "[MainWindow] 积木式组件启动完成";
    qDebug() << "[MainWindow] ========== 启动时间诊断结束，总耗时:" << m_startupTimer.elapsed() << "ms ==========";
}

void MainWindow::stopProcesses()
{
    qDebug() << "[MainWindow] 停止所有进程";
    
    // 停止捕获进程
    if (m_captureProcess) {
        qDebug() << "[MainWindow] 停止捕获进程";
        m_captureProcess->terminate();
        if (!m_captureProcess->waitForFinished(3000)) {
            m_captureProcess->kill();
        }
        m_captureProcess->deleteLater();
        m_captureProcess = nullptr;
    }
    
    // 停止播放进程
    if (m_playerProcess) {
        qDebug() << "[MainWindow] 停止播放进程";
        m_playerProcess->terminate();
        if (!m_playerProcess->waitForFinished(3000)) {
            m_playerProcess->kill();
        }
        m_playerProcess->deleteLater();
        m_playerProcess = nullptr;
    }
    
    qDebug() << "[MainWindow] 所有进程已停止";
}

void MainWindow::onCaptureProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug() << "[MainWindow] 捕获进程退出，退出码:" << exitCode << "状态:" << (exitStatus == QProcess::NormalExit ? "正常退出" : "崩溃退出");
    
    if (m_captureProcess) {
        qDebug() << "[MainWindow] 捕获进程错误信息:" << m_captureProcess->errorString();
        QString output = m_captureProcess->readAllStandardOutput();
        QString error = m_captureProcess->readAllStandardError();
        if (!output.isEmpty()) {
            qDebug() << "[MainWindow] 捕获进程标准输出:" << output;
        }
        if (!error.isEmpty()) {
            qDebug() << "[MainWindow] 捕获进程错误输出:" << error;
        }
    }
    
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
    qDebug() << "[MainWindow] 播放进程退出，退出码:" << exitCode << "状态:" << (exitStatus == QProcess::NormalExit ? "正常退出" : "崩溃退出");
    
    if (m_playerProcess) {
        qDebug() << "[MainWindow] 播放进程错误信息:" << m_playerProcess->errorString();
        QString output = m_playerProcess->readAllStandardOutput();
        QString error = m_playerProcess->readAllStandardError();
        if (!output.isEmpty()) {
            qDebug() << "[MainWindow] 播放进程标准输出:" << output;
        }
        if (!error.isEmpty()) {
            qDebug() << "[MainWindow] 播放进程错误输出:" << error;
        }
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
            qDebug() << "[MainWindow] 创建配置文件夹:" << configDir;
        }
        
        // 缓存配置文件完整路径
        cachedConfigFilePath = configDir + "/app_config.txt";
        qDebug() << "[MainWindow] 配置文件路径:" << cachedConfigFilePath;
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
                    qDebug() << "[MainWindow] 从配置文件读取已保存的随机ID:" << existingId;
                    return existingId;
                }
            }
        }
        configFile.close();
    }
    
    // 如果文件不存在或读取失败，生成新的随机ID
    int newRandomId = 10000 + (rand() % 90000); // 生成10000-99999之间的随机数
    saveRandomIdToConfig(newRandomId);
    qDebug() << "[MainWindow] 生成新的随机ID并保存到配置文件:" << newRandomId;
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
                    qDebug() << "[MainWindow] 从配置文件读取已保存的icon ID:" << existingIconId;
                    return existingIconId;
                }
            }
        }
        configFile.close();
    }
    
    // 如果文件不存在或读取失败，生成新的icon ID
    int newIconId = 3 + (rand() % 19); // 生成3-21之间的随机数
    saveIconIdToConfig(newIconId);
    qDebug() << "[MainWindow] 生成新的icon ID并保存到配置文件:" << newIconId;
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
        qDebug() << "[MainWindow] 随机ID已保存到配置文件:" << randomId;
    } else {
        qWarning() << "[MainWindow] 无法写入配置文件:" << configFilePath;
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
        qDebug() << "[MainWindow] icon ID已保存到配置文件:" << iconId;
    } else {
        qWarning() << "[MainWindow] 无法写入配置文件:" << configFilePath;
    }
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
                        qDebug() << "[MainWindow] 从配置文件读取设备ID(random_id):" << cachedDeviceId;
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
        qDebug() << "[MainWindow] 使用生成的random_id作为设备ID:" << cachedDeviceId;
    }
    
    return cachedDeviceId;
}

QString MainWindow::generateUniqueDeviceId() const
{
    // 生成5位数字ID，与random_id保持一致的格式
    srand(static_cast<unsigned int>(QDateTime::currentMSecsSinceEpoch()));
    int deviceId = 10000 + (rand() % 90000); // 生成10000-99999之间的随机数
    
    qDebug() << "[MainWindow] 生成新的设备ID:" << deviceId;
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
        qDebug() << "[MainWindow] 设备ID已保存到配置文件:" << deviceId;
    } else {
        qWarning() << "[MainWindow] 无法写入配置文件:" << configFilePath;
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
                    qDebug() << "[MainWindow] 从配置文件读取服务器地址:" << serverAddress;
                    cachedServerAddress = serverAddress;
                    initialized = true;
                    return cachedServerAddress;
                }
            }
            configFile.close();
        }
        
        // 如果读取失败，返回默认的腾讯云地址
        qDebug() << "[MainWindow] 未找到服务器地址配置，使用默认 123.207.222.92:8765";
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
        qDebug() << "[MainWindow] 服务器地址已保存到配置文件:" << serverAddress;
    } else {
        qWarning() << "[MainWindow] 无法写入配置文件:" << configFilePath;
    }
}

// 登录系统相关方法实现
void MainWindow::initializeLoginSystem()
{
    // 获取用户ID和名称
    m_userId = getDeviceId();
    m_userName = QString("用户%1").arg(m_userId);
    
    qDebug() << "[LoginSystem] 初始化登录系统，用户ID:" << m_userId << "用户名:" << m_userName;
    
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
    qDebug() << "[LoginSystem] 连接到登录服务器:" << serverUrl;
    qDebug() << "[LoginSystem] 当前WebSocket状态:" << m_loginWebSocket->state();
    qDebug() << "[LoginSystem] 本地用户ID:" << m_userId << "用户名:" << m_userName;
    m_loginWebSocket->open(QUrl(serverUrl));
}

void MainWindow::sendLoginRequest()
{
    if (!m_loginWebSocket || m_loginWebSocket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "[LoginSystem] WebSocket未连接，无法发送登录请求，当前状态:" << (m_loginWebSocket ? m_loginWebSocket->state() : -1);
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
    
    qDebug() << "[LoginSystem] 发送登录请求:" << message;
    qDebug() << "[LoginSystem] 登录数据 - ID:" << m_userId << "名称:" << m_userName
             << "Icon ID:" << loadOrGenerateIconId() << "Viewer Icon ID:" << loadOrGenerateIconId();
    m_loginWebSocket->sendTextMessage(message);
}

void MainWindow::updateUserList(const QJsonArray& users)
{
    qDebug() << "[LoginSystem] ========== 开始更新用户列表 ==========";
    qDebug() << "[LoginSystem] 收到用户数组，用户数量:" << users.size();
    qDebug() << "[LoginSystem] 用户数组内容:" << QJsonDocument(users).toJson(QJsonDocument::Compact);
    
    // 清空现有列表
    m_listWidget->clear();
    
    // 准备透明图片列表的用户ID列表
    QStringList onlineUserIds;
    
    if (users.isEmpty()) {
        qDebug() << "[LoginSystem] 用户列表为空，显示默认消息";
        m_listWidget->addItem("暂无在线用户");
        // 清空透明图片列表
        m_transparentImageList->updateUserList(onlineUserIds);
        return;
    }
    
    // 添加在线用户到列表
    for (int i = 0; i < users.size(); ++i) {
        const QJsonValue& userValue = users[i];
        qDebug() << "[LoginSystem] 处理用户" << i << ":" << userValue;
        
        if (!userValue.isObject()) {
            qWarning() << "[LoginSystem] 用户数据不是对象，跳过:" << userValue;
            continue;
        }
        
        QJsonObject userObj = userValue.toObject();
        QString userId = userObj["id"].toString();
        QString userName = userObj["name"].toString();
        
        qDebug() << "[LoginSystem] 解析用户数据 - ID:" << userId << "名称:" << userName;
        
        QString displayText = QString("%1 (%2)").arg(userName).arg(userId);
        m_listWidget->addItem(displayText);
        
        qDebug() << "[LoginSystem] 添加用户到UI列表:" << displayText;
        
        // 添加所有用户到透明图片列表（包含自己）
        onlineUserIds.append(userId);
        
        // 检查是否是当前用户
        if (userId == m_userId) {
            qDebug() << "[LoginSystem] 发现当前用户在列表中:" << userId;
        } else {
            qDebug() << "[LoginSystem] 发现其他用户:" << userId << "（当前用户:" << m_userId << "）";
        }
    }
    
    // 更新透明图片列表 - 使用新的JSON格式
    m_transparentImageList->updateUserList(users);
    
    qDebug() << "[LoginSystem] 用户列表更新完成，UI显示" << users.size() << "个在线用户";
    qDebug() << "[LoginSystem] 透明图片列表显示" << onlineUserIds.size() << "个用户（包含自己）";
    qDebug() << "[LoginSystem] ========== 用户列表更新结束 ==========";
}

// 登录系统槽函数实现
void MainWindow::onLoginWebSocketConnected()
{
    qDebug() << "[LoginSystem] ========== WebSocket连接成功 ==========";
    qDebug() << "[LoginSystem] 连接状态:" << m_loginWebSocket->state();
    qDebug() << "[LoginSystem] 服务器地址:" << m_loginWebSocket->requestUrl().toString();
    qDebug() << "[LoginSystem] 本地地址:" << m_loginWebSocket->localAddress().toString() << ":" << m_loginWebSocket->localPort();
    qDebug() << "[LoginSystem] 对端地址:" << m_loginWebSocket->peerAddress().toString() << ":" << m_loginWebSocket->peerPort();
    
    m_listWidget->clear();
    m_listWidget->addItem("已连接服务器，正在登录...");
    
    // 连接成功后立即发送登录请求
    sendLoginRequest();
}

void MainWindow::onLoginWebSocketDisconnected()
{
    qDebug() << "[LoginSystem] ========== WebSocket连接断开 ==========";
    qDebug() << "[LoginSystem] 断开原因:" << m_loginWebSocket->closeReason();
    qDebug() << "[LoginSystem] 断开代码:" << m_loginWebSocket->closeCode();
    m_isLoggedIn = false;
    
    m_listWidget->clear();
    m_listWidget->addItem("与服务器断开连接");
    
    // 5秒后尝试重新连接
    qDebug() << "[LoginSystem] 5秒后尝试重新连接";
    QTimer::singleShot(5000, this, &MainWindow::connectToLoginServer);
}

void MainWindow::onLoginWebSocketTextMessageReceived(const QString &message)
{
    qDebug() << "[LoginSystem] ========== 收到服务器消息 ==========";
    qDebug() << "[LoginSystem] 消息长度:" << message.length();
    qDebug() << "[LoginSystem] 消息内容:" << message;
    qDebug() << "[LoginSystem] 当前登录状态:" << (m_isLoggedIn ? "已登录" : "未登录");
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "[LoginSystem] JSON解析错误:" << error.errorString();
        qWarning() << "[LoginSystem] 错误位置:" << error.offset;
        qWarning() << "[LoginSystem] 原始消息:" << message;
        return;
    }
    
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();
    
    qDebug() << "[LoginSystem] 解析成功，消息类型:" << type;
    qDebug() << "[LoginSystem] 消息对象键:" << obj.keys();
    
    if (type == "login_response") {
        qDebug() << "[LoginSystem] 处理登录响应消息";
        bool success = obj["success"].toBool();
        QString responseMessage = obj["message"].toString();
        
        qDebug() << "[LoginSystem] 登录结果:" << (success ? "成功" : "失败");
        qDebug() << "[LoginSystem] 服务器消息:" << responseMessage;
        
        if (success) {
            qDebug() << "[LoginSystem] 登录成功，更新状态";
            m_isLoggedIn = true;
            m_listWidget->clear();
            m_listWidget->addItem("登录成功，等待用户列表...");
        } else {
            qWarning() << "[LoginSystem] 登录失败:" << responseMessage;
            m_listWidget->clear();
            m_listWidget->addItem("登录失败: " + responseMessage);
        }
    } else if (type == "online_users_update" || type == "online_users") {
        qDebug() << "[LoginSystem] ========== 处理用户列表更新消息 ==========";
        qDebug() << "[LoginSystem] 消息类型:" << type;
        qDebug() << "[LoginSystem] 当前时间:" << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
        qDebug() << "[LoginSystem] 本地用户ID:" << getDeviceId();
        qDebug() << "[LoginSystem] WebSocket连接状态:" << (m_loginWebSocket ? (m_loginWebSocket->state() == QAbstractSocket::ConnectedState ? "已连接" : "未连接") : "空指针");
        qDebug() << "[LoginSystem] 本地地址:" << (m_loginWebSocket ? m_loginWebSocket->localAddress().toString() + ":" + QString::number(m_loginWebSocket->localPort()) : "N/A");
        qDebug() << "[LoginSystem] 服务器地址:" << (m_loginWebSocket ? m_loginWebSocket->peerAddress().toString() + ":" + QString::number(m_loginWebSocket->peerPort()) : "N/A");
        
        if (!obj.contains("data")) {
            qWarning() << "[LoginSystem] 错误: 消息缺少data字段";
            qWarning() << "[LoginSystem] 消息对象键列表:" << obj.keys();
            qWarning() << "[LoginSystem] 完整消息对象:" << obj;
            return;
        }
        
        QJsonValue dataValue = obj["data"];
        qDebug() << "[LoginSystem] data字段类型:" << (dataValue.isArray() ? "数组" : dataValue.isObject() ? "对象" : dataValue.isNull() ? "空值" : "其他");
        qDebug() << "[LoginSystem] data字段原始内容:" << QJsonDocument::fromVariant(dataValue.toVariant()).toJson(QJsonDocument::Compact);
        
        if (!dataValue.isArray()) {
            qWarning() << "[LoginSystem] 错误: data字段不是数组";
            qWarning() << "[LoginSystem] data字段实际类型:" << dataValue.type();
            qWarning() << "[LoginSystem] data字段值:" << dataValue;
            return;
        }
        
        QJsonArray users = dataValue.toArray();
        qDebug() << "[LoginSystem] 成功解析用户数组，用户数量:" << users.size();
        
        // 详细记录每个用户信息
        for (int i = 0; i < users.size(); ++i) {
            const QJsonValue& userValue = users[i];
            qDebug() << "[LoginSystem] 用户" << i << "原始数据:" << userValue;
            
            if (userValue.isObject()) {
                QJsonObject userObj = userValue.toObject();
                QString userId = userObj["id"].toString();
                QString userName = userObj["name"].toString();
                qDebug() << "[LoginSystem] 用户" << i << "解析结果 - ID:" << userId << "名称:" << userName;
                qDebug() << "[LoginSystem] 用户" << i << "是否为本地用户:" << (userId == getDeviceId() ? "是" : "否");
            } else {
                qWarning() << "[LoginSystem] 用户" << i << "数据格式错误，不是对象:" << userValue.type();
            }
        }
        
        qDebug() << "[LoginSystem] 准备更新UI用户列表...";
        updateUserList(users);
        qDebug() << "[LoginSystem] ========== 用户列表更新处理完成 ==========";
    } else if (type == "start_streaming_request") {
        // 处理推流请求
        QString viewerId = obj["viewer_id"].toString();
        QString targetId = obj["target_id"].toString();
        
        qDebug() << "[StreamingRequest] 收到推流请求，观看者:" << viewerId << "目标:" << targetId;
        
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
            qDebug() << "[StreamingRequest] 已发送推流OK响应:" << responseDoc.toJson(QJsonDocument::Compact);
        }
    } else if (type == "streaming_ok") {
        // 处理推流OK响应，开始拉流播放
        QString viewerId = obj["viewer_id"].toString();
        QString targetId = obj["target_id"].toString();
        QString streamUrl = obj["stream_url"].toString();
        
        qDebug() << "[StreamingOK] 收到推流OK响应，观看者:" << viewerId << "目标:" << targetId << "流URL:" << streamUrl;
        
        // 检查是否是当前用户的观看请求
        if (viewerId == getDeviceId()) {
            qDebug() << "[StreamingOK] 这是当前用户的观看请求，开始拉流播放";
            
            // 开始视频接收和播放
            startVideoReceiving(targetId);
        } else {
            qDebug() << "[StreamingOK] 这不是当前用户的观看请求，忽略";
        }
    } else {
        qWarning() << "[LoginSystem] 未知消息类型:" << type;
        qDebug() << "[LoginSystem] 完整消息对象:" << obj;
    }
    
    qDebug() << "[LoginSystem] ========== 消息处理完成 ==========";
}

void MainWindow::onLoginWebSocketError(QAbstractSocket::SocketError error)
{
    qWarning() << "[LoginSystem] ========== WebSocket连接错误 ==========";
    qWarning() << "[LoginSystem] 错误代码:" << error;
    qWarning() << "[LoginSystem] 错误描述:" << m_loginWebSocket->errorString();
    qWarning() << "[LoginSystem] 当前连接状态:" << m_loginWebSocket->state();
    qWarning() << "[LoginSystem] 服务器URL:" << m_loginWebSocket->requestUrl().toString();
    qWarning() << "[LoginSystem] 本地地址:" << m_loginWebSocket->localAddress().toString() << ":" << m_loginWebSocket->localPort();
    qWarning() << "[LoginSystem] 对端地址:" << m_loginWebSocket->peerAddress().toString() << ":" << m_loginWebSocket->peerPort();
    
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
    
    qWarning() << "[LoginSystem] 错误详情:" << errorMessage;
    qWarning() << "[LoginSystem] ========== 错误处理完成 ==========";
    
    m_listWidget->clear();
    m_listWidget->addItem("连接服务器失败: " + errorMessage);
    
    // 10秒后尝试重新连接
    qDebug() << "[LoginSystem] 10秒后尝试重新连接";
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
        qDebug() << "[ContextMenu] 观看被点击，但没有选中项目";
        return;
    }
    
    QString itemText = currentItem->text();
    qDebug() << "[ContextMenu] 观看被点击，当前项目:" << itemText;
    
    // 从项目文本中提取设备ID (格式: "用户名 (设备ID)")
    QRegularExpression regex("\\(([^)]+)\\)");
    QRegularExpressionMatch match = regex.match(itemText);
    if (match.hasMatch()) {
        QString targetDeviceId = match.captured(1);
        qDebug() << "[ContextMenu] 提取到目标设备ID:" << targetDeviceId;
        
        // 发送观看请求
        sendWatchRequest(targetDeviceId);
    } else {
        qDebug() << "[ContextMenu] 无法从项目文本中提取设备ID:" << itemText;
    }
}

void MainWindow::onContextMenuOption2()
{
    // 获取当前选中的项目
    QListWidgetItem *currentItem = m_listWidget->currentItem();
    if (currentItem) {
        qDebug() << "[ContextMenu] 选项二被点击，当前项目:" << currentItem->text();
    } else {
        qDebug() << "[ContextMenu] 选项二被点击，但没有选中项目";
    }
}

// 透明图片列表点击事件处理
void MainWindow::onUserImageClicked(const QString &userId, const QString &userName)
{
    qDebug() << "[TransparentImageList] 用户点击了图片，用户ID:" << userId << "用户名:" << userName;
    
    // 显示视频窗口
    if (m_videoWindow) {
        m_videoWindow->show();
        m_videoWindow->raise();
        m_videoWindow->activateWindow();
        qDebug() << "[TransparentImageList] 视频窗口已显示";
    }
    
    // 发送观看请求
    sendWatchRequest(userId);
    
    // 启动视频接收
    startVideoReceiving(userId);
    
    qDebug() << "[TransparentImageList] 已为用户" << userId << "启动视频观看";
}

void MainWindow::showMainList()
{
    // 显示整个主窗口
    this->show();
    this->raise();
    this->activateWindow();
    
    qDebug() << "[MainWindow] 显示主窗口";
}

void MainWindow::onSetAvatarRequested()
{
    qDebug() << "[MainWindow] 收到设置头像请求";
    
    // 如果头像设置窗口不存在，创建它
    if (!m_avatarSettingsWindow) {
        m_avatarSettingsWindow = new AvatarSettingsWindow(this);
        
        // 连接头像选择信号
        connect(m_avatarSettingsWindow, &AvatarSettingsWindow::avatarSelected,
                this, &MainWindow::onAvatarSelected);
    }
    
    // 显示头像设置窗口
    m_avatarSettingsWindow->show();
    m_avatarSettingsWindow->raise();
    m_avatarSettingsWindow->activateWindow();
}

void MainWindow::onAvatarSelected(int iconId)
{
    qDebug() << "[MainWindow] 用户选择了头像ID:" << iconId;
    
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
        qDebug() << "[MainWindow] 已向服务器发送头像更新消息:" << jsonString;
    }
    
    // 4. 关闭头像设置窗口
    if (m_avatarSettingsWindow) {
        m_avatarSettingsWindow->hide();
    }
}