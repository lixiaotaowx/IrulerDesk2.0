#include "GraphSocket.h"
#include "GraphNode.h"
#include "GraphEdge.h"

#include <QPainter>
#include <QGraphicsScene>
#include <QStyleOption>

GraphSocket::GraphSocket(GraphNode *parent, SocketType type, const QString &name)
    : QGraphicsItem(parent), m_node(parent), m_type(type), m_name(name)
{
    setFlag(ItemSendsScenePositionChanges);
    setCacheMode(DeviceCoordinateCache);
    setZValue(1); // Above node background
}

GraphNode *GraphSocket::node() const
{
    return m_node;
}

void GraphSocket::addEdge(GraphEdge *edge)
{
    if (!m_edges.contains(edge)) {
        m_edges.append(edge);
    }
}

void GraphSocket::removeEdge(GraphEdge *edge)
{
    m_edges.removeAll(edge);
}

QPointF GraphSocket::centerPos() const
{
    return scenePos(); // + QPointF(m_radius, m_radius); // center is 0,0 local
}

QRectF GraphSocket::boundingRect() const
{
    return QRectF(-m_radius - m_margin, -m_radius - m_margin, 
                  (m_radius + m_margin) * 2, (m_radius + m_margin) * 2);
}

void GraphSocket::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setPen(QPen(Qt::black, 1.0));
    painter->setBrush(QColor(100, 200, 100)); // Light Green

    // Draw circle
    painter->drawEllipse(QPointF(0, 0), m_radius, m_radius);
    
    // Draw highlight if hovering (can implement later)
}

QVariant GraphSocket::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemScenePositionHasChanged) {
        for (GraphEdge *edge : m_edges) {
            edge->adjust();
        }
    }
    return QGraphicsItem::itemChange(change, value);
}
