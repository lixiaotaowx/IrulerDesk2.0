#include "ConsoleLogger.h"
 #include <QDateTime>
 #include <QTextStream>
 #include <cstdio>
 
 #ifdef _WIN32
 #define NOMINMAX
 #include <windows.h>
 #include <io.h>
 #include <fcntl.h>
 #endif
 
 namespace {
    static void qtConsoleMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
#ifdef DISABLE_ALL_LOGS
        // 静音非致命日志，保留 Critical/Fatal
        if (type == QtDebugMsg || type == QtInfoMsg || type == QtWarningMsg) {
            return;
        }
#endif
        const char *level = "DEBUG";
        switch (type) {
            case QtDebugMsg: level = "DEBUG"; break;
            case QtInfoMsg: level = "INFO"; break;
            case QtWarningMsg: level = "WARN"; break;
            case QtCriticalMsg: level = "ERROR"; break;
            case QtFatalMsg: level = "FATAL"; break;
        }
        QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        QByteArray text = msg.toLocal8Bit();
        const char *file = ctx.file ? ctx.file : "";
        const char *func = ctx.function ? ctx.function : "";
        int line = ctx.line;
        FILE *out = (type == QtCriticalMsg || type == QtFatalMsg) ? stderr : stdout;
        std::fprintf(out, "[%s] [%s] %s (%s:%d %s)\n",
                     ts.toUtf8().constData(), level, text.constData(), file, line, func);
        std::fflush(out);
        if (type == QtFatalMsg) {
            // 保持默认行为
            std::abort();
        }
    }
 }
 
 namespace ConsoleLogger {
 
 bool attachToParentConsole() {
 #ifdef _WIN32
     if (AttachConsole(ATTACH_PARENT_PROCESS)) {
         FILE *fp;
         freopen_s(&fp, "CONOUT$", "w", stdout);
         freopen_s(&fp, "CONOUT$", "w", stderr);
         freopen_s(&fp, "CONIN$", "r", stdin);
         setvbuf(stdout, nullptr, _IONBF, 0);
         setvbuf(stderr, nullptr, _IONBF, 0);
         SetConsoleOutputCP(CP_UTF8);
         SetConsoleCP(CP_UTF8);
         return true;
     }
     return false;
 #else
     return true; // 非Windows默认即附着到终端
 #endif
 }
 
 void installQtMessageHandler() {
     qInstallMessageHandler(qtConsoleMessageHandler);
 }
 
 }