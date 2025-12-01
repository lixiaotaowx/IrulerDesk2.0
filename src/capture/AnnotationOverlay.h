#ifndef ANNOTATIONOVERLAY_H
#define ANNOTATIONOVERLAY_H

#include <QWidget>
#include <QVector>
#include <QPoint>
#include <QString>
#include <QScreen>
#include <QPixmap>
#include <QHash>

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
    void onCursorMoved(int x, int y);
    void onViewerCursor(const QString &viewerId, int x, int y, const QString &viewerName);
    void onViewerNameUpdate(const QString &viewerId, const QString &viewerName);
    void onViewerExited(const QString &viewerId);

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
    QPixmap m_cursorPixmap;
    QPixmap m_cursorSmall;
    struct CursorItem { QPoint pos; QString name; qint64 lastMs; };
    QHash<QString, CursorItem> m_cursors;
    QHash<QString, QColor> m_cursorColors;
    QString m_selfName;
};

#endif // ANNOTATIONOVERLAY_H
