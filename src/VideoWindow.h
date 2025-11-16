#ifndef VIDEOWINDOW_H
#define VIDEOWINDOW_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QMouseEvent>
#include <QApplication>
#include <QIcon>
#include <QSlider>
#include <QTimer>
#include "video_components/VideoDisplayWidget.h"
#include "ui/ColorCircleButton.h"

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
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onMinimizeClicked();
    void onMaximizeClicked();
    void onCloseClicked();
    void toggleFullscreen();
    void onMicToggled(bool checked);
    void onSpeakerToggled(bool checked);
    void onColorClicked();

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
    QWidget *m_toolBar;
    QHBoxLayout *m_toolBarLayout;
    QPushButton *m_penButton;
    QPushButton *m_rectButton;
    QPushButton *m_circleButton;
    QPushButton *m_textButton;
    QPushButton *m_eraserButton;
    QPushButton *m_undoButton;
    QPushButton *m_likeButton;
    ColorCircleButton *m_colorButton;
    QPushButton *m_micButton;
    QPushButton *m_speakerButton;
    QSlider *m_textSizeSlider;
    QTimer *m_textSizeLongPressTimer;
    QLabel *m_textSizeFloatLabel;
    QSlider *m_volumeSlider;
    QTimer *m_volumeLongPressTimer;
    QIcon m_micIconOn;
    QIcon m_micIconOff;
    QIcon m_speakerIconOn;
    QIcon m_speakerIconOff;
    VideoDisplayWidget *m_videoDisplayWidget;
    
    // 窗口拖拽相关
    bool m_dragging;
    QPoint m_dragPosition;
    bool m_isMaximized;
    QRect m_normalGeometry;
    void updateColorButtonVisual(int colorId);
    void updateTextSizeFloatLabelPos();
    bool m_volumeDragActive = false;
    QPoint m_volumeDragStartPos;
    int m_volumeDragStartValue = 100;
    bool m_leftPressing = false;
    bool m_volumeTargetMic = false;
    int m_textFontSize = 16;
    bool m_textSizeDragActive = false;
    QPoint m_textSizeDragStartPos;
    int m_textSizeDragStartValue = 16;
};

#endif // VIDEOWINDOW_H