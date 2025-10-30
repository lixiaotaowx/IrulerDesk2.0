#include <QApplication>
#include <QStyleFactory>
#include <QLoggingCategory>
#include <QDebug>
#include <iostream>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#endif
#include "MainWindow.h"

int main(int argc, char *argv[])
{
#ifdef _WIN32
    // 在Windows上分配控制台以显示调试输出
    if (AllocConsole()) {
        freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
        freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);
        freopen_s((FILE**)stdin, "CONIN$", "r", stdin);
        std::cout.clear();
        std::cerr.clear();
        std::cin.clear();
        
        // 设置控制台编码为UTF-8以正确显示中文
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }
#endif

    QApplication app(argc, argv);
    
    // 禁用Qt内部调试输出，只保留我们的应用程序日志
    QLoggingCategory::setFilterRules("qt.*=false");
    
    qDebug() << "[Main] ========== 应用程序启动 ==========";
    
    // 设置应用程序信息
    qDebug() << "[Main] 设置应用程序信息...";
    app.setApplicationName("ScreenStreamApp");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("ScreenStream");
    
    // 设置现代化样式
    qDebug() << "[Main] 设置UI样式...";
    app.setStyle(QStyleFactory::create("Fusion"));
    
    // 设置深色主题
    qDebug() << "[Main] 应用深色主题...";
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(darkPalette);
    qDebug() << "[Main] 深色主题设置完成";
    
    // 创建并显示主窗口
    qDebug() << "[Main] 创建主窗口...";
    MainWindow window;
    qDebug() << "[Main] 主窗口创建成功";
    
    // 不显示主窗口，只显示透明图片列表
    qDebug() << "[Main] 主窗口已创建但不显示，只显示透明图片列表";
    
    qDebug() << "[Main] 进入事件循环...";
    int result = app.exec();
    qDebug() << "[Main] 应用程序退出，返回码:" << result;
    return result;
}