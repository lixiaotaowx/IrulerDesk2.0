#include "VolumeLevelBar.h"
#include <QPainter>

VolumeLevelBar::VolumeLevelBar(QWidget *parent)
    : QWidget(parent)
    , m_activeColor(0, 255, 0)       // Green
    , m_inactiveColor(80, 80, 80)    // Dark Gray
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    setFixedWidth(8); // Fixed width for the column
}

void VolumeLevelBar::setLevel(float level)
{
    float newLevel = qBound(0.0f, level, 1.0f);
    if (!qFuzzyCompare(m_level, newLevel)) {
        m_level = newLevel;
        update();
    }
}

QSize VolumeLevelBar::sizeHint() const
{
    return QSize(8, 48);
}

void VolumeLevelBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false); // Crisp lines

    int w = width();
    int h = height();
    int spacing = 2;
    int barHeight = (h - (m_barCount - 1) * spacing) / m_barCount;
    if (barHeight < 2) barHeight = 2;

    // Recalculate total height used to center vertically if needed
    // Or just start from bottom
    
    // Determine how many bars are lit
    // Use a slightly non-linear mapping or direct linear?
    // Linear is fine for simple visual.
    int activeBars = static_cast<int>(m_level * m_barCount + 0.5f);
    if (m_level > 0.0f && activeBars == 0) activeBars = 1; // Show at least one if some sound

    for (int i = 0; i < m_barCount; ++i) {
        // Index 0 is bottom, Index m_barCount-1 is top
        // Draw from bottom up
        int y = h - (i + 1) * (barHeight + spacing) + spacing;
        
        QRect r(2, y, w - 4, barHeight);
        
        if (i < activeBars) {
            p.fillRect(r, m_activeColor);
        } else {
            p.fillRect(r, m_inactiveColor);
        }
    }
}
