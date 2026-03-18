#pragma once

#include <QGraphicsView>

class NodeGraphView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit NodeGraphView(QWidget *parent = nullptr);
    NodeGraphView(QGraphicsScene *scene, QWidget *parent = nullptr);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void init();
    
    bool m_isPanning = false;
    QPoint m_lastMousePos;
};
