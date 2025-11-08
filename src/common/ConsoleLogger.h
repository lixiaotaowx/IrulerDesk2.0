#ifndef CONSOLELOGGER_H
 #define CONSOLELOGGER_H
 
 #include <QtGlobal>
 
 namespace ConsoleLogger {
     // 在Windows上附着到父进程的控制台（如果有），以便日志输出到启动终端
     bool attachToParentConsole();
 
     // 安装Qt消息处理器，把 qDebug/qInfo/qWarning/qCritical 输出到stdout/stderr
     void installQtMessageHandler();
 }
 
 #endif // CONSOLELOGGER_H