#include "RainbowToolButton.h"
#include <QPainter>
#include <QPainterPath>
#include <QConicalGradient>
#include <QEvent>

RainbowToolButton::RainbowToolButton(QWidget *parent)
    : QPushButton(parent)
    , m_timer(new QTimer(this))
    , m_angle(0)
{
    // Ensure we can handle hover events
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    
    // Set size to match original design (24px content + 1px margin approx)
    setFixedSize(26, 26);
    
    connect(m_timer, &QTimer::timeout, this, &RainbowToolButton::updateAnimation);
    connect(this, &QPushButton::toggled, this, [this](bool checked){
        if (checked) {
            m_timer->start(50); // 20fps
        } else {
            m_timer->stop();
            update();
        }
    });
}

RainbowToolButton::~RainbowToolButton()
{
}

void RainbowToolButton::updateAnimation()
{
    m_angle = (m_angle + 10) % 360;
    update();
}

void RainbowToolButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Calculate drawing area (centered, 24x24)
    QRectF rect((width() - 24) / 2.0, (height() - 24) / 2.0, 24, 24);

    if (isChecked()) {
        // Rotating Rainbow Gradient
        QConicalGradient gradient(rect.center(), m_angle);
        // Saturation 120/255 (~47%), Value 255
        gradient.setColorAt(0.0, QColor::fromHsv(0, 120, 255));
        gradient.setColorAt(0.166, QColor::fromHsv(60, 120, 255));
        gradient.setColorAt(0.333, QColor::fromHsv(120, 120, 255));
        gradient.setColorAt(0.5, QColor::fromHsv(180, 120, 255));
        gradient.setColorAt(0.666, QColor::fromHsv(240, 120, 255));
        gradient.setColorAt(0.833, QColor::fromHsv(300, 120, 255));
        gradient.setColorAt(1.0, QColor::fromHsv(0, 120, 255));

        // Draw hollow ring
        QPen pen(QBrush(gradient), 3.0);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        // Adjust rect slightly inwards to account for stroke width
        p.drawEllipse(rect.adjusted(2, 2, -2, -2));
    } else {
        // Normal/Hover state
        QColor bgColor(Qt::transparent);
        if (isDown()) {
            bgColor = QColor(255, 255, 255, 64); // 0.25
        } else if (underMouse()) {
            bgColor = QColor(255, 255, 255, 38); // 0.15
        }
        
        if (bgColor != Qt::transparent) {
            p.setPen(Qt::NoPen);
            p.setBrush(bgColor);
            p.drawEllipse(rect);
        }
    }

    // Draw Icon
    if (!icon().isNull()) {
        QRect iconRect(rect.center().x() - 8, rect.center().y() - 8, 16, 16);
        icon().paint(&p, iconRect, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled, isChecked() ? QIcon::On : QIcon::Off);
    }
}
