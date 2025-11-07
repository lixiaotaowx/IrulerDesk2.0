#include "ColorCircleButton.h"
#include <QPainter>
#include <QPainterPath>
#include <QPen>

ColorCircleButton::ColorCircleButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void ColorCircleButton::setColorId(int id)
{
    if (id < 0) id = 0;
    if (id > 3) id = 3;
    if (m_colorId == id) return;
    m_colorId = id;
    update();
}

QColor ColorCircleButton::colorForId(int id) const
{
    switch (id) {
    case 0: return QColor(255, 59, 48);   // 红 #ff3b30
    case 1: return QColor(52, 199, 89);   // 绿 #34c759
    case 2: return QColor(10, 132, 255);  // 蓝 #0a84ff
    default: return QColor(255, 214, 10); // 黄 #ffd60a
    }
}

void ColorCircleButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const int d = qMin(width(), height());
    const qreal margin = 0.5; // 轻微留白，避免裁剪造成锯齿
    const QRectF rect((width() - d) / 2.0 + margin,
                      (height() - d) / 2.0 + margin,
                      d - 2 * margin,
                      d - 2 * margin);

    QPainterPath circle;
    circle.addEllipse(rect);

    // 填充圆形颜色
    p.fillPath(circle, colorForId(m_colorId));

    // 悬停时轻描边框，更柔和
    if (m_hovered) {
        QPen pen(QColor(255, 255, 255, 55)); // 半透明白
        pen.setWidthF(1.0);
        p.setPen(pen);
        p.drawEllipse(rect);
    }
}

void ColorCircleButton::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event);
    m_hovered = true;
    update();
}

void ColorCircleButton::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_hovered = false;
    update();
}

QSize ColorCircleButton::sizeHint() const
{
    return QSize(20, 20);
}