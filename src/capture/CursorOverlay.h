#ifndef CURSOROVERLAY_H
#define CURSOROVERLAY_H

#include <QWidget>
#include <QPoint>
#include <QString>
#include <QScreen>
#include <QPixmap>
#include <QHash>
#include <QColor>

class CursorOverlay : public QWidget {
    Q_OBJECT
public:
    explicit CursorOverlay(QWidget *parent = nullptr);
    void alignToScreen(QScreen *screen);
    void clear();

public slots:
    void onCursorMoved(int x, int y);
    void onViewerCursor(const QString &viewerId, int x, int y, const QString &viewerName);
    void onViewerNameUpdate(const QString &viewerId, const QString &viewerName);
    void onViewerExited(const QString &viewerId);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct CursorItem { QPoint pos; QString name; qint64 lastMs; };
    QPixmap m_cursorPixmap;
    QPixmap m_cursorSmall;
    QHash<QString, CursorItem> m_cursors;
    QHash<QString, QColor> m_cursorColors;
    QString m_selfName;
};

#endif // CURSOROVERLAY_H
