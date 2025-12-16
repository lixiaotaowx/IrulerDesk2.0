#ifndef MOUSECAPTURE_H
#define MOUSECAPTURE_H

#include <QObject>
#include <QPoint>
#include <QSize>
#include <QRect>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>

#ifdef _WIN32
#include <windows.h>
#endif

class MouseCapture : public QObject
{
    Q_OBJECT

public:
    explicit MouseCapture(QObject *parent = nullptr);
    ~MouseCapture();
    
    void startCapture();
    void stopCapture();
    
    // 设置捕获区域和目标分辨率（处理坐标偏移和缩放）
    void setScreenRect(const QRect &sourceRect, const QSize &targetSize);

    QPoint getCurrentPosition() const;
    bool isCapturing() const { return m_isCapturing; }
    
signals:
    void mousePositionChanged(const QPoint &position);
    void mousePositionMessage(const QString &jsonMessage);

private slots:
    void checkMousePosition();

private:
    QTimer *m_captureTimer;
    QPoint m_lastPosition;
    bool m_isCapturing;
    int m_updateInterval; // 更新间隔（毫秒）
    
    // 缩放和偏移相关
    QRect m_sourceRect; // 源屏幕物理区域（包含偏移）
    QSize m_targetSize;
    bool m_needScaling;

    QPoint getSystemMousePosition() const;
    QString createMousePositionMessage(const QPoint &position);
};

#endif // MOUSECAPTURE_H