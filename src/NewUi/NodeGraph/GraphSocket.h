#pragma once

#include <QGraphicsItem>
#include <QBrush>
#include <QPen>

class GraphNode;
class GraphEdge;

class GraphSocket : public QGraphicsItem
{
public:
    enum SocketType {
        Input,
        Output
    };

    GraphSocket(GraphNode *parent, SocketType type, const QString &name);

    SocketType socketType() const { return m_type; }
    GraphNode *node() const;
    QString name() const { return m_name; }

    void addEdge(GraphEdge *edge);
    void removeEdge(GraphEdge *edge);
    QList<GraphEdge *> edges() const { return m_edges; }

    QPointF centerPos() const;

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    GraphNode *m_node;
    SocketType m_type;
    QString m_name;
    QList<GraphEdge *> m_edges;
    
    const qreal m_radius = 5.0;
    const qreal m_margin = 2.0;
};
