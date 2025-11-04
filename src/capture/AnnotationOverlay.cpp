#include "AnnotationOverlay.h"
#include <QPainter>
#include <QPaintEvent>
#include <QGuiApplication>

AnnotationOverlay::AnnotationOverlay(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);
    hide();
}

void AnnotationOverlay::alignToScreen(QScreen *screen)
{
    if (!screen) return;
    setGeometry(screen->geometry());
}

void AnnotationOverlay::clear()
{
    m_currentStroke.clear();
    m_strokes.clear();
    update();
}

void AnnotationOverlay::onAnnotationEvent(const QString &phase, int x, int y, const QString &viewerId)
{
    Q_UNUSED(viewerId);
    QPoint p(x, y);
    if (phase == "down") {
        m_currentStroke.clear();
        m_currentStroke.push_back(p);
    } else if (phase == "move") {
        if (!m_currentStroke.isEmpty()) {
            m_currentStroke.push_back(p);
        }
    } else if (phase == "up") {
        if (!m_currentStroke.isEmpty()) {
            m_currentStroke.push_back(p);
            m_strokes.push_back(m_currentStroke);
            m_currentStroke.clear();
        }
    } else if (phase == "undo") {
        // 撤销上一笔
        if (!m_currentStroke.isEmpty()) {
            // 正在绘制时撤销当前笔
            m_currentStroke.clear();
        } else if (!m_strokes.isEmpty()) {
            m_strokes.removeLast();
        }
    } else if (phase == "clear") {
        // 清空所有笔划
        clear();
        return; // clear内部已调用update
    } else if (phase == "overlay_close") {
        // 关闭透明画板（隐藏并清空）
        hide();
        clear();
        return;
    }
    update();
}

void AnnotationOverlay::hideOverlay()
{
    hide();
    clear();
}

void AnnotationOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(255, 0, 0, 180));
    pen.setWidth(3);
    painter.setPen(pen);

    // 绘制已完成的笔划
    for (const auto &stroke : m_strokes) {
        if (stroke.size() < 2) continue;
        for (int i = 1; i < stroke.size(); ++i) {
            painter.drawLine(stroke[i-1], stroke[i]);
        }
    }
    // 绘制当前笔划
    if (m_currentStroke.size() >= 2) {
        for (int i = 1; i < m_currentStroke.size(); ++i) {
            painter.drawLine(m_currentStroke[i-1], m_currentStroke[i]);
        }
    }
}