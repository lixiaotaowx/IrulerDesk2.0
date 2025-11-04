#include "LogManager.h"
#include <QFileInfo>
#include <QStandardPaths>

LogManager& LogManager::instance()
{
    static LogManager instance;
    return instance;
}

void LogManager::setLogLevel(LogLevel level)
{
    QMutexLocker locker(&m_mutex);
    m_logLevel = level;
    
    QString levelName;
    switch (level) {
        case Silent: levelName = "静默模式"; break;
        case Error: levelName = "仅错误"; break;
        case Warning: levelName = "错误+警告"; break;
        case Info: levelName = "信息级别"; break;
        case Debug: levelName = "调试级别"; break;
        case Verbose: levelName = "详细模式"; break;
    }
    
    qDebug() << "[LogManager] 日志级别已设置为:" << levelName << "(" << level << ")";
}

LogManager::LogLevel LogManager::getLogLevel() const
{
    QMutexLocker locker(&m_mutex);
    return m_logLevel;
}

bool LogManager::shouldLog(LogLevel level) const
{
    QMutexLocker locker(&m_mutex);
    return level <= m_logLevel;
}

bool LogManager::shouldLogVerbose() const
{
    return shouldLog(Verbose);
}

void LogManager::setLogOutputEnabled(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_logOutputEnabled = enabled;
    qDebug() << "[LogManager] 日志输出已" << (enabled ? "启用" : "禁用");
}

bool LogManager::isLogOutputEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_logOutputEnabled;
}

void LogManager::setLogFile(const QString& filePath)
{
    QMutexLocker locker(&m_mutex);
    m_logFilePath = filePath;
    
    // 确保日志目录存在
    QFileInfo fileInfo(filePath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    qDebug() << "[LogManager] 日志文件路径设置为:" << filePath;
}

void LogManager::setMaxFileSize(qint64 maxSize)
{
    QMutexLocker locker(&m_mutex);
    m_maxFileSize = maxSize;
    qDebug() << "[LogManager] 最大日志文件大小设置为:" << (maxSize / 1024 / 1024) << "MB";
}

void LogManager::setMaxFileCount(int maxCount)
{
    QMutexLocker locker(&m_mutex);
    m_maxFileCount = maxCount;
    qDebug() << "[LogManager] 最大日志文件数量设置为:" << maxCount;
}

void LogManager::checkFileSize()
{
    if (m_logFilePath.isEmpty()) return;
    
    QFileInfo fileInfo(m_logFilePath);
    if (fileInfo.exists() && fileInfo.size() > m_maxFileSize) {
        rotateLogFile();
    }
}

void LogManager::rotateLogFile()
{
    if (m_logFilePath.isEmpty()) return;
    
    QFileInfo fileInfo(m_logFilePath);
    QString baseName = fileInfo.completeBaseName();
    QString suffix = fileInfo.suffix();
    QString dir = fileInfo.absolutePath();
    
    // 轮转现有文件
    for (int i = m_maxFileCount - 1; i >= 1; i--) {
        QString oldFile = QString("%1/%2.%3.%4").arg(dir, baseName).arg(i).arg(suffix);
        QString newFile = QString("%1/%2.%3.%4").arg(dir, baseName).arg(i + 1).arg(suffix);
        
        if (QFile::exists(oldFile)) {
            QFile::remove(newFile); // 删除目标文件（如果存在）
            QFile::rename(oldFile, newFile);
        }
    }
    
    // 将当前文件重命名为 .1
    QString backupFile = QString("%1/%2.1.%3").arg(dir, baseName, suffix);
    QFile::remove(backupFile); // 删除可能存在的备份文件
    QFile::rename(m_logFilePath, backupFile);
    
    // 清理超出数量限制的文件
    cleanupOldLogFiles();
    
    qDebug() << "[LogManager] 日志文件已轮转，当前文件:" << m_logFilePath;
}

void LogManager::cleanupOldLogFiles()
{
    if (m_logFilePath.isEmpty()) return;
    
    QFileInfo fileInfo(m_logFilePath);
    QString baseName = fileInfo.completeBaseName();
    QString suffix = fileInfo.suffix();
    QString dir = fileInfo.absolutePath();
    
    // 删除超出数量限制的文件
    for (int i = m_maxFileCount + 1; i <= m_maxFileCount + 10; i++) {
        QString oldFile = QString("%1/%2.%3.%4").arg(dir, baseName).arg(i).arg(suffix);
        if (QFile::exists(oldFile)) {
            QFile::remove(oldFile);
            qDebug() << "[LogManager] 已删除旧日志文件:" << oldFile;
        }
    }
}