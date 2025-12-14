#ifndef ANNOTATIONTOOLBAR_H
#define ANNOTATIONTOOLBAR_H

#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include "RainbowToolButton.h"
#include "ColorCircleButton.h"

class AnnotationToolbar : public QWidget
{
    Q_OBJECT

public:
    explicit AnnotationToolbar(QWidget *parent = nullptr);
    ~AnnotationToolbar();

    // Reset all tools to unselected state
    void resetTools();

    // Get current color ID
    int currentColorId() const;

signals:
    void toolSelected(int mode); // 0:None, 1:Pen, 2:Rect, 3:Circle, 4:Arrow, 5:Text, 6:Eraser
    void colorChanged(int colorId);
    void undoRequested();
    void cameraRequested();
    void clearRequested();

private slots:
    void onToolToggled(bool checked);
    void updateToolStyles();

private:
    void setupUI();

    QHBoxLayout *m_layout;
    ColorCircleButton *m_colorButton;
    RainbowToolButton *m_penButton;
    RainbowToolButton *m_rectButton;
    RainbowToolButton *m_circleButton;
    RainbowToolButton *m_arrowButton;
    RainbowToolButton *m_textButton;
    RainbowToolButton *m_eraserButton;
    QPushButton *m_undoButton;
    QPushButton *m_cameraButton;
    QPushButton *m_clearButton;

    QString m_micButtonStyle;
    QString m_selectedToolStyle;
};

#endif // ANNOTATIONTOOLBAR_H
