#pragma once
#include <QWidget>
#include <QMouseEvent>
#include <QListWidget>
#include <QTimer>
#include <QLabel>

class NewUiWindow : public QWidget
{
    Q_OBJECT

public:
    explicit NewUiWindow(QWidget *parent = nullptr);
    ~NewUiWindow() override = default;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void onTimerTimeout();

private:
    void setupUi();
    
    // Dragging support
    bool m_dragging = false;
    QPoint m_dragPosition;

    QListWidget *m_listWidget = nullptr;
    QTimer *m_timer = nullptr;
    QLabel *m_videoLabel = nullptr;

    // Layout constants
    int m_cardBaseWidth;
    int m_bottomAreaHeight;
    int m_shadowSize;
    double m_aspectRatio;
    int m_cardBaseHeight;
    int m_totalItemWidth;
    int m_totalItemHeight;
    int m_imgWidth;
    int m_imgHeight;
    int m_marginX;
    int m_topAreaHeight;
    int m_marginTop;
};
