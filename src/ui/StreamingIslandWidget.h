#ifndef STREAMINGISLANDWIDGET_H
#define STREAMINGISLANDWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QMovie>
#include "ScreenAnnotationWidget.h"
#include "AnnotationToolbar.h"

class StreamingIslandWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StreamingIslandWidget(QWidget *parent = nullptr);
    ~StreamingIslandWidget();
    void showOnScreen();
    void setTargetScreen(QScreen *screen);

signals:
    void stopStreamingRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    void setupUI();
    void showClipboardToast();
    void updateAnnotationMask();

    QWidget *m_contentWidget;
    QHBoxLayout *m_layout;
    
    AnnotationToolbar *m_toolbar;
    
    ScreenAnnotationWidget *m_annotationWidget;

    bool m_dragging;
    QPoint m_dragPosition;
    QScreen *m_targetScreen = nullptr;
};

#endif // STREAMINGISLANDWIDGET_H
