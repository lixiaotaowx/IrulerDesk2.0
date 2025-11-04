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
    void onAnnotationEvent(const QString &phase, int x, int y, const QString &viewerId);
    void hideOverlay();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<QPoint> m_currentStroke;
    QVector<QVector<QPoint>> m_strokes;
};

#endif // ANNOTATIONOVERLAY_H