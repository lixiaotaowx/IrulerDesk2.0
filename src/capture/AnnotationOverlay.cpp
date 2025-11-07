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
    m_currentStroke.points.clear();
    m_currentStroke.colorId = 0;
    m_strokes.clear();
    update();
}

void AnnotationOverlay::onAnnotationEvent(const QString &phase, int x, int y, const QString &viewerId, int colorId)
{
    Q_UNUSED(viewerId);
    QPoint p(x, y);
    if (phase == "down") {
        m_currentStroke.points.clear();
        m_currentStroke.colorId = colorId;
        m_currentStroke.points.push_back(p);
    } else if (phase == "move") {
        if (!m_currentStroke.points.isEmpty()) {
            m_currentStroke.points.push_back(p);
        }
    } else if (phase == "up") {
        if (!m_currentStroke.points.isEmpty()) {
            m_currentStroke.points.push_back(p);
            m_strokes.push_back(m_currentStroke);
            m_currentStroke.points.clear();
        }
    } else if (phase == "undo") {
        // 撤销上一笔
        if (!m_currentStroke.points.isEmpty()) {
            // 正在绘制时撤销当前笔
            m_currentStroke.points.clear();
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
    auto colorForId = [](int id) -> QColor {
        switch (id) {
            case 0: return QColor(255, 0, 0, 200);      // 红
            case 1: return QColor(0, 200, 0, 200);      // 绿
            case 2: return QColor(0, 128, 255, 200);    // 蓝
            case 3: return QColor(255, 204, 0, 200);    // 黄
            default: return QColor(255, 0, 0, 200);     // 默认红
        }
    };

    // 绘制已完成的笔划
    for (const auto &stroke : m_strokes) {
        if (stroke.points.size() < 2) continue;
        QPen pen(colorForId(stroke.colorId));
        pen.setWidth(3);
        painter.setPen(pen);
        for (int i = 1; i < stroke.points.size(); ++i) {
            painter.drawLine(stroke.points[i-1], stroke.points[i]);
        }
    }
    // 绘制当前笔划
    if (m_currentStroke.points.size() >= 2) {
        QPen pen(colorForId(m_currentStroke.colorId));
        pen.setWidth(3);
        painter.setPen(pen);
        for (int i = 1; i < m_currentStroke.points.size(); ++i) {
            painter.drawLine(m_currentStroke.points[i-1], m_currentStroke.points[i]);
        }
    }
}