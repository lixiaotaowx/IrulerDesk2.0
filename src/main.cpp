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
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif
#include "common/ConsoleLogger.h"
#include "common/CrashGuard.h"
#include "MainWindow.h"
#include "ui/TransparentImageList.h"

int main(int argc, char *argv[])
{
    // 安装崩溃守护与控制台日志重定向
    CrashGuard::install();
    ConsoleLogger::attachToParentConsole();
    ConsoleLogger::installQtMessageHandler();

    QApplication app(argc, argv);
    
    QString appDir = QCoreApplication::applicationDirPath();
    app.setWindowIcon(QIcon(appDir + "/maps/logo/logo.png"));
    
    // 设置应用程序信息
    app.setApplicationName("ScreenStreamApp");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("ScreenStream");
    
    // 设置现代化样式
    app.setStyle(QStyleFactory::create("Fusion"));
    
    // 设置深色主题（含黑底白字的工具提示）
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    // 工具提示：黑背景白文字
    darkPalette.setColor(QPalette::ToolTipBase, QColor(0, 0, 0));
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
        "    color: #ffffff;\n"
        "    background-color: #000000;\n"
        "    border: 1px solid #4c4c4c;\n"
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
    QFont f = p.font();
    f.setPixelSize(20);
    p.setFont(f);
    p.setPen(QColor(255,255,255,220));
    QString t = QStringLiteral("启动中...");
    QFontMetrics fm(f);
    int tw = fm.horizontalAdvance(t);
    int th = fm.height();
    int bx = (ss.width() - tw) / 2;
    int by = ss.height() - th - 16;
    p.drawText(QPoint(bx, by), t);
    p.end();
    QSplashScreen splash(canvas);
    splash.show();
    
    // 创建并显示主窗口
    MainWindow window;
    QObject::connect(&window, &MainWindow::appReady, &splash, [&splash, &window, canvas]() {
        QRect startRect = splash.geometry();
        TransparentImageList *target = window.transparentImageList();
        QPoint destCenter;
        if (target) {
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
        geomAnim->setDuration(500);
        geomAnim->setStartValue(startRect);
        geomAnim->setEndValue(endRect);
        auto opAnim = new QPropertyAnimation(opacity, "opacity");
        opAnim->setDuration(500);
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