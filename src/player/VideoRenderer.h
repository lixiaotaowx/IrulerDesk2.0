#ifndef VIDEORENDERER_H
#define VIDEORENDERER_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QPainter>
#include <QPixmap>
#include <QImage>
#include <QSize>
#include <QMutex>
#include <QStatusBar>
#include <QMainWindow>
#include <QCloseEvent>
#include <QHash>
#include <QMap>
#include <QRect>
#include <QReadWriteLock>
#include <atomic>
#include <QDateTime>

class VideoRenderer : public QMainWindow
{
    Q_OBJECT

public:
    explicit VideoRenderer(QWidget *parent = nullptr);
    ~VideoRenderer();
    
    // 渲染帧数据
    void renderFrame(const QByteArray &frameData, const QSize &frameSize);
    
    // 设置连接状态
    void setConnectionStatus(bool connected);
    
    // 获取渲染统计信息
    struct RenderStats {
        quint64 totalFrames;
        quint64 renderedFrames;
        double averageFPS;
        QSize currentFrameSize;
    };
    RenderStats getStats() const { return m_stats; }

signals:
    void windowClosed();
    void statsUpdated(const RenderStats &stats);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void updateDisplay();
    void updateStats();

private:
    void setupUI();
    void setupStatusBar();
    QPixmap scalePixmapToFit(const QPixmap &pixmap, const QSize &targetSize);
    
    // UI组件
    QWidget *m_centralWidget;
    QVBoxLayout *m_mainLayout;
    QLabel *m_videoLabel;
    QLabel *m_statusLabel;
    QStatusBar *m_statusBar;
    
    // 渲染相关
    QPixmap m_currentFrame;
    QSize m_frameSize;
    QMutex m_frameMutex;
    
    // 状态管理
    bool m_connected;
    QTimer *m_displayTimer;
    QTimer *m_statsTimer;
    
    // 统计信息
    RenderStats m_stats;
    QList<qint64> m_frameTimes;
    qint64 m_lastFrameTime;
    
};

#endif // VIDEORENDERER_H