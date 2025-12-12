#include "ConsoleLogger.h"
#include <QDateTime>
#include <QTextStream>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <cstdio>
#include <mutex>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace {
    static std::mutex logMutex;
    static QFile *logFile = nullptr;

    static void qtConsoleMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
        // 过滤不需要的日志
        if (msg.contains("libpng warning: iCCP: cHRM chunk does not match sRGB") ||
            msg.contains("Using Qt multimedia with FFmpeg")) {
            return;
        }

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
        QByteArray text = msg.toUtf8();
        const char *file = ctx.file ? ctx.file : "";
        const char *func = ctx.function ? ctx.function : "";
        int line = ctx.line;
        
        // 1. 输出到控制台
        FILE *out = (type == QtCriticalMsg || type == QtFatalMsg) ? stderr : stdout;
        std::fprintf(out, "[%s] [%s] %s (%s:%d %s)\n",
                     ts.toUtf8().constData(), level, text.constData(), file, line, func);
        std::fflush(out);

        // 2. 输出到文件
        std::lock_guard<std::mutex> lock(logMutex);
        if (!logFile) {
            QString logPath = QCoreApplication::applicationDirPath() + "/logs";
            QDir dir(logPath);
            if (!dir.exists()) dir.mkpath(".");
            QString filename = QString("process_%1.log").arg(QCoreApplication::applicationPid());
            logFile = new QFile(dir.filePath(filename));
            logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        }
        if (logFile && logFile->isOpen()) {
            QTextStream stream(logFile);
            stream << "[" << ts << "] [" << level << "] " << msg << " (" << file << ":" << line << " " << func << ")\n";
            stream.flush();
        }

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