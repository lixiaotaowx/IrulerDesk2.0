#pragma once

#include <QGraphicsItem>
#include <QList>

class GraphEdge;
class GraphSocket;

class GraphNode : public QGraphicsItem
{
public:
    explicit GraphNode(const QString &title);

    enum { Type = UserType + 1 };
    int type() const override { return Type; }

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    GraphSocket* addInput(const QString &name);
    GraphSocket* addOutput(const QString &name);

    QList<GraphSocket*> inputs() const { return m_inputs; }
    QList<GraphSocket*> outputs() const { return m_outputs; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    void layoutSockets();

    QString m_title;
    QList<GraphSocket *> m_inputs;
    QList<GraphSocket *> m_outputs;
    
    qreal m_width = 150.0;
    qreal m_headerHeight = 30.0;
    qreal m_socketSpacing = 20.0;
    qreal m_height = 50.0; // Dynamic
};
