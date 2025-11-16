#ifndef ANNOTATIONOVERLAY_H
#define ANNOTATIONOVERLAY_H

#include <QWidget>
#include <QVector>
#include <QPoint>
#include <QString>
#include <QScreen>

class AnnotationOverlay : public QWidget {
    Q_OBJECT
public:
    explicit AnnotationOverlay(QWidget *parent = nullptr);
    void alignToScreen(QScreen *screen);
    void clear();

public slots:
    void onAnnotationEvent(const QString &phase, int x, int y, const QString &viewerId, int colorId);
    void hideOverlay();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Stroke { QVector<QPoint> points; int colorId = 0; };
    Stroke m_currentStroke;
    QVector<Stroke> m_strokes;
    bool m_drawingRect = false;
    QPoint m_rectStart;
    QPoint m_rectEnd;
    int m_currentShapeColor = 0;
    bool m_drawingCircle = false;
    QPoint m_circleCenter;
    int m_circleRadius = 0;
};

#endif // ANNOTATIONOVERLAY_H