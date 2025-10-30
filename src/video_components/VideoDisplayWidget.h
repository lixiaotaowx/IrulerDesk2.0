#ifndef VIDEODISPLAYWIDGET_H
#define VIDEODISPLAYWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTimer>
#include <QPixmap>
#include <QPoint>
#include <memory>

// 前向声明
class DxvaVP9Decoder;
class WebSocketReceiver;

struct VideoStats {
    int framesReceived = 0;
    int framesDecoded = 0;
    int framesDisplayed = 0;
    double avgDecodeTime = 0.0;
    double avgEndToEndLatency = 0.0; // 端到端延迟（毫秒）
    QString connectionStatus = "Disconnected";
    QSize frameSize = QSize(0, 0);
};

class VideoDisplayWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoDisplayWidget(QWidget *parent = nullptr);
    ~VideoDisplayWidget();
    
    // 控制接口
    void startReceiving(const QString &serverUrl = "");
    void stopReceiving();
    bool isReceiving() const { return m_isReceiving; }
    
    // 发送观看请求
    void sendWatchRequest(const QString &viewerId, const QString &targetId);
    
    // 获取统计信息
    VideoStats getStats() const { return m_stats; }
    
    // 设置显示模式
    void setShowControls(bool show);
    void setAutoResize(bool autoResize) { m_autoResize = autoResize; }
    
signals:
    void connectionStatusChanged(const QString &status);
    void frameReceived();
    void statsUpdated(const VideoStats &stats);
    
public slots:
    void renderFrame(const QByteArray &frameData, const QSize &frameSize);
    void renderFrameWithTimestamp(const QByteArray &frameData, const QSize &frameSize, qint64 captureTimestamp);
    void updateConnectionStatus(const QString &status);
    void onMousePositionReceived(const QPoint &position, qint64 timestamp); // 新增：鼠标位置接收槽
    
private slots:
    void onStartStopClicked();
    void updateStatsDisplay();
    
private:
    void setupUI();
    void updateButtonText();
    void drawMouseCursor(QPixmap &pixmap, const QPoint &position); // 新增：绘制鼠标光标
    
    // UI组件
    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_controlLayout;
    QLabel *m_videoLabel;
    QPushButton *m_startStopButton;
    QLabel *m_statusLabel;
    QLabel *m_statsLabel;
    QTimer *m_statsTimer;
    
    // 控制接口
    std::unique_ptr<DxvaVP9Decoder> m_decoder;
    std::unique_ptr<WebSocketReceiver> m_receiver;
    
    // 状态
    bool m_isReceiving;
    bool m_showControls;
    bool m_autoResize;
    QString m_serverUrl;
    VideoStats m_stats;
    
    // 端到端延迟统计
    QList<double> m_latencyHistory;
    qint64 m_currentCaptureTimestamp = 0; // 当前帧的捕获时间戳
    static const int MAX_LATENCY_SAMPLES = 30; // 保持最近30帧的延迟数据
    
    // 鼠标位置相关
    QPoint m_mousePosition; // 当前鼠标位置
    qint64 m_mouseTimestamp = 0; // 鼠标位置时间戳
    bool m_hasMousePosition = false; // 是否有有效的鼠标位置数据
};

#endif // VIDEODISPLAYWIDGET_H