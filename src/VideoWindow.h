#ifndef VIDEOWINDOW_H
#define VIDEOWINDOW_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMouseEvent>
#include <QApplication>
#include <QIcon>
#include "video_components/VideoDisplayWidget.h"

class VideoWindow : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWindow(QWidget *parent = nullptr);
    ~VideoWindow();
    
    // 获取视频显示组件
    VideoDisplayWidget* getVideoDisplayWidget() const;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void onMinimizeClicked();
    void onMaximizeClicked();
    void onCloseClicked();
    void toggleFullscreen();
    void onMicToggled(bool checked);

private:
    void setupUI();
    void setupCustomTitleBar();
    
    // UI组件
    QVBoxLayout *m_mainLayout;
    QWidget *m_titleBar;
    QHBoxLayout *m_titleBarLayout;
    QLabel *m_titleLabel;
    QPushButton *m_minimizeButton;
    QPushButton *m_maximizeButton;
    QPushButton *m_closeButton;
    QPushButton *m_micButton;
    QIcon m_micIconOn;
    QIcon m_micIconOff;
    VideoDisplayWidget *m_videoDisplayWidget;
    
    // 窗口拖拽相关
    bool m_dragging;
    QPoint m_dragPosition;
    bool m_isMaximized;
    QRect m_normalGeometry;
};

#endif // VIDEOWINDOW_H