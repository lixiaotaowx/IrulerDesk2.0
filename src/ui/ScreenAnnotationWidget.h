#ifndef SCREENANNOTATIONWIDGET_H
#define SCREENANNOTATIONWIDGET_H

#include <QWidget>
#include <QPainter>
#include <QPolygonF>
#include <QVector>
#include <QMouseEvent>

struct AnnotationItem {
    enum Type { Pen, Rect, Circle, Arrow, Text, Eraser };
    Type type;
    int colorId;
    QPolygonF path;
    QPointF start;
    QPointF end;
    QString text;
    int penWidth = 3;
};

class ScreenAnnotationWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenAnnotationWidget(QWidget *parent = nullptr);
    ~ScreenAnnotationWidget();

    // 0:None, 1:Pen, 2:Rect, 3:Circle, 4:Arrow, 5:Text, 6:Eraser
    void setTool(int toolMode); 
    void setColorId(int id);
    void clear();
    void undo();
    void setEnabled(bool enabled); // Controls visibility and input pass-through
    bool isEnabled() const { return m_enabled; }

signals:
    void toolCancelled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void drawItem(QPainter &painter, const AnnotationItem &item);
    QColor getColor(int id) const;

    QVector<AnnotationItem> m_items;
    AnnotationItem m_currentItem;
    bool m_isDrawing;
    int m_currentTool; // 0:None, 1:Pen, 2:Rect, 3:Circle, 4:Arrow, 5:Text, 6:Eraser
    int m_currentColorId;
    bool m_enabled;
};

#endif // SCREENANNOTATIONWIDGET_H
