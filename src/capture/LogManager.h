#pragma once

#include <QDebug>
#include <QMutex>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDateTime>

/**
 * 日志管理器 - 控制日志输出级别和性能优化
 */
class LogManager
{
public:
    enum LogLevel {
        Silent = 0,     // 静默模式 - 不输出日志
        Error = 1,      // 仅错误日志
        Warning = 2,    // 错误和警告日志
        Info = 3,       // 错误、警告和信息日志
        Debug = 4,      // 所有日志（包括调试）
        Verbose = 5     // 详细模式 - 包括高频日志
    };

    static LogManager& instance();
    
    // 设置日志级别
    void setLogLevel(LogLevel level);
    LogLevel getLogLevel() const;
    
    // 日志输出控制
    void setLogOutputEnabled(bool enabled);
    bool isLogOutputEnabled() const;
    
    // 日志文件管理
    void setLogFile(const QString& filePath);
    void setMaxFileSize(qint64 maxSize); // 设置最大文件大小（字节）
    void setMaxFileCount(int maxCount);  // 设置最大文件数量
    
    // 日志输出方法
    bool shouldLog(LogLevel level) const;
    
    // 高频日志控制（如每帧日志）
    bool shouldLogVerbose() const;
    
    // 文件大小检查（公开方法，供宏调用）
    void checkFileSize();
    
    // 性能优化：减少字符串构造
    template<typename... Args>
    void log(LogLevel level, const QString& category, const QString& message, Args... args) {
        if (shouldLog(level)) {
            QMutexLocker locker(&m_mutex);
            qDebug().noquote() << QString("[%1] %2").arg(category, message);
        }
    }

private:
    LogManager() : m_logLevel(Info), m_logOutputEnabled(true), 
                   m_maxFileSize(10 * 1024 * 1024), m_maxFileCount(3) {} // 默认10MB，3个文件
    
    // 文件轮转管理
    void rotateLogFile();
    void cleanupOldLogFiles();
    
    LogLevel m_logLevel;
    bool m_logOutputEnabled;
    QString m_logFilePath;
    qint64 m_maxFileSize;
    int m_maxFileCount;
    mutable QMutex m_mutex;
};

// 便捷宏定义
#define LOG_ERROR(category, message) \
    do { \
        LogManager& logMgr = LogManager::instance(); \
        if (logMgr.isLogOutputEnabled() && logMgr.shouldLog(LogManager::Error)) { \
            qDebug() << "[" << category << "] " << message; \
            logMgr.checkFileSize(); \
        } \
    } while(0)

#define LOG_WARNING(category, message) \
    do { \
        LogManager& logMgr = LogManager::instance(); \
        if (logMgr.isLogOutputEnabled() && logMgr.shouldLog(LogManager::Warning)) { \
            qWarning() << "[" << category << "] " << message; \
            logMgr.checkFileSize(); \
        } \
    } while(0)

#define LOG_INFO(category, message) \
    do { \
        LogManager& logMgr = LogManager::instance(); \
        if (logMgr.isLogOutputEnabled() && logMgr.shouldLog(LogManager::Info)) { \
            qDebug() << "[" << category << "] " << message; \
            logMgr.checkFileSize(); \
        } \
    } while(0)

#define LOG_DEBUG(category, message) \
    do { \
        LogManager& logMgr = LogManager::instance(); \
        if (logMgr.isLogOutputEnabled() && logMgr.shouldLog(LogManager::Debug)) { \
            qDebug() << "[" << category << "] " << message; \
            logMgr.checkFileSize(); \
        } \
    } while(0)

#define LOG_VERBOSE(category, message) \
    do { \
        LogManager& logMgr = LogManager::instance(); \
        if (logMgr.isLogOutputEnabled() && logMgr.shouldLog(LogManager::Verbose)) { \
            qDebug() << "[" << category << "] " << message; \
            logMgr.checkFileSize(); \
        } \
    } while(0)