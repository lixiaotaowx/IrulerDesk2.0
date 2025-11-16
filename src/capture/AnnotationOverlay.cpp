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
        hide();
        clear();
        return;
    } else if (phase == "rect_down") {
        m_drawingRect = true;
        m_rectStart = p;
        m_rectEnd = p;
        m_currentShapeColor = colorId;
    } else if (phase == "rect_move") {
        if (m_drawingRect) { m_rectEnd = p; }
    } else if (phase == "rect_up") {
        if (m_drawingRect) {
            Stroke s;
            s.colorId = m_currentShapeColor;
            QPoint tl(qMin(m_rectStart.x(), m_rectEnd.x()), qMin(m_rectStart.y(), m_rectEnd.y()));
            QPoint br(qMax(m_rectStart.x(), m_rectEnd.x()), qMax(m_rectStart.y(), m_rectEnd.y()));
            QPoint tr(br.x(), tl.y());
            QPoint bl(tl.x(), br.y());
            s.points << tl << tr << br << bl << tl;
            m_strokes.push_back(s);
        }
        m_drawingRect = false;
    } else if (phase == "circle_down") {
        m_drawingCircle = true;
        m_circleCenter = p;
        m_circleRadius = 0;
        m_currentShapeColor = colorId;
    } else if (phase == "circle_move") {
        if (m_drawingCircle) {
            int dx = p.x() - m_circleCenter.x();
            int dy = p.y() - m_circleCenter.y();
            m_circleRadius = static_cast<int>(std::sqrt(dx*dx + dy*dy));
        }
    } else if (phase == "circle_up") {
        if (m_drawingCircle && m_circleRadius > 0) {
            Stroke s;
            s.colorId = m_currentShapeColor;
            int segments = 64;
            for (int i = 0; i <= segments; ++i) {
                double t = (static_cast<double>(i) / segments) * 2.0 * 3.14159265358979323846;
                int x = m_circleCenter.x() + static_cast<int>(std::cos(t) * m_circleRadius);
                int y = m_circleCenter.y() + static_cast<int>(std::sin(t) * m_circleRadius);
                s.points << QPoint(x, y);
            }
            m_strokes.push_back(s);
        }
        m_drawingCircle = false;
    } else if (phase == "erase_down" || phase == "erase_move" || phase == "erase_up") {
        auto hitSegment = [](const QPoint &a, const QPoint &b, const QPoint &p, int r2) -> bool {
            int abx = b.x() - a.x();
            int aby = b.y() - a.y();
            int apx = p.x() - a.x();
            int apy = p.y() - a.y();
            long long ab2 = 1LL*abx*abx + 1LL*aby*aby;
            if (ab2 == 0) {
                int dx = p.x() - a.x();
                int dy = p.y() - a.y();
                return (dx*dx + dy*dy) <= r2;
            }
            double t = (apx*abx + apy*aby) / static_cast<double>(ab2);
            if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
            double qx = a.x() + t * abx;
            double qy = a.y() + t * aby;
            double dx = p.x() - qx;
            double dy = p.y() - qy;
            return (dx*dx + dy*dy) <= r2;
        };
        int radius = 20;
        int r2 = radius * radius;
        for (int i = m_strokes.size() - 1; i >= 0; --i) {
            const auto &s = m_strokes[i];
            QVector<Stroke> parts;
            QVector<QPoint> cur;
            if (!s.points.isEmpty()) cur.push_back(s.points[0]);
            for (int j = 1; j < s.points.size(); ++j) {
                bool cut = hitSegment(s.points[j-1], s.points[j], p, r2);
                if (cut) {
                    if (cur.size() >= 2) { Stroke ns; ns.colorId = s.colorId; ns.points = cur; parts.push_back(ns); }
                    cur.clear();
                    cur.push_back(s.points[j]);
                } else {
                    if (cur.isEmpty()) cur.push_back(s.points[j-1]);
                    cur.push_back(s.points[j]);
                }
            }
            if (cur.size() >= 2) { Stroke ns; ns.colorId = s.colorId; ns.points = cur; parts.push_back(ns); }
            m_strokes.removeAt(i);
            for (int k = 0; k < parts.size(); ++k) { m_strokes.insert(i + k, parts[k]); }
        }
        if (!m_currentStroke.points.isEmpty()) {
            bool hitCur = false;
            if (m_currentStroke.points.size() <= 1) {
                int dx = m_currentStroke.points[0].x() - p.x();
                int dy = m_currentStroke.points[0].y() - p.y();
                hitCur = (dx*dx + dy*dy) <= r2;
            } else {
                for (int j = 1; j < m_currentStroke.points.size(); ++j) {
                    if (hitSegment(m_currentStroke.points[j-1], m_currentStroke.points[j], p, r2)) { hitCur = true; break; }
                }
            }
            if (hitCur) m_currentStroke.points.clear();
        }
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
    if (m_drawingRect) {
        QPen pen(colorForId(m_currentShapeColor));
        pen.setWidth(3);
        painter.setPen(pen);
        QPoint tl(qMin(m_rectStart.x(), m_rectEnd.x()), qMin(m_rectStart.y(), m_rectEnd.y()));
        QPoint br(qMax(m_rectStart.x(), m_rectEnd.x()), qMax(m_rectStart.y(), m_rectEnd.y()));
        painter.drawRect(QRect(tl, br));
    }
    if (m_drawingCircle && m_circleRadius > 0) {
        QPen pen(colorForId(m_currentShapeColor));
        pen.setWidth(3);
        painter.setPen(pen);
        painter.drawEllipse(m_circleCenter, m_circleRadius, m_circleRadius);
    }
}