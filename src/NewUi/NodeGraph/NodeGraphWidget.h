#pragma once

#include <QWidget>

class QGraphicsScene;
class QGraphicsView;

class NodeGraphWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NodeGraphWidget(QWidget *parent = nullptr);
    ~NodeGraphWidget();

private:
    void setupScene();

    QGraphicsScene *m_scene;
    QGraphicsView *m_view;
};
