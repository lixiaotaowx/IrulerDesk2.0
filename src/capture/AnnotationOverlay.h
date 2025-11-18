#ifndef ANNOTATIONOVERLAY_H
#define ANNOTATIONOVERLAY_H

#include <QWidget>
#include <QVector>
#include <QPoint>
#include <QString>
#include <QScreen>

class QLabel;
class QMovie;

class AnnotationOverlay : public QWidget {
    Q_OBJECT
public:
    explicit AnnotationOverlay(QWidget *parent = nullptr);
    void alignToScreen(QScreen *screen);
    void clear();

public slots:
    void onAnnotationEvent(const QString &phase, int x, int y, const QString &viewerId, int colorId);
    void onTextAnnotation(const QString &text, int x, int y, const QString &viewerId, int colorId, int fontSize);
    void hideOverlay();
    void onLikeRequested(const QString &viewerId);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Stroke { QVector<QPoint> points; int colorId = 0; int thickness = 3; };
    struct TextItem { QString text; QPoint pos; int colorId = 0; int fontSize = 16; };
    struct OpEntry { int kind = 0; int startIndex = 0; int count = 1; };
    Stroke m_currentStroke;
    QVector<Stroke> m_strokes;
    QVector<TextItem> m_texts;
    QVector<OpEntry> m_ops;
    bool m_drawingRect = false;
    QPoint m_rectStart;
    QPoint m_rectEnd;
    int m_currentShapeColor = 0;
    bool m_drawingCircle = false;
    QPoint m_circleCenter;
    int m_circleRadius = 0;
    bool m_drawingArrow = false;
    QPoint m_arrowStart;
    QPoint m_arrowEnd;
    QLabel *m_likeLabel = nullptr;
    QMovie *m_likeMovie = nullptr;
    QTimer *m_idleTimer = nullptr;
    qint64 m_lastEventMs = 0;
};

#endif // ANNOTATIONOVERLAY_H