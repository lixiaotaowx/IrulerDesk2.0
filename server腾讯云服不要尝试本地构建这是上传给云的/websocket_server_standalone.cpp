#include <QCoreApplication>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QDebug>
#include <QCommandLineParser>
#include <QHostAddress>
#include <QTimer>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

// 用户信息结构
struct UserInfo {
    QString id;
    QString name;
    int iconId;  // 添加icon ID字段
    QWebSocket* socket;
    QDateTime loginTime;
    qint64 lastHeartbeatMs = 0;
    
    UserInfo() : socket(nullptr), iconId(3) {}
    UserInfo(const QString& userId, const QString& userName, int userIconId, QWebSocket* userSocket)
        : id(userId), name(userName), iconId(userIconId), socket(userSocket), loginTime(QDateTime::currentDateTime()) {}
};

class WebSocketServerApp : public QObject
{
    Q_OBJECT

public:
    WebSocketServerApp(int port = 8765, QObject *parent = nullptr) : QObject(parent), m_port(port)
    {
        m_server = new QWebSocketServer(QStringLiteral("Screen Stream Server"), 
                                       QWebSocketServer::NonSecureMode, this);
        
        // 初始化用户数据文件路径
        initializeUserDataPath();
        
        if (m_server->listen(QHostAddress::Any, port)) {
            qDebug() << QDateTime::currentDateTime().toString() 
                     << "WebSocket服务器启动成功，监听端口:" << port;
            connect(m_server, &QWebSocketServer::newConnection,
                    this, &WebSocketServerApp::onNewConnection);
            
            // 定时输出统计信息
            QTimer *statsTimer = new QTimer(this);
            connect(statsTimer, &QTimer::timeout, this, &WebSocketServerApp::printStats);
            statsTimer->start(30000); // 每30秒输出一次统计
            m_heartbeatTimer = new QTimer(this);
            connect(m_heartbeatTimer, &QTimer::timeout, this, &WebSocketServerApp::checkHeartbeatTimeouts);
            m_heartbeatTimer->start(5000);
        } else {
            qDebug() << QDateTime::currentDateTime().toString()
                     << "WebSocket服务器启动失败:" << m_server->errorString();
        }
    }
    
private slots:
    void onNewConnection()
    {
        QWebSocket *socket = m_server->nextPendingConnection();
        QString clientInfo = QString("%1:%2").arg(socket->peerAddress().toString())
                                             .arg(socket->peerPort());
        
        qDebug() << QDateTime::currentDateTime().toString() 
                 << "新客户端连接:" << clientInfo;
        
        connect(socket, &QWebSocket::binaryMessageReceived,
                this, &WebSocketServerApp::onBinaryMessageReceived);
        connect(socket, &QWebSocket::textMessageReceived,
                this, &WebSocketServerApp::onTextMessageReceived);
        connect(socket, &QWebSocket::disconnected,
                this, &WebSocketServerApp::onClientDisconnected);
        
        m_clients.append(socket);
        m_totalConnections++;
    }
    
    void onBinaryMessageReceived(const QByteArray &message)
    {
        QWebSocket *sender = qobject_cast<QWebSocket*>(this->sender());
        if (!sender) return;
        
        m_totalMessages++;
        m_totalBytes += message.size();
        
        // 转发给所有其他客户端（除了发送者）
        // 这样可以支持自己看自己的画面（一台电脑多个连接）
        int forwardCount = 0;
        for (QWebSocket *client : m_clients) {
            if (client != sender && client->state() == QAbstractSocket::ConnectedState) {
                client->sendBinaryMessage(message);
                forwardCount++;
            }
        }
        
        // 只在调试模式下输出统计，避免影响性能
        #ifdef QT_DEBUG
        if (m_totalMessages % 1000 == 0) {
            qDebug() << QDateTime::currentDateTime().toString()
                     << "已处理" << m_totalMessages << "条消息，当前转发给" 
                     << forwardCount << "个客户端";
        }
        #endif
    }
    
    void onTextMessageReceived(const QString &message)
    {
        QWebSocket *sender = qobject_cast<QWebSocket*>(this->sender());
        if (!sender) return;
        
        // 只在调试模式下输出详细日志，避免影响性能
        #ifdef QT_DEBUG
        qDebug() << QDateTime::currentDateTime().toString()
                 << "收到文本消息:" << message.left(100); // 只显示前100字符
        #endif
        
        // 尝试解析JSON消息
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
        
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject obj = doc.object();
            QString type = obj["type"].toString();
            
            if (type == "login") {
                handleUserLogin(sender, obj);
                return;
            } else if (type == "logout") {
                handleUserLogout(sender);
                return;
            } else if (type == "get_online_users") {
                sendOnlineUsersList(sender);
                return;
            } else if (type == "heartbeat") {
                QString uid = obj.value("id").toString();
                if (uid.isEmpty()) {
                    for (auto it = m_onlineUsers.begin(); it != m_onlineUsers.end(); ++it) {
                        if (it.value().socket == sender) { uid = it.key(); break; }
                    }
                }
                if (!uid.isEmpty() && m_onlineUsers.contains(uid)) {
                    m_onlineUsers[uid].lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
                }
                return;
            }
        }
        
        // 如果不是登录相关消息，按原来的方式转发
        for (QWebSocket *client : m_clients) {
            if (client != sender && client->state() == QAbstractSocket::ConnectedState) {
                client->sendTextMessage(message);
            }
        }
    }
    
    void onClientDisconnected()
    {
        QWebSocket *client = qobject_cast<QWebSocket*>(sender());
        if (client) {
            QString clientInfo = QString("%1:%2").arg(client->peerAddress().toString())
                                                 .arg(client->peerPort());
            qDebug() << QDateTime::currentDateTime().toString()
                     << "客户端断开连接:" << clientInfo;
            
            // 处理用户登出
            handleUserLogout(client);
            
            m_clients.removeAll(client);
            client->deleteLater();
        }
    }
    
    void printStats()
    {
        qDebug() << "=== 服务器统计信息 ===" 
                 << QDateTime::currentDateTime().toString();
        qDebug() << "监听端口:" << m_port;
        qDebug() << "当前连接数:" << m_clients.size();
        qDebug() << "在线用户数:" << m_onlineUsers.size();
        qDebug() << "总连接数:" << m_totalConnections;
        qDebug() << "总消息数:" << m_totalMessages;
        qDebug() << "总流量:" << QString("%1 MB").arg(m_totalBytes / 1024.0 / 1024.0, 0, 'f', 2);
        qDebug() << "平均消息大小:" << (m_totalMessages > 0 ? m_totalBytes / m_totalMessages : 0) << "字节";
        
        // 显示在线用户列表
        if (!m_onlineUsers.isEmpty()) {
            qDebug() << "在线用户:";
            for (auto it = m_onlineUsers.begin(); it != m_onlineUsers.end(); ++it) {
                const UserInfo& user = it.value();
                qDebug() << "  -" << user.id << "(" << user.name << ")" 
                         << "登录时间:" << user.loginTime.toString();
            }
        }
        qDebug() << "===================";
    }

private:
    void initializeUserDataPath()
    {
        // 创建用户数据目录
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir dir;
        if (!dir.exists(dataDir)) {
            dir.mkpath(dataDir);
        }
        m_userDataFile = dataDir + "/users.json";
        qDebug() << "用户数据文件路径:" << m_userDataFile;
    }
    
    void handleUserLogin(QWebSocket* socket, const QJsonObject& loginData)
    {
        QString userId = loginData["data"].toObject()["id"].toString();
        QString userName = loginData["data"].toObject()["name"].toString();
        int iconId = loginData["data"].toObject()["icon_id"].toInt(3); // 默认为3
        
        if (userId.isEmpty() || userName.isEmpty()) {
            sendLoginResponse(socket, false, "用户ID和名称不能为空");
            return;
        }
        
        // 验证icon ID范围
        if (iconId < 3 || iconId > 21) {
            iconId = 3; // 使用默认值
        }
        
        // 检查用户是否已经在线
        if (m_onlineUsers.contains(userId)) {
            // 如果用户已在线，先踢掉旧连接
            UserInfo& existingUser = m_onlineUsers[userId];
            if (existingUser.socket && existingUser.socket != socket) {
                existingUser.socket->close();
            }
        }
        
        // 添加或更新用户信息
        UserInfo userInfo(userId, userName, iconId, socket);
        userInfo.lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
        m_onlineUsers[userId] = userInfo;
        
        qDebug() << QDateTime::currentDateTime().toString()
                 << "用户登录:" << userId << "(" << userName << ") icon:" << iconId;
        
        // 发送登录成功响应
        sendLoginResponse(socket, true, "登录成功");
        
        // 保存用户数据到文件
        saveUserData();
        
        // 广播在线用户列表给所有客户端
        broadcastOnlineUsersList();
    }
    
    void handleUserLogout(QWebSocket* socket)
    {
        // 查找并移除用户
        QString logoutUserId;
        for (auto it = m_onlineUsers.begin(); it != m_onlineUsers.end(); ++it) {
            if (it.value().socket == socket) {
                logoutUserId = it.key();
                qDebug() << QDateTime::currentDateTime().toString()
                         << "用户登出:" << it.value().id << "(" << it.value().name << ")";
                m_onlineUsers.erase(it);
                break;
            }
        }
        
        if (!logoutUserId.isEmpty()) {
            // 保存用户数据到文件
            saveUserData();
            
            // 广播更新后的在线用户列表
            broadcastOnlineUsersList();
        }
    }
    
    void sendLoginResponse(QWebSocket* socket, bool success, const QString& message)
    {
        QJsonObject response;
        response["type"] = "login_response";
        response["success"] = success;
        response["message"] = message;
        
        if (success) {
            // 查找用户信息
            for (auto it = m_onlineUsers.begin(); it != m_onlineUsers.end(); ++it) {
                if (it.value().socket == socket) {
                    QJsonObject userData;
                    userData["id"] = it.value().id;
                    userData["name"] = it.value().name;
                    userData["streamUrl"] = QString("ws://%1:%2/stream/%3")
                                           .arg(m_server->serverAddress().toString())
                                           .arg(m_port)
                                           .arg(it.value().id);
                    response["data"] = userData;
                    break;
                }
            }
        }
        
        QJsonDocument doc(response);
        socket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
    }
    
    void sendOnlineUsersList(QWebSocket* socket)
    {
        QJsonObject response;
        response["type"] = "online_users";
        
        QJsonArray usersArray;
        for (auto it = m_onlineUsers.begin(); it != m_onlineUsers.end(); ++it) {
            const UserInfo& user = it.value();
            QJsonObject userObj;
            userObj["id"] = user.id;
            userObj["name"] = user.name;
            usersArray.append(userObj);
        }
        response["data"] = usersArray;
        
        QJsonDocument doc(response);
        socket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
    }
    
    void broadcastOnlineUsersList()
    {
        QJsonObject broadcast;
        broadcast["type"] = "online_users_update";
        
        QJsonArray usersArray;
        for (auto it = m_onlineUsers.begin(); it != m_onlineUsers.end(); ++it) {
            const UserInfo& user = it.value();
            QJsonObject userObj;
            userObj["id"] = user.id;
            userObj["name"] = user.name;
            userObj["icon_id"] = user.iconId;  // 添加icon ID到广播消息
            usersArray.append(userObj);
        }
        broadcast["data"] = usersArray;
        
        QJsonDocument doc(broadcast);
        QString message = doc.toJson(QJsonDocument::Compact);
        
        // 发送给所有连接的客户端
        for (QWebSocket *client : m_clients) {
            if (client->state() == QAbstractSocket::ConnectedState) {
                client->sendTextMessage(message);
            }
        }
        
        // 只在调试模式下输出详细日志，避免影响性能
        #ifdef QT_DEBUG
        qDebug() << QDateTime::currentDateTime().toString()
                 << "广播在线用户列表给" << m_clients.size() << "个客户端，用户数:" << m_onlineUsers.size();
        #endif
    }
    
    void saveUserData()
    {
        QJsonObject root;
        QJsonObject users;
        
        for (auto it = m_onlineUsers.begin(); it != m_onlineUsers.end(); ++it) {
            const UserInfo& user = it.value();
            QJsonObject userObj;
            userObj["id"] = user.id;
            userObj["name"] = user.name;
            userObj["online"] = true;
            userObj["lastLogin"] = user.loginTime.toString(Qt::ISODate);
            users[user.id] = userObj;
        }
        
        root["users"] = users;
        
        QJsonDocument doc(root);
        QFile file(m_userDataFile);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(doc.toJson());
            file.close();
        } else {
            qWarning() << "无法保存用户数据到文件:" << m_userDataFile;
        }
    }

    void checkHeartbeatTimeouts()
    {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        QList<QString> toRemove;
        for (auto it = m_onlineUsers.begin(); it != m_onlineUsers.end(); ++it) {
            if (it.value().lastHeartbeatMs > 0 && now - it.value().lastHeartbeatMs > 15000) {
                toRemove.append(it.key());
            }
        }
        for (const QString &uid : toRemove) {
            QWebSocket *sock = m_onlineUsers[uid].socket;
            if (sock) { sock->close(); }
            m_onlineUsers.remove(uid);
        }
        if (!toRemove.isEmpty()) {
            saveUserData();
            broadcastOnlineUsersList();
        }
    }

private:
    QWebSocketServer *m_server;
    QList<QWebSocket*> m_clients;
    QMap<QString, UserInfo> m_onlineUsers;  // 在线用户映射 (userId -> UserInfo)
    QString m_userDataFile;                 // 用户数据文件路径
    int m_port;
    quint64 m_totalConnections = 0;
    quint64 m_totalMessages = 0;
    quint64 m_totalBytes = 0;
    QTimer *m_heartbeatTimer = nullptr;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("WebSocket Screen Stream Server");
    app.setApplicationVersion("1.0");
    
    QCommandLineParser parser;
    parser.setApplicationDescription("WebSocket服务器用于屏幕流传输中继");
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption portOption(QStringList() << "p" << "port",
                                  "监听端口 (默认: 8765)", "port", "8765");
    parser.addOption(portOption);
    
    QCommandLineOption daemonOption(QStringList() << "d" << "daemon",
                                    "以守护进程模式运行");
    parser.addOption(daemonOption);
    
    parser.process(app);
    
    int port = parser.value(portOption).toInt();
    if (port <= 0 || port > 65535) {
        qDebug() << "错误：无效端口号" << port << "，端口范围应为1-65535";
        return 1;
    }
    
    qDebug() << "启动WebSocket屏幕流中继服务器";
    qDebug() << "版本:" << app.applicationVersion();
    qDebug() << "监听端口:" << port;
    qDebug() << "启动时间:" << QDateTime::currentDateTime().toString();
    
    if (parser.isSet(daemonOption)) {
        qDebug() << "以守护进程模式运行";
    }
    
    WebSocketServerApp serverApp(port);
    
    // 优雅关闭处理
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        qDebug() << QDateTime::currentDateTime().toString() 
                 << "服务器正在关闭...";
    });
    
    return app.exec();
}

#include "websocket_server_standalone.moc"
