#include "NodeGraphView.h"
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScrollBar>

NodeGraphView::NodeGraphView(QWidget *parent)
    : QGraphicsView(parent)
{
    init();
}

NodeGraphView::NodeGraphView(QGraphicsScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{
    init();
}

void NodeGraphView::init()
{
    setRenderHint(QPainter::Antialiasing);
    setCacheMode(QGraphicsView::CacheBackground);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setDragMode(QGraphicsView::RubberBandDrag);
    
    // Hide scrollbars for infinite canvas feel
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
}

void NodeGraphView::wheelEvent(QWheelEvent *event)
{
    // Blender-style Zoom
    if (event->angleDelta().y() > 0) {
        scale(1.1, 1.1);
    } else {
        scale(1.0 / 1.1, 1.0 / 1.1);
    }
}

void NodeGraphView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_isPanning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        QGraphicsView::mousePressEvent(event);
    }
}

void NodeGraphView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isPanning) {
        // Calculate delta
        QPoint delta = event->pos() - m_lastMousePos;
        
        // Update scrollbars
        QScrollBar *hBar = horizontalScrollBar();
        QScrollBar *vBar = verticalScrollBar();
        hBar->setValue(hBar->value() - delta.x());
        vBar->setValue(vBar->value() - delta.y());
        
        // Update last pos
        m_lastMousePos = event->pos();
        event->accept();
    } else {
        QGraphicsView::mouseMoveEvent(event);
    }
}

void NodeGraphView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_isPanning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
    } else {
        QGraphicsView::mouseReleaseEvent(event);
    }
}
