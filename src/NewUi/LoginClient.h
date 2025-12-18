#pragma once

#include <QObject>
#include <QWebSocket>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

class LoginClient : public QObject {
    Q_OBJECT
public:
    explicit LoginClient(QObject *parent = nullptr);
    ~LoginClient();

    void connectToServer(const QUrl &url);
    void disconnectFromServer();
    void login(const QString &userId, const QString &userName);
    bool isConnected() const;

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &errorMsg);
    void logMessage(const QString &msg);
    void userListUpdated(const QJsonArray &users);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString &message);
    void onError(QAbstractSocket::SocketError error);
    void sendHeartbeat();

private:
    QWebSocket *m_webSocket;
    bool m_isConnected;
    QTimer *m_heartbeatTimer;
    QString m_currentUserId;
};
