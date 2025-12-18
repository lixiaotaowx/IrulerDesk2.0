#include "LoginClient.h"
#include <QDebug>

LoginClient::LoginClient(QObject *parent)
    : QObject(parent)
    , m_webSocket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , m_isConnected(false)
    , m_heartbeatTimer(new QTimer(this))
{
    connect(m_webSocket, &QWebSocket::connected, this, &LoginClient::onConnected);
    connect(m_webSocket, &QWebSocket::disconnected, this, &LoginClient::onDisconnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &LoginClient::onTextMessageReceived);
    connect(m_webSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred),
            this, &LoginClient::onError);
            
    // Setup heartbeat timer
    connect(m_heartbeatTimer, &QTimer::timeout, this, &LoginClient::sendHeartbeat);
    m_heartbeatTimer->setInterval(5000); // Send heartbeat every 5 seconds
}

LoginClient::~LoginClient()
{
    if (m_heartbeatTimer->isActive()) {
        m_heartbeatTimer->stop();
    }
    if (m_webSocket) {
        m_webSocket->close();
    }
}

void LoginClient::connectToServer(const QUrl &url)
{
    emit logMessage("LoginClient connecting to: " + url.toString());
    m_webSocket->open(url);
}

void LoginClient::disconnectFromServer()
{
    m_webSocket->close();
}

void LoginClient::login(const QString &userId, const QString &userName)
{
    if (!m_isConnected) return;

    m_currentUserId = userId; // Store for heartbeat

    QJsonObject data;
    data["id"] = userId;
    data["name"] = userName;

    QJsonObject message;
    message["type"] = "login";
    message["data"] = data;

    QJsonDocument doc(message);
    m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
    emit logMessage("Sent login request for user: " + userId);
    
    // Start sending heartbeats
    if (!m_heartbeatTimer->isActive()) {
        m_heartbeatTimer->start();
    }
}

bool LoginClient::isConnected() const
{
    return m_isConnected;
}

void LoginClient::onConnected()
{
    m_isConnected = true;
    emit connected();
    emit logMessage("LoginClient Connected");
}

void LoginClient::onDisconnected()
{
    m_isConnected = false;
    if (m_heartbeatTimer->isActive()) {
        m_heartbeatTimer->stop();
    }
    emit disconnected();
    emit logMessage("LoginClient Disconnected");
}

void LoginClient::sendHeartbeat()
{
    if (!m_isConnected || m_currentUserId.isEmpty()) return;

    QJsonObject message;
    message["type"] = "heartbeat";
    message["id"] = m_currentUserId;

    QJsonDocument doc(message);
    m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
    // emit logMessage("Sent heartbeat"); // Comment out to avoid log spam
}

void LoginClient::onTextMessageReceived(const QString &message)
{
    // emit logMessage("LoginClient received: " + message);
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        emit logMessage("JSON Parse Error: " + error.errorString());
        return;
    }

    if (!doc.isObject()) return;

    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    if (type == "online_users_update") {
        QJsonArray users = obj["data"].toArray();
        emit userListUpdated(users);
        emit logMessage(QString("Received user list update with %1 users").arg(users.size()));
    } else if (type == "login_response") {
        bool success = obj["success"].toBool();
        QString msg = obj["message"].toString();
        emit logMessage(QString("Login response: %1 (%2)").arg(success ? "Success" : "Failed").arg(msg));
    }
}

void LoginClient::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    emit errorOccurred(m_webSocket->errorString());
    emit logMessage("LoginClient Error: " + m_webSocket->errorString());
}
