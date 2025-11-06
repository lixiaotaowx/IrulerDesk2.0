#ifndef PERFORMANCEMONITOR_H
#define PERFORMANCEMONITOR_H

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QMutex>

// 前向声明
class TileManager;
class WebSocketSender;

/**
 * @brief 性能监控管理器
 * 
 * 负责定期收集和输出系统各组件的性能统计信息，
 * 提供统一的性能监控和报告功能。
 */
class PerformanceMonitor : public QObject
{
    Q_OBJECT

public:
    explicit PerformanceMonitor(QObject *parent = nullptr);
    ~PerformanceMonitor();

    // 注册监控组件
    void registerTileManager(TileManager *tileManager);
    void registerWebSocketSender(WebSocketSender *sender);

    // 监控控制
    void startMonitoring(int intervalMs = 30000); // 默认30秒输出一次
    void stopMonitoring();
    void setReportInterval(int intervalMs);

    // 手动触发报告
    void generateReport();
    void generateDetailedReport();

    // 统计重置
    void resetAllStats();

    // 配置选项
    void setVerboseLogging(bool enabled) { m_verboseLogging = enabled; }
    void setLogToFile(bool enabled, const QString &filePath = "");

public slots:
    void onPerformanceReport(); // 定期性能报告槽函数

private:
    void outputSystemInfo();
    void outputTileManagerStats();
    void outputWebSocketSenderStats();
    void outputSummaryStats();
    
    QString formatBytes(qint64 bytes) const;
    QString formatTime(qint64 milliseconds) const;
    QString formatRate(double rate, const QString &unit) const;

private:
    // 监控组件
    TileManager *m_tileManager;
    WebSocketSender *m_webSocketSender;

    // 定时器
    QTimer *m_reportTimer;
    int m_reportInterval;

    // 监控状态
    bool m_isMonitoring;
    qint64 m_monitoringStartTime;
    quint64 m_reportCount;

    // 配置选项
    bool m_verboseLogging;
    bool m_logToFile;
    QString m_logFilePath;

    // 线程安全
    mutable QMutex m_mutex;
};

#endif // PERFORMANCEMONITOR_H