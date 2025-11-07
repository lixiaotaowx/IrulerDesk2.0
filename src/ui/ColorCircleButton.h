#ifndef COLORCIRCLEBUTTON_H
#define COLORCIRCLEBUTTON_H

#include <QAbstractButton>
#include <QColor>

class ColorCircleButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ColorCircleButton(QWidget *parent = nullptr);

    void setColorId(int id);
    int colorId() const { return m_colorId; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    QSize sizeHint() const override;

private:
    QColor colorForId(int id) const;

private:
    int m_colorId{0};
    bool m_hovered{false};
};

#endif // COLORCIRCLEBUTTON_H