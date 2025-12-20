#include <QCoreApplication>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QDebug>
#include <QCommandLineParser>
#include <QHostAddress>
#include <QTimer>
#include <QDateTime>
#include <QUrl>
#include <QMap>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// 房间管理类
class Room
{
public:
    QString roomId;
    QWebSocket *publisher = nullptr;  // 推流端
    QSet<QWebSocket*> subscribers;    // 订阅端集合
    QDateTime createdTime;
    quint64 messageCount = 0;
    quint64 totalBytes = 0;
    
    Room(const QString &id) : roomId(id), createdTime(QDateTime::currentDateTime()) {}
    
    void addSubscriber(QWebSocket *socket) {
        subscribers.insert(socket);
        qDebug() << QDateTime::currentDateTime().toString()
                 << "房间" << roomId << "新增订阅者，当前订阅者数量:" << subscribers.size();
    }
    
    void removeSubscriber(QWebSocket *socket) {
        subscribers.remove(socket);
        qDebug() << QDateTime::currentDateTime().toString()
                 << "房间" << roomId << "移除订阅者，当前订阅者数量:" << subscribers.size();
    }
    
    void setPublisher(QWebSocket *socket) {
        if (publisher) {
            qDebug() << QDateTime::currentDateTime().toString()
                     << "房间" << roomId << "替换推流端";
        }
        publisher = socket;
        qDebug() << QDateTime::currentDateTime().toString()
                 << "房间" << roomId << "设置推流端";
    }
    
    void removePublisher() {
        publisher = nullptr;
        qDebug() << QDateTime::currentDateTime().toString()
                 << "房间" << roomId << "推流端断开";
    }
    
    // 广播消息给所有订阅者
    int broadcastToSubscribers(const QByteArray &message) {
        messageCount++;
        totalBytes += message.size();
        
        int sentCount = 0;
        auto it = subscribers.begin();
        while (it != subscribers.end()) {
            QWebSocket *subscriber = *it;
            if (subscriber->state() == QAbstractSocket::ConnectedState) {
                subscriber->sendBinaryMessage(message);
                sentCount++;
                ++it;
            } else {
                // 移除断开的连接
                it = subscribers.erase(it);
            }
        }
        return sentCount;
    }
    
    bool isEmpty() const {
        return !publisher && subscribers.isEmpty();
    }
};

class WebSocketServerApp : public QObject
{
    Q_OBJECT

public:
    WebSocketServerApp(int port = 8765, QObject *parent = nullptr) : QObject(parent), m_port(port)
    {
        m_server = new QWebSocketServer(QStringLiteral("Screen Stream Server with Routing"), 
                                       QWebSocketServer::NonSecureMode, this);
        
        if (m_server->listen(QHostAddress::Any, port)) {
            qDebug() << QDateTime::currentDateTime().toString() 
                     << "WebSocket路由服务器启动成功，监听端口:" << port;
            connect(m_server, &QWebSocketServer::newConnection,
                    this, &WebSocketServerApp::onNewConnection);
            
            // 定时输出统计信息
            QTimer *statsTimer = new QTimer(this);
            connect(statsTimer, &QTimer::timeout, this, &WebSocketServerApp::printStats);
            statsTimer->start(30000); // 每30秒输出一次统计
            
            // 定时清理空房间
            QTimer *cleanupTimer = new QTimer(this);
            connect(cleanupTimer, &QTimer::timeout, this, &WebSocketServerApp::cleanupEmptyRooms);
            cleanupTimer->start(60000); // 每分钟清理一次
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
        
        // 解析URL路径
        QString path = socket->requestUrl().path();
        qDebug() << QDateTime::currentDateTime().toString() 
                 << "新客户端连接:" << clientInfo << "路径:" << path;
        
        // 特殊处理登录连接
        if (path == "/login" || path == "/") {
            qDebug() << "登录系统连接，使用简单广播模式";
            connect(socket, &QWebSocket::binaryMessageReceived,
                    this, &WebSocketServerApp::onBinaryMessageReceived);
            connect(socket, &QWebSocket::textMessageReceived,
                    this, &WebSocketServerApp::onTextMessageReceived);
            connect(socket, &QWebSocket::disconnected,
                    this, &WebSocketServerApp::onClientDisconnected);
            
            m_loginClients.append(socket);
            m_totalConnections++;
            return;
        }
        
        // 解析路径格式: /publish/{room_id} 或 /subscribe/{room_id}
        QStringList pathParts = path.split('/', Qt::SkipEmptyParts);
        
        if (pathParts.size() != 2) {
            qDebug() << "无效路径格式，期望: /publish/{room_id} 或 /subscribe/{room_id}";
            socket->close(QWebSocketProtocol::CloseCodeNormal, "Invalid path format");
            return;
        }
        
        QString action = pathParts[0];      // publish 或 subscribe
        QString roomId = pathParts[1];      // 房间ID
        
        if (action != "publish" && action != "subscribe") {
            qDebug() << "无效操作类型:" << action << "，期望: publish 或 subscribe";
            socket->close(QWebSocketProtocol::CloseCodeNormal, "Invalid action");
            return;
        }
        
        // 获取或创建房间
        if (!m_rooms.contains(roomId)) {
            m_rooms[roomId] = new Room(roomId);
            qDebug() << "创建新房间:" << roomId;
        }
        
        Room *room = m_rooms[roomId];
        
        // 根据操作类型处理连接
        if (action == "publish") {
            room->setPublisher(socket);
            m_clientRoles[socket] = QPair<QString, QString>(roomId, "publisher");
            
            // 自动触发推流：如果有订阅者加入且推流端在线，发送start_streaming
            if (!room->subscribers.isEmpty()) {
                QJsonObject startStreamingMsg;
                startStreamingMsg["type"] = "start_streaming";
                QJsonDocument startDoc(startStreamingMsg);
                socket->sendTextMessage(startDoc.toJson(QJsonDocument::Compact));
                qDebug() << "推流端加入，已有订阅者，自动触发推流" << roomId;
            }
        } else { // subscribe
            room->addSubscriber(socket);
            m_clientRoles[socket] = QPair<QString, QString>(roomId, "subscriber");
            
            // 自动触发推流：如果有订阅者加入且推流端在线，发送start_streaming
            if (room->publisher && room->publisher->state() == QAbstractSocket::ConnectedState) {
                QJsonObject startStreamingMsg;
                startStreamingMsg["type"] = "start_streaming";
                QJsonDocument startDoc(startStreamingMsg);
                room->publisher->sendTextMessage(startDoc.toJson(QJsonDocument::Compact));
                qDebug() << "订阅者加入，自动触发推流端" << roomId;
            }
        }
        
        // 连接信号槽
        connect(socket, &QWebSocket::binaryMessageReceived,
                this, &WebSocketServerApp::onBinaryMessageReceived);
        connect(socket, &QWebSocket::textMessageReceived,
                this, &WebSocketServerApp::onTextMessageReceived);
        connect(socket, &QWebSocket::disconnected,
                this, &WebSocketServerApp::onClientDisconnected);
        
        m_totalConnections++;
    }
    
    void onBinaryMessageReceived(const QByteArray &message)
    {
        QWebSocket *sender = qobject_cast<QWebSocket*>(this->sender());
        if (!sender || !m_clientRoles.contains(sender)) return;
        
        QPair<QString, QString> roleInfo = m_clientRoles[sender];
        QString roomId = roleInfo.first;
        QString role = roleInfo.second;
        
        // 只有推流端可以发送二进制消息
        if (role != "publisher") {
            qDebug() << "订阅端尝试发送二进制消息，忽略";
            return;
        }
        
        if (!m_rooms.contains(roomId)) {
            qDebug() << "房间不存在:" << roomId;
            return;
        }
        
        Room *room = m_rooms[roomId];
        int sentCount = room->broadcastToSubscribers(message);
        
        m_totalMessages++;
        m_totalBytes += message.size();
        
        // 每1000条消息输出一次转发统计
        if (m_totalMessages % 1000 == 0) {
            qDebug() << QDateTime::currentDateTime().toString()
                     << "房间" << roomId << "已处理" << room->messageCount 
                     << "条消息，当前转发给" << sentCount << "个订阅者";
        }
    }
    
    void onTextMessageReceived(const QString &message)
    {
        QWebSocket *sender = qobject_cast<QWebSocket*>(this->sender());
        if (!sender) return;
        
        // 检查是否是登录系统客户端
        if (m_loginClients.contains(sender)) {
            qDebug() << QDateTime::currentDateTime().toString()
                     << "登录系统消息:" << message.left(100);
            
            // 解析登录消息，处理用户登录
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
            
            if (error.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject obj = doc.object();
                QString type = obj["type"].toString();
                
                if (type == "login") {
                    QJsonObject data = obj["data"].toObject();
                    QString userId = data["id"].toString();
                    QString userName = data["name"].toString();
                    // 解析icon_id（优先使用icon_id，其次viewer_icon_id）
                    // 默认沿用已知服务器记录；若无记录则设为-1（未知，用默认头像）
                    int iconId = m_userIcons.value(userId, -1);
                    if (data.contains("icon_id")) {
                        int parsed = data["icon_id"].toInt(-1);
                        if (parsed >= 3 && parsed <= 21) {
                            iconId = parsed;
                        }
                    } else if (data.contains("viewer_icon_id")) {
                        int parsed = data["viewer_icon_id"].toInt(-1);
                        if (parsed >= 3 && parsed <= 21) {
                            iconId = parsed;
                        }
                    }
                    
                    // 存储用户信息
                    m_loginUsers[sender] = QPair<QString, QString>(userId, userName);
                    m_userIcons[userId] = iconId;
                    m_userLastHeartbeat[userId] = QDateTime::currentMSecsSinceEpoch();
                    
                    // 发送登录成功响应
                    QJsonObject response;
                    response["type"] = "login_response";
                    response["success"] = true;
                    response["message"] = "登录成功";
                    
                    QJsonObject responseData;
                    responseData["id"] = userId;
                    responseData["name"] = userName;
                    responseData["icon_id"] = iconId;
                    response["data"] = responseData;
                    
                    QJsonDocument responseDoc(response);
                    sender->sendTextMessage(responseDoc.toJson(QJsonDocument::Compact));
                    
                    // 广播在线用户列表
                    broadcastOnlineUsersList();
                    return;
                } else if (type == "watch_request") {
                    // 处理观看请求
                    QString viewerId = obj["viewer_id"].toString();
                    QString targetId = obj["target_id"].toString();
                    
                    qDebug() << QDateTime::currentDateTime().toString()
                             << "收到观看请求，观看者:" << viewerId << "目标:" << targetId;
                    
                    // 查找目标用户的WebSocket连接
                    QWebSocket *targetSocket = nullptr;
                    for (auto it = m_loginUsers.begin(); it != m_loginUsers.end(); ++it) {
                        if (it.value().first == targetId) {
                            targetSocket = it.key();
                            break;
                        }
                    }
                    
                    bool targetOnline = false;
                    if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
                        qint64 now = QDateTime::currentMSecsSinceEpoch();
                        qint64 ts = m_userLastHeartbeat.value(targetId, 0);
                        if (ts > 0 && now - ts <= 15000) {
                            targetOnline = true;
                        }
                    }
                    if (targetOnline) {
                        // 向目标用户发送推流请求
                        QJsonObject streamRequest;
                        streamRequest["type"] = "start_streaming_request";
                        streamRequest["viewer_id"] = viewerId;
                        streamRequest["target_id"] = targetId;
                        // [Fix] 透传 action 字段 (用于取消请求等)
                        if (obj.contains("action")) {
                            streamRequest["action"] = obj["action"];
                        }
                        
                        QJsonDocument streamDoc(streamRequest);
                        targetSocket->sendTextMessage(streamDoc.toJson(QJsonDocument::Compact));
                        
                        qDebug() << QDateTime::currentDateTime().toString()
                                 << "已向目标用户" << targetId << "发送推流请求";
                    } else {
                        qDebug() << QDateTime::currentDateTime().toString()
                                 << "目标用户" << targetId << "不在线或连接已断开";
                        
                        // 向观看者发送错误响应
                        QJsonObject errorResponse;
                        errorResponse["type"] = "watch_request_error";
                        errorResponse["message"] = "目标用户不在线";
                        errorResponse["target_id"] = targetId;
                        
                        QJsonDocument errorDoc(errorResponse);
                        sender->sendTextMessage(errorDoc.toJson(QJsonDocument::Compact));
                    }
                    return;
                } else if (type == "watch_request_canceled") {
                    // [New] 处理取消观看请求 - 直接转发给目标用户
                    QString viewerId = obj["viewer_id"].toString();
                    QString targetId = obj["target_id"].toString();
                    
                    qDebug() << QDateTime::currentDateTime().toString()
                             << "收到取消观看请求，观看者:" << viewerId << "目标:" << targetId;
                    
                    // 查找目标用户的WebSocket连接
                    QWebSocket *targetSocket = nullptr;
                    for (auto it = m_loginUsers.begin(); it != m_loginUsers.end(); ++it) {
                        if (it.value().first == targetId) {
                            targetSocket = it.key();
                            break;
                        }
                    }
                    
                    if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
                        QJsonDocument doc(obj);
                        targetSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
                        qDebug() << "已转发取消请求给目标用户:" << targetId;
                    }
                    return;
                } else if (type == "streaming_ok") {
                    // 处理推流OK响应，转发给观看者
                    QString viewerId = obj["viewer_id"].toString();
                    QString targetId = obj["target_id"].toString();
                    QString streamUrl = obj["stream_url"].toString();
                    
                    qDebug() << QDateTime::currentDateTime().toString()
                             << "收到推流OK响应，观看者:" << viewerId << "目标:" << targetId;
                    
                    // 查找观看者的WebSocket连接
                    QWebSocket *viewerSocket = nullptr;
                    for (auto it = m_loginUsers.begin(); it != m_loginUsers.end(); ++it) {
                        if (it.value().first == viewerId) {
                            viewerSocket = it.key();
                            break;
                        }
                    }
                    
                    if (viewerSocket && viewerSocket->state() == QAbstractSocket::ConnectedState) {
                        // 向观看者发送推流OK响应
                        QJsonObject okResponse;
                        okResponse["type"] = "streaming_ok";
                        okResponse["viewer_id"] = viewerId;
                        okResponse["target_id"] = targetId;
                        okResponse["stream_url"] = streamUrl;
                        
                        QJsonDocument okDoc(okResponse);
                        viewerSocket->sendTextMessage(okDoc.toJson(QJsonDocument::Compact));
                        
                        qDebug() << QDateTime::currentDateTime().toString()
                                 << "已向观看者" << viewerId << "转发推流OK响应";
                    } else {
                        qDebug() << QDateTime::currentDateTime().toString()
                                 << "观看者" << viewerId << "不在线或连接已断开";
                    }
                    
                    // 【关键修复】向推流端发送start_streaming消息来触发实际推流
                    if (m_rooms.contains(targetId)) {
                        Room *room = m_rooms[targetId];
                        if (room->publisher && room->publisher->state() == QAbstractSocket::ConnectedState) {
                            QJsonObject startStreamingMsg;
                            startStreamingMsg["type"] = "start_streaming";
                            
                            QJsonDocument startDoc(startStreamingMsg);
                            room->publisher->sendTextMessage(startDoc.toJson(QJsonDocument::Compact));
                            
                            qDebug() << QDateTime::currentDateTime().toString()
                                     << "已向推流端" << targetId << "发送start_streaming消息";
                        } else {
                            qDebug() << QDateTime::currentDateTime().toString()
                                     << "推流端" << targetId << "不在线或连接已断开";
                        }
                    } else {
                        qDebug() << QDateTime::currentDateTime().toString()
                                 << "房间" << targetId << "不存在";
                    }
                    return;
                } else if (type == "approval_required") {
                    QString viewerId = obj.value("viewer_id").toString();
                    QString targetId = obj.value("target_id").toString();
                    QWebSocket *viewerSocket = nullptr;
                    for (auto it = m_loginUsers.begin(); it != m_loginUsers.end(); ++it) {
                        if (it.value().first == viewerId) { viewerSocket = it.key(); break; }
                    }
                    if (viewerSocket && viewerSocket->state() == QAbstractSocket::ConnectedState) {
                        QJsonDocument doc(obj);
                        viewerSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
                        qDebug() << QDateTime::currentDateTime().toString()
                                 << "已向观看者" << viewerId << "转发审批请求";
                    }
                    return;
                } else if (type == "watch_request_accepted") {
                    QString viewerId = obj.value("viewer_id").toString();
                    QString targetId = obj.value("target_id").toString();
                    
                    // 1. 转发给观看者 (Viewer)
                    QWebSocket *viewerSocket = nullptr;
                    for (auto it = m_loginUsers.begin(); it != m_loginUsers.end(); ++it) {
                        if (it.value().first == viewerId) { viewerSocket = it.key(); break; }
                    }
                    if (viewerSocket && viewerSocket->state() == QAbstractSocket::ConnectedState) {
                        QJsonDocument doc(obj);
                        viewerSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
                        qDebug() << QDateTime::currentDateTime().toString()
                                 << "已向观看者" << viewerId << "转发同意消息";
                    }

                    // 2. 转发给推流端 (Publisher) 以触发推流
                    if (m_rooms.contains(targetId)) {
                        Room *room = m_rooms[targetId];
                        if (room && room->publisher && room->publisher->state() == QAbstractSocket::ConnectedState) {
                            QJsonDocument doc(obj);
                            room->publisher->sendTextMessage(doc.toJson(QJsonDocument::Compact));
                            qDebug() << QDateTime::currentDateTime().toString()
                                     << "已向推流端" << targetId << "转发同意消息(触发推流)";
                        }
                    }
                    return;
                } else if (type == "watch_request_rejected") {
                    QString viewerId = obj.value("viewer_id").toString();
                    QString targetId = obj.value("target_id").toString();
                    QWebSocket *viewerSocket = nullptr;
                    for (auto it = m_loginUsers.begin(); it != m_loginUsers.end(); ++it) {
                        if (it.value().first == viewerId) { viewerSocket = it.key(); break; }
                    }
                    if (viewerSocket && viewerSocket->state() == QAbstractSocket::ConnectedState) {
                        QJsonDocument doc(obj);
                        viewerSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
                        qDebug() << QDateTime::currentDateTime().toString()
                                 << "已向观看者" << viewerId << "转发拒绝消息";
                    }
                    return;
                } else if (type == "streaming_ok") {
                    QString viewerId = obj.value("viewer_id").toString();
                    QString targetId = obj.value("target_id").toString();
                    QWebSocket *viewerSocket = nullptr;
                    for (auto it = m_loginUsers.begin(); it != m_loginUsers.end(); ++it) {
                        if (it.value().first == viewerId) { viewerSocket = it.key(); break; }
                    }
                    if (viewerSocket && viewerSocket->state() == QAbstractSocket::ConnectedState) {
                        QJsonDocument doc(obj);
                        viewerSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
                        qDebug() << QDateTime::currentDateTime().toString()
                                 << "已向观看者" << viewerId << "转发推流就绪消息";
                    }
                    return;
                } else if (type == "kick_viewer") {
                    QString viewerId = obj.value("viewer_id").toString();
                    QString targetId = obj.value("target_id").toString();
                    QWebSocket *viewerSocket = nullptr;
                    for (auto it = m_loginUsers.begin(); it != m_loginUsers.end(); ++it) {
                        if (it.value().first == viewerId) { viewerSocket = it.key(); break; }
                    }
                    if (viewerSocket && viewerSocket->state() == QAbstractSocket::ConnectedState) {
                        QJsonDocument doc(obj);
                        viewerSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
                        qDebug() << QDateTime::currentDateTime().toString()
                                 << "已向观看者" << viewerId << "转发踢出消息，房主:" << targetId;
                    } else {
                        qDebug() << QDateTime::currentDateTime().toString()
                                 << "观看者" << viewerId << "不在线或连接已断开，无法转发踢出消息";
                    }
                    return;
                } else if (type == "viewer_mic_state") {
                    QString viewerId = obj.value("viewer_id").toString();
                    QString targetId = obj.value("target_id").toString();
                    if (viewerId.isEmpty() || targetId.isEmpty()) {
                        return;
                    }
                    QWebSocket *targetSocket = nullptr;
                    for (auto it = m_loginUsers.begin(); it != m_loginUsers.end(); ++it) {
                        if (it.value().first == targetId) { targetSocket = it.key(); break; }
                    }
                    if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
                        QJsonDocument doc(obj);
                        targetSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
                    }
                    return;
                } else if (type == "heartbeat") {
                    QString uid = obj.value("id").toString();
                    if (uid.isEmpty()) {
                        QPair<QString, QString> ui = m_loginUsers.value(sender);
                        uid = ui.first;
                    }
                    if (!uid.isEmpty()) {
                        m_userLastHeartbeat[uid] = QDateTime::currentMSecsSinceEpoch();
                    }
                    return;
                } else if (type == "ping") {
                    // 处理轻量级心跳
                    QPair<QString, QString> ui = m_loginUsers.value(sender);
                    QString uid = ui.first;
                    if (!uid.isEmpty()) {
                        m_userLastHeartbeat[uid] = QDateTime::currentMSecsSinceEpoch();
                    }
                    return;
                }
            }
            
            // 其他登录系统消息广播给所有登录客户端 - 已禁用以防止全员广播干扰
            // for (QWebSocket *client : m_loginClients) {
            //     if (client != sender && client->state() == QAbstractSocket::ConnectedState) {
            //         client->sendTextMessage(message);
            //     }
            // }
            return;
        }
        
        // 房间系统消息处理
        if (!m_clientRoles.contains(sender)) return;
        
        QPair<QString, QString> roleInfo = m_clientRoles[sender];
        QString roomId = roleInfo.first;
        QString role = roleInfo.second;
        
        // 减少日志输出：仅在非鼠标位置消息时打印
        // qDebug() << QDateTime::currentDateTime().toString()
        //          << "房间" << roomId << role << "发送文本消息:" << message.left(100);
        
        // 解析JSON消息以处理特定类型
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
        
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject obj = doc.object();
            QString type = obj["type"].toString();
            
            // 处理鼠标位置消息 - 只从推流端转发给订阅者
            if (type == "mouse_position" && role == "publisher") {
                if (m_rooms.contains(roomId)) {
                    Room *room = m_rooms[roomId];
                    int sentCount = 0;
                    
                    for (QWebSocket *subscriber : room->subscribers) {
                        if (subscriber->state() == QAbstractSocket::ConnectedState) {
                            subscriber->sendTextMessage(message);
                            sentCount++;
                        }
                    }
                    
                    // 每1000条鼠标消息输出一次统计（避免日志过多）
                    static int mouseMessageCount = 0;
                    mouseMessageCount++;
                    if (mouseMessageCount % 1000 == 0) {
                        // qDebug() << QDateTime::currentDateTime().toString()
                        //          << "房间" << roomId << "已转发" << mouseMessageCount 
                        //          << "条鼠标位置消息给" << sentCount << "个订阅者";
                    }
                }
                return; // 鼠标消息处理完毕，不再进行通用转发
            } else if (type == "audio_opus") {
                // 降低日志频率：每100个包打印一次，或者每秒一次
                static int audioCount = 0;
                if (++audioCount % 100 == 0) {
                    qDebug() << "转发音频包 序号:" << audioCount << " 来源:" << sender->peerAddress().toString();
                }
                // 转发给房间内的所有订阅者
                if (m_rooms.contains(roomId)) {
                    Room *room = m_rooms[roomId];
                    for (QWebSocket *subscriber : room->subscribers) {
                        if (subscriber == sender) continue; // 防止回音：不要发回给发送者
                        if (subscriber->state() == QAbstractSocket::ConnectedState) {
                            subscriber->sendTextMessage(message);
                        }
                    }
                }
                return;
            }
        }
        
        // 文本消息转发
        if (m_rooms.contains(roomId)) {
            Room *room = m_rooms[roomId];
            QJsonParseError jerr;
            QJsonDocument jdoc = QJsonDocument::fromJson(message.toUtf8(), &jerr);
            QString t = QString();
            if (jerr.error == QJsonParseError::NoError && jdoc.isObject()) {
                t = jdoc.object().value("type").toString();
            }
            if (role == "subscriber" && t == "viewer_audio_opus") {
                if (room->publisher && room->publisher->state() == QAbstractSocket::ConnectedState) {
                    room->publisher->sendTextMessage(message);
                }
                // 恢复转发：允许消费者之间互通 (Consumer -> Consumer)
                // 之前为了防回音禁用了它，但导致了“岔路”不通。
                // 现在的策略是：全通路打通，回音问题交给客户端处理或用户配置（如佩戴耳机）。
                for (QWebSocket *subscriber : room->subscribers) {
                    if (subscriber == sender) continue;
                    if (subscriber->state() == QAbstractSocket::ConnectedState) {
                        subscriber->sendTextMessage(message);
                    }
                }
            } else if (role == "publisher") {
                for (QWebSocket *subscriber : room->subscribers) {
                    if (subscriber->state() == QAbstractSocket::ConnectedState) {
                        subscriber->sendTextMessage(message);
                    }
                }
            } else {
                if (room->publisher && room->publisher->state() == QAbstractSocket::ConnectedState) {
                    room->publisher->sendTextMessage(message);
                }
            }
        }
    }
    
    void onClientDisconnected()
    {
        QWebSocket *client = qobject_cast<QWebSocket*>(sender());
        if (!client) return;
        
        QString clientInfo = QString("%1:%2").arg(client->peerAddress().toString())
                                             .arg(client->peerPort());
        
        // 检查是否是登录系统客户端
        if (m_loginClients.contains(client)) {
            qDebug() << QDateTime::currentDateTime().toString()
                     << "登录系统客户端断开连接:" << clientInfo;
            
            // 从登录用户列表中移除
            if (m_loginUsers.contains(client)) {
                QPair<QString, QString> userInfo = m_loginUsers[client];
                qDebug() << QDateTime::currentDateTime().toString()
                         << "用户登出:" << userInfo.first << "(" << userInfo.second << ")";
                m_loginUsers.remove(client);
                // 同步移除头像记录
                m_userIcons.remove(userInfo.first);
                m_userLastHeartbeat.remove(userInfo.first);
                
                // 广播更新后的在线用户列表
                broadcastOnlineUsersList();
            }
            
            m_loginClients.removeAll(client);
            client->deleteLater();
            return;
        }
        
        // 房间系统客户端断开处理
        if (!m_clientRoles.contains(client)) {
            client->deleteLater();
            return;
        }
        
        QPair<QString, QString> roleInfo = m_clientRoles[client];
        QString roomId = roleInfo.first;
        QString role = roleInfo.second;
        
        qDebug() << QDateTime::currentDateTime().toString()
                 << "客户端断开连接:" << clientInfo << "房间:" << roomId << "角色:" << role;
        
        // 从房间中移除客户端
        if (m_rooms.contains(roomId)) {
            Room *room = m_rooms[roomId];
            if (role == "publisher") {
                room->removePublisher();
            } else {
                room->removeSubscriber(client);
            }
        }
        
        m_clientRoles.remove(client);
        client->deleteLater();
    }
    
    void cleanupEmptyRooms()
    {
        QStringList emptyRooms;
        for (auto it = m_rooms.begin(); it != m_rooms.end(); ++it) {
            if (it.value()->isEmpty()) {
                emptyRooms.append(it.key());
            }
        }
        
        for (const QString &roomId : emptyRooms) {
            qDebug() << "清理空房间:" << roomId;
            delete m_rooms[roomId];
            m_rooms.remove(roomId);
        }
    }
    
    void printStats()
    {
        qDebug() << "=== 路由服务器统计信息 ===" 
                 << QDateTime::currentDateTime().toString();
        qDebug() << "监听端口:" << m_port;
        qDebug() << "活跃房间数:" << m_rooms.size();
        qDebug() << "总连接数:" << m_totalConnections;
        qDebug() << "总消息数:" << m_totalMessages;
        qDebug() << "总流量:" << QString("%1 MB").arg(m_totalBytes / 1024.0 / 1024.0, 0, 'f', 2);
        
        // 输出每个房间的详细信息
        for (auto it = m_rooms.begin(); it != m_rooms.end(); ++it) {
            Room *room = it.value();
            qDebug() << "  房间" << room->roomId << ":"
                     << "推流端:" << (room->publisher ? "在线" : "离线")
                     << "订阅者:" << room->subscribers.size()
                     << "消息数:" << room->messageCount
                     << "流量:" << QString("%1 MB").arg(room->totalBytes / 1024.0 / 1024.0, 0, 'f', 2);
        }
        qDebug() << "===================";
    }

private:
    QWebSocketServer *m_server;
    QMap<QString, Room*> m_rooms;                           // 房间管理
    QMap<QWebSocket*, QPair<QString, QString>> m_clientRoles; // 客户端角色 (roomId, role)
    QList<QWebSocket*> m_loginClients;                      // 登录系统客户端列表
    QMap<QWebSocket*, QPair<QString, QString>> m_loginUsers; // 登录用户信息 (socket -> (userId, userName))
    QMap<QString, int> m_userIcons;                         // 登录用户头像 (userId -> iconId)
    QMap<QString, qint64> m_userLastHeartbeat;              // 登录用户最近心跳
    QTimer *m_heartbeatTimer = nullptr;                     // 心跳检查定时器
    int m_port;
    quint64 m_totalConnections = 0;
    quint64 m_totalMessages = 0;
    quint64 m_totalBytes = 0;
    
    // 广播在线用户列表
    void broadcastOnlineUsersList()
    {
        QJsonObject broadcast;
        broadcast["type"] = "online_users_update";
        
        QJsonArray usersArray;
        for (auto it = m_loginUsers.begin(); it != m_loginUsers.end(); ++it) {
            QPair<QString, QString> userInfo = it.value();
            QJsonObject userObj;
            userObj["id"] = userInfo.first;
            userObj["name"] = userInfo.second;
            userObj["icon_id"] = m_userIcons.value(userInfo.first, 3);
            usersArray.append(userObj);
        }
        broadcast["data"] = usersArray;
        
        QJsonDocument doc(broadcast);
        QString message = doc.toJson(QJsonDocument::Compact);
        
        // 发送给所有登录客户端
        for (QWebSocket *client : m_loginClients) {
            if (client->state() == QAbstractSocket::ConnectedState) {
                client->sendTextMessage(message);
            }
        }
        
        qDebug() << QDateTime::currentDateTime().toString()
                 << "广播在线用户列表给" << m_loginClients.size() << "个登录客户端，用户数:" << usersArray.size();
    }

    void checkHeartbeatTimeouts()
    {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        QList<QString> toRemove;
        for (auto it = m_userLastHeartbeat.begin(); it != m_userLastHeartbeat.end(); ++it) {
            if (it.value() > 0 && now - it.value() > 15000) {
                toRemove.append(it.key());
            }
        }
        for (const QString &uid : toRemove) {
            QWebSocket *sock = nullptr;
            for (auto it = m_loginUsers.begin(); it != m_loginUsers.end(); ++it) {
                if (it.value().first == uid) { sock = it.key(); break; }
            }
            if (sock) {
                m_loginUsers.remove(sock);
                sock->close();
            }
            m_userIcons.remove(uid);
            m_userLastHeartbeat.remove(uid);
        }
        if (!toRemove.isEmpty()) {
            broadcastOnlineUsersList();
        }
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("WebSocket Screen Stream Server with Routing");
    app.setApplicationVersion("2.0");
    
    QCommandLineParser parser;
    parser.setApplicationDescription("支持路径路由的WebSocket服务器用于屏幕流传输中继");
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
    
    qDebug() << "启动WebSocket屏幕流路由服务器";
    qDebug() << "版本:" << app.applicationVersion();
    qDebug() << "监听端口:" << port;
    qDebug() << "支持路径格式: /publish/{room_id} 和 /subscribe/{room_id}";
    qDebug() << "启动时间:" << QDateTime::currentDateTime().toString();
    
    if (parser.isSet(daemonOption)) {
        qDebug() << "以守护进程模式运行";
    }
    
    WebSocketServerApp serverApp(port);
    
    // 优雅关闭处理
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        qDebug() << QDateTime::currentDateTime().toString() 
                 << "路由服务器正在关闭...";
    });
    
    return app.exec();
}

#include "websocket_server_with_routing.moc"
