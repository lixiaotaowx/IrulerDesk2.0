#pragma once

#include <QGraphicsPathItem>

class GraphSocket;

class GraphEdge : public QGraphicsPathItem
{
public:
    GraphEdge(GraphSocket *source, GraphSocket *dest);

    GraphSocket *sourceSocket() const;
    GraphSocket *destSocket() const;

    void adjust();

protected:
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    GraphSocket *m_source;
    GraphSocket *m_dest;
    QPointF m_sourcePoint;
    QPointF m_destPoint;
};
