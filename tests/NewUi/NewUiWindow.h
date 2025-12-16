#pragma once
#include <QWidget>
#include <QMouseEvent>

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

private:
    void setupUi();
    
    // Dragging support
    bool m_dragging = false;
    QPoint m_dragPosition;
};
