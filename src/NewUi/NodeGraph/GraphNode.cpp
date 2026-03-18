#include "GraphNode.h"
#include "GraphSocket.h"

#include <QPainter>
#include <QGraphicsScene>
#include <QStyleOption>
#include <QFontMetrics>

GraphNode::GraphNode(const QString &title)
    : m_title(title)
{
    setFlag(ItemIsMovable);
    setFlag(ItemSendsGeometryChanges);
    setCacheMode(DeviceCoordinateCache);
    setZValue(0);
}

QRectF GraphNode::boundingRect() const
{
    return QRectF(0, 0, m_width, m_height);
}

void GraphNode::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(widget);

    // Draw Background
    QPainterPath path;
    path.addRoundedRect(boundingRect(), 10, 10);
    
    // Body
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(40, 40, 40, 230)); // Dark grey
    painter->drawPath(path);

    // Header
    QPainterPath headerPath;
    headerPath.addRoundedRect(0, 0, m_width, m_headerHeight, 10, 10);
    // Clip bottom of header to make it straight
    QRectF headerRect(0, 0, m_width, m_headerHeight);
    
    painter->setBrush(QColor(60, 60, 60, 255)); // Lighter grey header
    // Draw simplified header rect
    painter->drawRoundedRect(headerRect, 10, 10);
    // Cover bottom corners
    painter->drawRect(0, m_headerHeight - 10, m_width, 10);

    // Title
    painter->setPen(Qt::white);
    painter->setFont(QFont("Arial", 10, QFont::Bold));
    painter->drawText(headerRect, Qt::AlignCenter, m_title);
    
    // Socket labels
    painter->setFont(QFont("Arial", 9));
    
    for (GraphSocket* socket : m_inputs) {
        QPointF pos = socket->pos();
        QRectF textRect(pos.x() + 15, pos.y() - 10, m_width/2 - 20, 20);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, socket->name());
    }
    
    for (GraphSocket* socket : m_outputs) {
        QPointF pos = socket->pos();
        QRectF textRect(pos.x() - m_width/2, pos.y() - 10, m_width/2 - 15, 20);
        painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, socket->name());
    }
    
    // Selection outline
    if (isSelected()) {
        painter->setPen(QPen(QColor(255, 165, 0), 2)); // Orange
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    }
}

GraphSocket* GraphNode::addInput(const QString &name)
{
    GraphSocket *socket = new GraphSocket(this, GraphSocket::Input, name);
    m_inputs.append(socket);
    layoutSockets();
    return socket;
}

GraphSocket* GraphNode::addOutput(const QString &name)
{
    GraphSocket *socket = new GraphSocket(this, GraphSocket::Output, name);
    m_outputs.append(socket);
    layoutSockets();
    return socket;
}

void GraphNode::layoutSockets()
{
    qreal y = m_headerHeight + 20;
    
    for (GraphSocket *socket : m_inputs) {
        socket->setPos(0, y);
        y += m_socketSpacing;
    }
    
    qreal inputHeight = y;
    
    y = m_headerHeight + 20;
    for (GraphSocket *socket : m_outputs) {
        socket->setPos(m_width, y);
        y += m_socketSpacing;
    }
    
    m_height = qMax(inputHeight, y) + 10;
    update(); // Redraw with new height
}

QVariant GraphNode::itemChange(GraphicsItemChange change, const QVariant &value)
{
    // Notify connected edges when node moves
    // Sockets handle their own scene position changes, but we might want to optimize
    return QGraphicsItem::itemChange(change, value);
}
