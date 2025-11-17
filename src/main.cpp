#include <QApplication>
#include <QIcon>
#include <QStyleFactory>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif
#include "common/ConsoleLogger.h"
#include "common/CrashGuard.h"
#include "MainWindow.h"

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
    
    // 创建并显示主窗口
    MainWindow window;
    // 简单的启动日志（使用 C++ 标准输出）
    // std::cout << "启动成功" << std::endl;
    
    // 不显示主窗口，只显示透明图片列表
    int result = app.exec();
    return result;
}