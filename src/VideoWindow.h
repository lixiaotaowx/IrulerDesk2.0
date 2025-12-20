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
#include "ui/RainbowToolButton.h"

class VideoWindow : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWindow(QWidget *parent = nullptr);
    ~VideoWindow();
    
    // 获取视频显示组件
    VideoDisplayWidget* getVideoDisplayWidget() const;
    void setMicChecked(bool checked);
    void setMicCheckedSilently(bool checked);
    void setSpeakerChecked(bool checked);
    bool isMicChecked() const;
    bool isSpeakerChecked() const;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

signals:
    void avatarUpdateReceived(const QString &userId, int iconId);
    void receivingStopped(const QString &viewerId, const QString &targetId);
    void micToggled(bool checked);
    void speakerToggled(bool checked);

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
    RainbowToolButton *m_penButton;
    RainbowToolButton *m_rectButton;
    RainbowToolButton *m_circleButton;
    RainbowToolButton *m_arrowButton;
    RainbowToolButton *m_textButton;
    RainbowToolButton *m_eraserButton;
    QPushButton *m_undoButton;
    QPushButton *m_cameraButton;
    QPushButton *m_snippetButton;
    QPushButton *m_clearButton;
    ColorCircleButton *m_colorButton;
    QPushButton *m_micButton;
    QPushButton *m_speakerButton;
    QSlider *m_textSizeSlider = nullptr;
    QTimer *m_textSizeLongPressTimer = nullptr;
    QLabel *m_textSizeFloatLabel = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QTimer *m_volumeLongPressTimer = nullptr;
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
    QLabel *m_clipToast = nullptr;
    void showClipboardToast();
    QWidget *m_fullscreenBar = nullptr;
    QHBoxLayout *m_fullscreenBarLayout = nullptr;
    QWidget *m_fullscreenPill = nullptr;
    QHBoxLayout *m_fullscreenPillLayout = nullptr;
    QWidget *m_titleCenter = nullptr;
    QHBoxLayout *m_titleCenterLayout = nullptr;
    void attachToolbarToFullscreenBar();
    void detachToolbarToTitleBar();
};

#endif // VIDEOWINDOW_H
