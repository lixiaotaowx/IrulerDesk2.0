#include "GraphEdge.h"
#include "GraphSocket.h"
#include "GraphNode.h"

#include <QPainter>
#include <QtMath>

GraphEdge::GraphEdge(GraphSocket *source, GraphSocket *dest)
    : m_source(source), m_dest(dest)
{
    setAcceptedMouseButtons(Qt::NoButton);
    setZValue(-1); // Behind nodes
    
    m_source->addEdge(this);
    m_dest->addEdge(this);
    
    adjust();
}

GraphSocket *GraphEdge::sourceSocket() const
{
    return m_source;
}

GraphSocket *GraphEdge::destSocket() const
{
    return m_dest;
}

void GraphEdge::adjust()
{
    if (!m_source || !m_dest)
        return;

    m_sourcePoint = m_source->centerPos();
    m_destPoint = m_dest->centerPos();
    
    prepareGeometryChange();
}

QRectF GraphEdge::boundingRect() const
{
    if (!m_source || !m_dest)
        return QRectF();

    qreal penWidth = 2;
    qreal extra = penWidth + 20;

    return QRectF(m_sourcePoint, QSizeF(m_destPoint.x() - m_sourcePoint.x(),
                                      m_destPoint.y() - m_sourcePoint.y()))
        .normalized()
        .adjusted(-extra, -extra, extra, extra);
}

void GraphEdge::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    if (!m_source || !m_dest)
        return;

    m_sourcePoint = m_source->centerPos();
    m_destPoint = m_dest->centerPos();

    QPainterPath path;
    path.moveTo(m_sourcePoint);
    
    qreal dx = m_destPoint.x() - m_sourcePoint.x();
    
    // Better bezier for node graph
    // Control points should be horizontal from sockets
    qreal controlDist = qAbs(dx) * 0.5;
    if (controlDist < 50) controlDist = 50;
    
    QPointF c1 = m_sourcePoint + QPointF(controlDist, 0);
    QPointF c2 = m_destPoint - QPointF(controlDist, 0);

    path.cubicTo(c1, c2, m_destPoint);

    painter->setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawPath(path);
}
