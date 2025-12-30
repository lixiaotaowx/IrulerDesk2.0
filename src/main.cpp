#include <QApplication>
#include <QIcon>
#include <QStyleFactory>
#include <QSplashScreen>
#include <QPixmap>
#include <QPainter>
#include <QFont>
#include <QRandomGenerator>
#include <QGuiApplication>
#include <QScreen>
#include <QLabel>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QByteArray>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QHash>
#include <QSet>
#include <QPointer>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QThread>
#include <iostream>
#include <atomic>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#endif
#include "common/ConsoleLogger.h"
#include "common/CrashGuard.h"
#include "common/AppConfig.h"
#include "MainWindow.h"
#include "NewUi/NewUiWindow.h"

namespace {
class LanRelayServer final : public QObject
{
public:
    explicit LanRelayServer(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    bool start(quint16 port)
    {
        if (m_server) {
            return true;
        }

        m_server = new QWebSocketServer(QStringLiteral("IrulerDeskpro LAN Relay"), QWebSocketServer::NonSecureMode, this);
        if (!m_server->listen(QHostAddress::AnyIPv4, port)) {
            const QString err = m_server->errorString();
            m_server->deleteLater();
            m_server = nullptr;
            qWarning().noquote() << "[LanRelay] start_failed"
                                 << " port=" << port
                                 << " err=" << err;
            return false;
        }

        connect(m_server, &QWebSocketServer::newConnection, this, &LanRelayServer::onNewConnection);
        qInfo().noquote() << "[LanRelay] started"
                          << " port=" << m_server->serverPort();
        return true;
    }

    quint16 port() const
    {
        if (!m_server) return 0;
        return static_cast<quint16>(m_server->serverPort());
    }

private:
    struct Room {
        QWebSocket *publisher = nullptr;
        QSet<QWebSocket*> subscribers;
        QVector<QString> pendingTextToPublisher;
        QVector<QByteArray> pendingBinaryToPublisher;
    };

    QWebSocketServer *m_server = nullptr;
    QHash<QString, Room> m_rooms;

    void onNewConnection()
    {
        if (!m_server) return;
        QWebSocket *sock = m_server->nextPendingConnection();
        if (!sock) return;

        const QString path = sock->requestUrl().path();
        const QStringList parts = path.split('/', Qt::SkipEmptyParts);
        if (parts.size() < 2) {
            sock->close();
            sock->deleteLater();
            return;
        }

        const QString role = parts.value(0);
        const QString roomId = parts.value(1);
        if (roomId.isEmpty()) {
            sock->close();
            sock->deleteLater();
            return;
        }

        if (role != QStringLiteral("publish") && role != QStringLiteral("subscribe")) {
            sock->close();
            sock->deleteLater();
            return;
        }

        qInfo().noquote() << "[LanRelay] New connection:" << sock->peerAddress().toString()
                          << " role=" << role << " roomId=" << roomId;

        Room &room = m_rooms[roomId];

        if (role == QStringLiteral("publish")) {
            if (room.publisher && room.publisher != sock) {
                room.publisher->close();
                room.publisher->deleteLater();
            }
            room.publisher = sock;
            qInfo().noquote() << "[LanRelay] Publisher connected for room:" << roomId;
            if (!room.pendingTextToPublisher.isEmpty()) {
                for (const QString &msg : room.pendingTextToPublisher) {
                    if (sock->state() == QAbstractSocket::ConnectedState) {
                        sock->sendTextMessage(msg);
                    }
                }
                room.pendingTextToPublisher.clear();
            }
            if (!room.pendingBinaryToPublisher.isEmpty()) {
                for (const QByteArray &msg : room.pendingBinaryToPublisher) {
                    if (sock->state() == QAbstractSocket::ConnectedState) {
                        sock->sendBinaryMessage(msg);
                    }
                }
                room.pendingBinaryToPublisher.clear();
            }

            connect(sock, &QWebSocket::binaryMessageReceived, this, [this, roomId](const QByteArray &msg) {
                auto it = m_rooms.find(roomId);
                if (it == m_rooms.end()) return;
                for (QWebSocket *rawSub : it->subscribers) {
                    QPointer<QWebSocket> sub = rawSub;
                    if (sub && sub->state() == QAbstractSocket::ConnectedState) {
                        sub->sendBinaryMessage(msg);
                    }
                }
            });

            connect(sock, &QWebSocket::textMessageReceived, this, [this, roomId](const QString &msg) {
                auto it = m_rooms.find(roomId);
                if (it == m_rooms.end()) return;
                for (QWebSocket *rawSub : it->subscribers) {
                    QPointer<QWebSocket> sub = rawSub;
                    if (sub && sub->state() == QAbstractSocket::ConnectedState) {
                        sub->sendTextMessage(msg);
                    }
                }
            });
        } else {
            room.subscribers.insert(sock);
            qInfo().noquote() << "[LanRelay] Subscriber connected for room:" << roomId;

            connect(sock, &QWebSocket::textMessageReceived, this, [this, roomId](const QString &msg) {
                auto it = m_rooms.find(roomId);
                if (it == m_rooms.end()) return;
                QPointer<QWebSocket> pub = it->publisher;
                if (pub && pub->state() == QAbstractSocket::ConnectedState) {
                    pub->sendTextMessage(msg);
                } else {
                    it->pendingTextToPublisher.append(msg);
                    if (it->pendingTextToPublisher.size() > 12) {
                        it->pendingTextToPublisher.remove(0, it->pendingTextToPublisher.size() - 12);
                    }
                }
            });

            connect(sock, &QWebSocket::binaryMessageReceived, this, [this, roomId](const QByteArray &msg) {
                auto it = m_rooms.find(roomId);
                if (it == m_rooms.end()) return;
                QPointer<QWebSocket> pub = it->publisher;
                if (pub && pub->state() == QAbstractSocket::ConnectedState) {
                    pub->sendBinaryMessage(msg);
                } else {
                    it->pendingBinaryToPublisher.append(msg);
                    if (it->pendingBinaryToPublisher.size() > 6) {
                        it->pendingBinaryToPublisher.remove(0, it->pendingBinaryToPublisher.size() - 6);
                    }
                }
            });
        }

        connect(sock, &QWebSocket::disconnected, this, [this, sock, roomId]() {
            auto it = m_rooms.find(roomId);
            if (it == m_rooms.end()) {
                sock->deleteLater();
                return;
            }

            if (it->publisher == sock) {
                it->publisher = nullptr;
            }
            it->subscribers.remove(sock);

            if (!it->publisher && it->subscribers.isEmpty()) {
                m_rooms.erase(it);
            }

            sock->deleteLater();
        });
    }
};
}

int main(int argc, char *argv[])
{
    // 安装崩溃守护与控制台日志重定向
    CrashGuard::install();
    ConsoleLogger::attachToParentConsole();
    ConsoleLogger::installQtMessageHandler();

    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    
    {
        QLocalSocket probe;
        probe.connectToServer("IrulerDeskpro_SingleInstance");
        if (probe.waitForConnected(100)) {
            QByteArray payload("show_main_window");
            probe.write(payload);
            probe.flush();
            probe.waitForBytesWritten(100);
            return 0;
        }
    }

    std::atomic<quint64> uiBeat{0};
    QTimer *uiBeatTimer = new QTimer(&app);
    uiBeatTimer->setInterval(200);
    QObject::connect(uiBeatTimer, &QTimer::timeout, &app, [&uiBeat]() {
        uiBeat.fetch_add(1, std::memory_order_relaxed);
    });
    uiBeatTimer->start();

    QThread *hangThread = new QThread(&app);
    QObject *hangWorker = new QObject();
    hangWorker->moveToThread(hangThread);
    QObject::connect(hangThread, &QThread::finished, hangWorker, &QObject::deleteLater);
    hangThread->start();
    QMetaObject::invokeMethod(hangWorker, [hangWorker, &uiBeat]() {
        QTimer *t = new QTimer(hangWorker);
        t->setInterval(2000);
        auto last = std::make_shared<quint64>(uiBeat.load(std::memory_order_relaxed));
        auto stuck = std::make_shared<int>(0);
        auto cooldown = std::make_shared<int>(0);
        QObject::connect(t, &QTimer::timeout, hangWorker, [last, stuck, cooldown, &uiBeat]() mutable {
            const quint64 now = uiBeat.load(std::memory_order_relaxed);
            if (now == *last) {
                (*stuck)++;
            } else {
                *last = now;
                *stuck = 0;
            }
            if (*cooldown > 0) {
                (*cooldown)--;
                return;
            }
            if (*stuck >= 4) {
                CrashGuard::writeMiniDump("ui");
                *cooldown = 30;
            }
        });
        t->start();
    }, Qt::QueuedConnection);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [hangThread]() {
        if (!hangThread) return;
        hangThread->quit();
        hangThread->wait(1500);
    });

    QString appDir = QCoreApplication::applicationDirPath();
    app.setWindowIcon(QIcon(appDir + "/maps/logo/iruler.ico"));
    
    AppConfig::applyApplicationInfo(app);
    
    // 设置现代化样式
    app.setStyle(QStyleFactory::create("Fusion"));
    
    // 设置深色主题
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(darkPalette);

    // 进一步通过样式表确保所有样式一致
    app.setStyleSheet(
        "QToolTip {\n"
        "    color: #e0e0e0;\n"
        "    background-color: #3b3b3b;\n"
        "    border: none;\n"
        "    border-radius: 8px;\n"
        "    padding: 6px 12px;\n"
        "    font-size: 12px;\n"
        "}"
    );
    
    QString dir = QCoreApplication::applicationDirPath();
    int idx = QRandomGenerator::global()->bounded(0, 9);
    QPixmap base(dir + "/maps/assets/" + QString::number(idx) + ".jpg");
    int maxEdge = 640;
    QSize ss;
    if (!base.isNull()) {
        if (base.width() >= base.height()) {
            int h = static_cast<int>((static_cast<double>(base.height()) * maxEdge) / qMax(1, base.width()));
            ss = QSize(maxEdge, qMax(1, h));
        } else {
            int w = static_cast<int>((static_cast<double>(base.width()) * maxEdge) / qMax(1, base.height()));
            ss = QSize(qMax(1, w), maxEdge);
        }
    } else {
        ss = QSize(maxEdge, (maxEdge * 9) / 16);
    }
    QPixmap canvas(ss);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (!base.isNull()) {
        QPixmap scaled = base.scaled(ss, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        int ox = (ss.width() - scaled.width()) / 2;
        int oy = (ss.height() - scaled.height()) / 2;
        p.drawPixmap(ox, oy, scaled);
    }
    QPixmap wm(dir + "/maps/assets/shuiyin.png");
    if (!wm.isNull()) {
        int maxW = qMax(24, ss.width() / 12);
        int w = qMin(wm.width(), maxW);
        int h = (w * wm.height()) / qMax(1, wm.width());
        int m = 12;
        int x = ss.width() - w - m;
        int y = ss.height() - h - m;
        p.setOpacity(0.85);
        p.drawPixmap(x, y, wm.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        p.setOpacity(1.0);
    }
    // 绘制进度条初始背景（确保启动瞬间可见）
    int barWidth = ss.width() * 0.4;
    int barHeight = 4;
    int barX = (ss.width() - barWidth) / 2;
    int barY = ss.height() - barHeight - 20;

    // 静态绘制背景
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 60));
    p.drawRoundedRect(barX, barY, barWidth, barHeight, 2, 2);

    p.end();
    QSplashScreen splash(canvas);
    splash.show();

    // 进度条动画
    QTimer *splashTimer = new QTimer(&app);
    QObject::connect(splashTimer, &QTimer::timeout, [&splash, canvas, barX, barY, barWidth, barHeight, progress = 0]() mutable {
        if (progress < 95) {
            if (progress < 50) progress += 4;
            else if (progress < 80) progress += 2;
            else if (progress < 92) progress += 1;
            else if (QRandomGenerator::global()->bounded(3) == 0) progress += 1;
            if (progress > 95) progress = 95;
        }

        QPixmap temp = canvas;
        QPainter p(&temp);
        p.setRenderHint(QPainter::Antialiasing);

        // 进度条背景
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 60));
        p.drawRoundedRect(barX, barY, barWidth, barHeight, 2, 2);

        // 进度条前景
        p.setBrush(QColor(255, 255, 255, 200));
        int w = (barWidth * progress) / 100;
        p.drawRoundedRect(barX, barY, w, barHeight, 2, 2);
        p.end();

        splash.setPixmap(temp);
    });
    splashTimer->start(30);

    // 创建并显示主窗口
    MainWindow window;

    LanRelayServer *lanRelay = nullptr;
    QThread *lanThread = nullptr;
    if (AppConfig::lanWsEnabled()) {
        lanThread = new QThread(&app);
        lanRelay = new LanRelayServer();
        lanRelay->moveToThread(lanThread);
        QObject::connect(lanThread, &QThread::finished, lanRelay, &QObject::deleteLater);
        lanThread->start();
        const quint16 port = static_cast<quint16>(AppConfig::lanWsPort());
        QMetaObject::invokeMethod(lanRelay, [lanRelay, port]() {
            lanRelay->start(port);
        }, Qt::QueuedConnection);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [lanThread]() {
            if (!lanThread) return;
            lanThread->quit();
            lanThread->wait(1500);
        });
    }

    QLocalServer *instServer = new QLocalServer(&app);
    QLocalServer::removeServer("IrulerDeskpro_SingleInstance");
    instServer->listen("IrulerDeskpro_SingleInstance");
    QObject::connect(instServer, &QLocalServer::newConnection, &app, [&window, instServer]() {
        QLocalSocket *cli = instServer->nextPendingConnection();
        if (!cli) return;
        QObject::connect(cli, &QLocalSocket::readyRead, &window, [cli, &window]() {
            QByteArray data = cli->readAll();
            if (data.contains("show_main_window")) {
                auto *til = window.transparentImageList();
                if (til) { til->show(); til->raise(); }
            }
        });
    });
    QObject::connect(&window, &MainWindow::appReady, &splash, [&splash, &window, canvas, splashTimer]() {
        splashTimer->stop();
        splashTimer->deleteLater();
        QRect startRect = splash.geometry();
        NewUiWindow *target = window.transparentImageList();
        QPoint destCenter;
        if (target) {
            target->show();
            QRect tGlobal(target->pos(), target->size());
            destCenter = tGlobal.center();
        } else {
            destCenter = QPoint(startRect.left() + startRect.width() - 20, startRect.top() + startRect.height() - 20);
        }
        int ew = qMax(8, startRect.width() / 10);
        int eh = qMax(8, startRect.height() / 10);
        QRect endRect(destCenter.x() - ew / 2, destCenter.y() - eh / 2, ew, eh);
        QLabel *fly = new QLabel(nullptr);
        fly->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
        fly->setAttribute(Qt::WA_TranslucentBackground, true);
        fly->setScaledContents(true);
        fly->setPixmap(canvas);
        fly->setGeometry(startRect);
        fly->show();
        splash.close();
        auto opacity = new QGraphicsOpacityEffect(fly);
        opacity->setOpacity(1.0);
        fly->setGraphicsEffect(opacity);
        auto geomAnim = new QPropertyAnimation(fly, "geometry");
        geomAnim->setDuration(300);
        geomAnim->setStartValue(startRect);
        geomAnim->setEndValue(endRect);
        auto opAnim = new QPropertyAnimation(opacity, "opacity");
        opAnim->setDuration(300);
        opAnim->setStartValue(1.0);
        opAnim->setEndValue(0.0);
        auto group = new QParallelAnimationGroup(fly);
        group->addAnimation(geomAnim);
        group->addAnimation(opAnim);
        QObject::connect(group, &QParallelAnimationGroup::finished, [fly, opacity, group]() {
            fly->hide();
            fly->deleteLater();
            opacity->deleteLater();
            group->deleteLater();
        });
        group->start(QAbstractAnimation::DeleteWhenStopped);
    });
    // 简单的启动日志（使用 C++ 标准输出）
    // std::cout << "启动成功" << std::endl;
    
    // 不显示主窗口，只显示透明图片列表
    int result = app.exec();
    return result;
}
