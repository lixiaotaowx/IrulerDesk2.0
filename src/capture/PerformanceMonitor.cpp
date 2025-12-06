#include "PerformanceMonitor.h"
#include "WebSocketSender.h"
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>

PerformanceMonitor::PerformanceMonitor(QObject *parent)
    : QObject(parent)
    , m_webSocketSender(nullptr)
    , m_reportTimer(new QTimer(this))
    , m_reportInterval(30000) // 默认30秒
    , m_isMonitoring(false)
    , m_monitoringStartTime(0)
    , m_reportCount(0)
    , m_verboseLogging(true)
    , m_logToFile(false)
{
    // 连接定时器信号
    connect(m_reportTimer, &QTimer::timeout, this, &PerformanceMonitor::onPerformanceReport);
}

PerformanceMonitor::~PerformanceMonitor()
{
    stopMonitoring();
}

void PerformanceMonitor::registerWebSocketSender(WebSocketSender *sender)
{
    QMutexLocker locker(&m_mutex);
    m_webSocketSender = sender;
}

void PerformanceMonitor::startMonitoring(int intervalMs)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_isMonitoring) {
        return;
    }
    
    m_reportInterval = intervalMs;
    m_monitoringStartTime = QDateTime::currentMSecsSinceEpoch();
    m_reportCount = 0;
    m_isMonitoring = true;
    
    m_reportTimer->start(m_reportInterval);
    // 输出初始系统信息
    outputSystemInfo();
}

void PerformanceMonitor::stopMonitoring()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_isMonitoring) {
        return;
    }
    
    m_reportTimer->stop();
    m_isMonitoring = false;
}

void PerformanceMonitor::setReportInterval(int intervalMs)
{
    QMutexLocker locker(&m_mutex);
    m_reportInterval = intervalMs;
    
    if (m_isMonitoring) {
        m_reportTimer->setInterval(intervalMs);
    }
    
    // 报告间隔配置更新
}

void PerformanceMonitor::generateReport()
{
    QMutexLocker locker(&m_mutex);
    
    // 性能监控报告生成（静默）
    
    if (m_isMonitoring) {
        // 保留运行时间计算但不输出
        (void)QDateTime::currentMSecsSinceEpoch();
    }
    
    outputWebSocketSenderStats();
    outputSummaryStats();
}

void PerformanceMonitor::generateDetailedReport()
{
    QMutexLocker locker(&m_mutex);
    
    outputSystemInfo();
    outputWebSocketSenderStats();
    outputSummaryStats();
}

void PerformanceMonitor::resetAllStats()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_webSocketSender) {
        m_webSocketSender->resetSenderStats();
    }
    
    m_reportCount = 0;
    m_monitoringStartTime = QDateTime::currentMSecsSinceEpoch();
}

void PerformanceMonitor::setLogToFile(bool enabled, const QString &filePath)
{
    QMutexLocker locker(&m_mutex);
    m_logToFile = enabled;
    
    if (enabled) {
        if (filePath.isEmpty()) {
            QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            QDir().mkpath(appDataPath);
            m_logFilePath = appDataPath + "/performance_log.txt";
        } else {
            m_logFilePath = filePath;
        }
    } else {
    }
}

void PerformanceMonitor::onPerformanceReport()
{
    m_reportCount++;
    
    if (m_verboseLogging) {
        generateReport();
    } else {
        outputSummaryStats();
    }
}

void PerformanceMonitor::outputSystemInfo()
{
    if (m_isMonitoring) {
        // 保留运行时间计算但不输出
        (void)QDateTime::currentMSecsSinceEpoch();
    }
}

void PerformanceMonitor::outputWebSocketSenderStats()
{
    if (!m_webSocketSender) {
        return;
    }
    
    WebSocketSender::SenderStats stats = m_webSocketSender->getSenderStats();
}

void PerformanceMonitor::outputSummaryStats()
{
    if (m_webSocketSender) {
        WebSocketSender::SenderStats senderStats = m_webSocketSender->getSenderStats();
    }
    
    if (m_isMonitoring) {
        // 保留运行时间计算但不输出
        (void)QDateTime::currentMSecsSinceEpoch();
    }
}

QString PerformanceMonitor::formatBytes(qint64 bytes) const
{
    if (bytes < 1024) {
        return QString::number(bytes) + " B";
    } else if (bytes < 1024 * 1024) {
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    } else if (bytes < 1024 * 1024 * 1024) {
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    } else {
        return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GB";
    }
}

QString PerformanceMonitor::formatTime(qint64 milliseconds) const
{
    if (milliseconds < 1000) {
        return QString::number(milliseconds) + "ms";
    } else if (milliseconds < 60000) {
        return QString::number(milliseconds / 1000.0, 'f', 1) + "s";
    } else if (milliseconds < 3600000) {
        int minutes = milliseconds / 60000;
        int seconds = (milliseconds % 60000) / 1000;
        return QString("%1m%2s").arg(minutes).arg(seconds);
    } else {
        int hours = milliseconds / 3600000;
        int minutes = (milliseconds % 3600000) / 60000;
        return QString("%1h%2m").arg(hours).arg(minutes);
    }
}

QString PerformanceMonitor::formatRate(double rate, const QString &unit) const
{
    if (rate < 1000) {
        return QString::number(rate, 'f', 1) + " " + unit;
    } else if (rate < 1000000) {
        return QString::number(rate / 1000.0, 'f', 1) + " K" + unit;
    } else {
        return QString::number(rate / 1000000.0, 'f', 1) + " M" + unit;
    }
}
