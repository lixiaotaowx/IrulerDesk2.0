#ifndef SNIPPETOVERLAY_H
#define SNIPPETOVERLAY_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QRect>

class SnippetOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit SnippetOverlay(const QPixmap &fullPixmap, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void confirmSelection();
    void cancelSelection();

    QPixmap m_fullPixmap;
    QPoint m_startPos;
    QPoint m_currentPos;
    QRect m_selectionRect;
    bool m_isSelecting;
    bool m_selectionFinished;
};

#endif // SNIPPETOVERLAY_H
