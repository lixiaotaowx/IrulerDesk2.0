#include "PerformanceMonitor.h"
#include "TileManager.h"
#include "WebSocketSender.h"
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>

PerformanceMonitor::PerformanceMonitor(QObject *parent)
    : QObject(parent)
    , m_tileManager(nullptr)
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
    
    qDebug() << "[PerformanceMonitor] 性能监控管理器已初始化";
}

PerformanceMonitor::~PerformanceMonitor()
{
    stopMonitoring();
    qDebug() << "[PerformanceMonitor] 性能监控管理器已销毁";
}

void PerformanceMonitor::registerTileManager(TileManager *tileManager)
{
    QMutexLocker locker(&m_mutex);
    m_tileManager = tileManager;
    qDebug() << "[PerformanceMonitor] 已注册TileManager";
}

void PerformanceMonitor::registerWebSocketSender(WebSocketSender *sender)
{
    QMutexLocker locker(&m_mutex);
    m_webSocketSender = sender;
    qDebug() << "[PerformanceMonitor] 已注册WebSocketSender";
}

void PerformanceMonitor::startMonitoring(int intervalMs)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_isMonitoring) {
        qDebug() << "[PerformanceMonitor] 监控已在运行中";
        return;
    }
    
    m_reportInterval = intervalMs;
    m_monitoringStartTime = QDateTime::currentMSecsSinceEpoch();
    m_reportCount = 0;
    m_isMonitoring = true;
    
    m_reportTimer->start(m_reportInterval);
    
    qDebug() << "[PerformanceMonitor] 开始性能监控，报告间隔:" << intervalMs << "ms";
    
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
    
    qint64 totalTime = QDateTime::currentMSecsSinceEpoch() - m_monitoringStartTime;
    qDebug() << "[PerformanceMonitor] 停止性能监控，总运行时间:" << formatTime(totalTime)
             << "总报告次数:" << m_reportCount;
}

void PerformanceMonitor::setReportInterval(int intervalMs)
{
    QMutexLocker locker(&m_mutex);
    m_reportInterval = intervalMs;
    
    if (m_isMonitoring) {
        m_reportTimer->setInterval(intervalMs);
    }
    
    qDebug() << "[PerformanceMonitor] 报告间隔已设置为:" << intervalMs << "ms";
}

void PerformanceMonitor::generateReport()
{
    QMutexLocker locker(&m_mutex);
    
    qDebug() << "==================== 性能监控报告 ====================";
    qDebug() << "报告时间:" << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    
    if (m_isMonitoring) {
        qint64 runningTime = QDateTime::currentMSecsSinceEpoch() - m_monitoringStartTime;
        qDebug() << "监控运行时间:" << formatTime(runningTime);
        qDebug() << "报告次数:" << m_reportCount;
    }
    
    outputTileManagerStats();
    outputWebSocketSenderStats();
    outputSummaryStats();
    
    qDebug() << "=====================================================";
}

void PerformanceMonitor::generateDetailedReport()
{
    QMutexLocker locker(&m_mutex);
    
    qDebug() << "================== 详细性能监控报告 ==================";
    qDebug() << "报告时间:" << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    
    outputSystemInfo();
    outputTileManagerStats();
    outputWebSocketSenderStats();
    outputSummaryStats();
    
    qDebug() << "====================================================";
}

void PerformanceMonitor::resetAllStats()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_tileManager) {
        m_tileManager->resetPerformanceStats();
    }
    
    if (m_webSocketSender) {
        m_webSocketSender->resetSenderStats();
    }
    
    m_reportCount = 0;
    m_monitoringStartTime = QDateTime::currentMSecsSinceEpoch();
    
    qDebug() << "[PerformanceMonitor] 所有性能统计已重置";
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
        qDebug() << "[PerformanceMonitor] 启用文件日志记录:" << m_logFilePath;
    } else {
        qDebug() << "[PerformanceMonitor] 禁用文件日志记录";
    }
}

void PerformanceMonitor::onPerformanceReport()
{
    m_reportCount++;
    
    if (m_verboseLogging) {
        generateReport();
    } else {
        // 简化报告
        qDebug() << "[PerformanceMonitor] 性能报告 #" << m_reportCount;
        outputSummaryStats();
    }
}

void PerformanceMonitor::outputSystemInfo()
{
    qDebug() << "--- 系统信息 ---";
    qDebug() << "Qt版本:" << QT_VERSION_STR;
    qDebug() << "当前时间:" << QDateTime::currentDateTime().toString();
    
    if (m_isMonitoring) {
        qint64 uptime = QDateTime::currentMSecsSinceEpoch() - m_monitoringStartTime;
        qDebug() << "监控运行时间:" << formatTime(uptime);
    }
}

void PerformanceMonitor::outputTileManagerStats()
{
    if (!m_tileManager) {
        qDebug() << "--- TileManager统计 ---";
        qDebug() << "TileManager未注册";
        return;
    }
    
    TileManager::PerformanceStats stats = m_tileManager->getPerformanceStats();
    
    qDebug() << "--- TileManager统计 ---";
    qDebug() << "总检测次数:" << stats.totalDetections;
    qDebug() << "总变化瓦片数:" << stats.totalChangedTiles;
    qDebug() << "瓦片变化率:" << QString::number(stats.changeRate, 'f', 2) << "%";
    qDebug() << "检测帧率:" << QString::number(stats.detectionFPS, 'f', 1) << "FPS";
    qDebug() << "平均检测时间:" << stats.averageDetectionTime << "ms";
    qDebug() << "最大检测时间:" << stats.maxDetectionTime << "ms";
    qDebug() << "最小检测时间:" << (stats.minDetectionTime == LLONG_MAX ? 0 : stats.minDetectionTime) << "ms";
    qDebug() << "平均哈希计算时间:" << stats.averageHashTime << "ms";
    qDebug() << "平均CRC计算时间:" << stats.averageCrcTime << "ms";
    qDebug() << "总检测时间:" << formatTime(stats.totalDetectionTime);
    qDebug() << "总哈希计算时间:" << formatTime(stats.totalHashCalculationTime);
    qDebug() << "总CRC计算时间:" << formatTime(stats.totalCrcCalculationTime);
}

void PerformanceMonitor::outputWebSocketSenderStats()
{
    if (!m_webSocketSender) {
        qDebug() << "--- WebSocketSender统计 ---";
        qDebug() << "WebSocketSender未注册";
        return;
    }
    
    WebSocketSender::SenderStats stats = m_webSocketSender->getSenderStats();
    
    qDebug() << "--- WebSocketSender统计 ---";
    qDebug() << "总发送瓦片数:" << stats.totalTilesSent;
    qDebug() << "总发送字节数:" << formatBytes(stats.totalBytesSent);
    qDebug() << "总编码操作数:" << stats.totalEncodingOperations;
    qDebug() << "平均编码时间:" << stats.averageEncodingTime << "ms";
    qDebug() << "最大编码时间:" << stats.maxEncodingTime << "ms";
    qDebug() << "最小编码时间:" << (stats.minEncodingTime == LLONG_MAX ? 0 : stats.minEncodingTime) << "ms";
    qDebug() << "平均发送时间:" << stats.averageSendingTime << "ms";
    qDebug() << "最大发送时间:" << stats.maxSendingTime << "ms";
    qDebug() << "最小发送时间:" << (stats.minSendingTime == LLONG_MAX ? 0 : stats.minSendingTime) << "ms";
    qDebug() << "发送速率:" << formatRate(stats.sendingRate, "B/s");
    qDebug() << "瓦片传输速率:" << formatRate(stats.tileTransmissionRate, "tiles/s");
    qDebug() << "重连次数:" << stats.reconnectionCount;
    qDebug() << "总断线时间:" << formatTime(stats.totalDowntime);
    qDebug() << "总编码时间:" << formatTime(stats.totalEncodingTime);
    qDebug() << "总发送时间:" << formatTime(stats.totalSendingTime);
    qDebug() << "总序列化时间:" << formatTime(stats.totalSerializationTime);
}

void PerformanceMonitor::outputSummaryStats()
{
    qDebug() << "--- 性能摘要 ---";
    
    if (m_tileManager) {
        TileManager::PerformanceStats tileStats = m_tileManager->getPerformanceStats();
        qDebug() << "瓦片检测: FPS=" << QString::number(tileStats.detectionFPS, 'f', 1)
                 << "变化率=" << QString::number(tileStats.changeRate, 'f', 1) << "%"
                 << "平均耗时=" << tileStats.averageDetectionTime << "ms";
    }
    
    if (m_webSocketSender) {
        WebSocketSender::SenderStats senderStats = m_webSocketSender->getSenderStats();
        qDebug() << "网络发送: 速率=" << formatRate(senderStats.sendingRate / 1024.0, "KB/s")
                 << "瓦片速率=" << formatRate(senderStats.tileTransmissionRate, "tiles/s")
                 << "平均发送耗时=" << senderStats.averageSendingTime << "ms";
    }
    
    if (m_isMonitoring) {
        qint64 uptime = QDateTime::currentMSecsSinceEpoch() - m_monitoringStartTime;
        qDebug() << "系统运行: 时间=" << formatTime(uptime) << "报告次数=" << m_reportCount;
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