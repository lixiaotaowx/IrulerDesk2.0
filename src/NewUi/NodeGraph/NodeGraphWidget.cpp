#include "NodeGraphWidget.h"
#include "GraphNode.h"
#include "GraphEdge.h"
#include "NodeGraphView.h"
#include "GraphSocket.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QVBoxLayout>
#include <QTime>

NodeGraphWidget::NodeGraphWidget(QWidget *parent)
    : QWidget(parent)
{
    m_scene = new QGraphicsScene(this);
    m_scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    // Use a larger scene rect or let it grow
    m_scene->setSceneRect(-2000, -2000, 4000, 4000);
    m_scene->setBackgroundBrush(QColor(30, 30, 30));

    m_view = new NodeGraphView(m_scene, this);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    setupScene();
}

NodeGraphWidget::~NodeGraphWidget()
{
}

void NodeGraphWidget::setupScene()
{
    GraphNode *node1 = new GraphNode("Input Source");
    node1->addOutput("Video");
    node1->addOutput("Audio");
    
    GraphNode *node2 = new GraphNode("Filter");
    node2->addInput("In");
    node2->addOutput("Out");
    
    GraphNode *node3 = new GraphNode("Display");
    node3->addInput("Video");
    
    GraphNode *node4 = new GraphNode("Speaker");
    node4->addInput("Audio");

    m_scene->addItem(node1);
    m_scene->addItem(node2);
    m_scene->addItem(node3);
    m_scene->addItem(node4);

    node1->setPos(-300, 0);
    node2->setPos(0, -50);
    node3->setPos(300, -100);
    node4->setPos(300, 100);
    
    // Connect
    // Note: We need to access sockets. In a real app we'd look them up by name or index.
    if (!node1->outputs().isEmpty() && !node2->inputs().isEmpty()) {
        m_scene->addItem(new GraphEdge(node1->outputs()[0], node2->inputs()[0]));
    }
    
    if (!node2->outputs().isEmpty() && !node3->inputs().isEmpty()) {
        m_scene->addItem(new GraphEdge(node2->outputs()[0], node3->inputs()[0]));
    }
    
    if (node1->outputs().size() > 1 && !node4->inputs().isEmpty()) {
        m_scene->addItem(new GraphEdge(node1->outputs()[1], node4->inputs()[0]));
    }
}
