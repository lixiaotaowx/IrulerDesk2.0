#include "AnnotationOverlay.h"
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QMovie>
#include <QFile>
#include <QTimer>
#include <QDateTime>
#include <QTextStream>
#include <QRandomGenerator>

AnnotationOverlay::AnnotationOverlay(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);
    hide();
    QString appDir = QCoreApplication::applicationDirPath();
    QString iconDir = appDir + "/maps/logo";
    QString file = iconDir + "/cursor.png";
    if (QFile::exists(file)) {
        m_cursorPixmap.load(file);
        int w = qMax(8, int(m_cursorPixmap.width() * 0.5));
        int h = qMax(8, int(m_cursorPixmap.height() * 0.5));
        m_cursorSmall = m_cursorPixmap.scaled(QSize(w, h), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QString cfg = appDir + "/config/app_config.txt";
    QFile f(cfg);
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("user_name=")) {
                m_selfName = line.mid(10).trimmed();
                break;
            }
        }
        f.close();
    }
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
    m_ops.clear();
    m_drawingRect = false;
    m_rectStart = QPoint();
    m_rectEnd = QPoint();
    m_drawingCircle = false;
    m_circleStart = QPoint();
    m_circleEnd = QPoint();
    m_drawingArrow = false;
    m_arrowStart = QPoint();
    m_arrowEnd = QPoint();
    m_texts.clear();
    m_cursors.clear();
    update();
}

void AnnotationOverlay::onAnnotationEvent(const QString &phase, int x, int y, const QString &viewerId, int colorId)
{
    Q_UNUSED(viewerId);
    QPoint p(x, y);
    if (phase == "down") {
        m_currentStroke.points.clear();
        m_currentStroke.colorId = colorId;
        m_currentStroke.thickness = 3;
        m_currentStroke.smooth = true; // Paint tool strokes should be smooth
        m_currentStroke.points.push_back(p);
    } else if (phase == "move") {
        if (!m_currentStroke.points.isEmpty()) {
            m_currentStroke.points.push_back(p);
        }
    } else if (phase == "up") {
        if (!m_currentStroke.points.isEmpty()) {
            m_currentStroke.points.push_back(p);
            m_strokes.push_back(m_currentStroke);
            OpEntry op; op.kind = 0; op.startIndex = m_strokes.size() - 1; op.count = 1; m_ops.push_back(op);
            m_currentStroke.points.clear();
            m_currentStroke.smooth = false; // Reset
        }
    } else if (phase == "undo") {
        if (!m_currentStroke.points.isEmpty()) {
            m_currentStroke.points.clear();
        } else if (!m_ops.isEmpty()) {
            OpEntry op = m_ops.last();
            m_ops.removeLast();
            if (op.kind == 0) {
                for (int i = 0; i < op.count && !m_strokes.isEmpty(); ++i) {
                    m_strokes.removeLast();
                }
            } else {
                if (!m_texts.isEmpty()) m_texts.removeLast();
            }
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
            s.thickness = 3;
            QPoint tl(qMin(m_rectStart.x(), m_rectEnd.x()), qMin(m_rectStart.y(), m_rectEnd.y()));
            QPoint br(qMax(m_rectStart.x(), m_rectEnd.x()), qMax(m_rectStart.y(), m_rectEnd.y()));
            QPoint tr(br.x(), tl.y());
            QPoint bl(tl.x(), br.y());
            s.points << tl << tr << br << bl << tl;
            m_strokes.push_back(s);
            OpEntry op; op.kind = 0; op.startIndex = m_strokes.size() - 1; op.count = 1; m_ops.push_back(op);
        }
        m_drawingRect = false;
    } else if (phase == "circle_down") {
        m_drawingCircle = true;
        m_circleStart = p;
        m_circleEnd = p;
        m_currentShapeColor = colorId;
    } else if (phase == "circle_move") {
        if (m_drawingCircle) {
            m_circleEnd = p;
        }
    } else if (phase == "circle_up") {
        if (m_drawingCircle) {
            Stroke s;
            s.colorId = m_currentShapeColor;
            s.thickness = 3;
            int segments = 64;
            QPoint tl(qMin(m_circleStart.x(), m_circleEnd.x()), qMin(m_circleStart.y(), m_circleEnd.y()));
            QPoint br(qMax(m_circleStart.x(), m_circleEnd.x()), qMax(m_circleStart.y(), m_circleEnd.y()));
            int w = br.x() - tl.x();
            int h = br.y() - tl.y();
            if (w > 0 || h > 0) {
                double cx = tl.x() + w / 2.0;
                double cy = tl.y() + h / 2.0;
                double rx = w / 2.0;
                double ry = h / 2.0;
                for (int i = 0; i <= segments; ++i) {
                    double t = (static_cast<double>(i) / segments) * 2.0 * 3.14159265358979323846;
                    int x = static_cast<int>(cx + std::cos(t) * rx);
                    int y = static_cast<int>(cy + std::sin(t) * ry);
                    s.points << QPoint(x, y);
                }
                m_strokes.push_back(s);
                OpEntry op; op.kind = 0; op.startIndex = m_strokes.size() - 1; op.count = 1; m_ops.push_back(op);
            }
        }
        m_drawingCircle = false;
    } else if (phase == "arrow_down") {
        m_drawingArrow = true;
        m_arrowStart = p;
        m_arrowEnd = p;
        m_currentShapeColor = colorId;
    } else if (phase == "arrow_move") {
        if (m_drawingArrow) { m_arrowEnd = p; }
    } else if (phase == "arrow_up") {
        if (m_drawingArrow) {
            int dx = m_arrowEnd.x() - m_arrowStart.x();
            int dy = m_arrowEnd.y() - m_arrowStart.y();
            double len = std::sqrt(static_cast<double>(dx*dx + dy*dy));
            if (len > 0.0) {
                double ux = dx / len;
                double uy = dy / len;
                double headLen = std::min(24.0, std::max(10.0, len * 0.25));
                double angle = 28.0 * 3.14159265358979323846 / 180.0;
                double cosA = std::cos(angle);
                double sinA = std::sin(angle);
                double lx = cosA * ux - sinA * uy;
                double ly = sinA * ux + cosA * uy;
                double rx = cosA * ux + sinA * uy;
                double ry = -sinA * ux + cosA * uy;
                QPoint left(m_arrowEnd.x() - static_cast<int>(lx * headLen), m_arrowEnd.y() - static_cast<int>(ly * headLen));
                QPoint right(m_arrowEnd.x() - static_cast<int>(rx * headLen), m_arrowEnd.y() - static_cast<int>(ry * headLen));
                Stroke shaft; shaft.colorId = m_currentShapeColor; shaft.thickness = 15; shaft.points << m_arrowStart << m_arrowEnd;
                Stroke head; head.colorId = m_currentShapeColor; head.thickness = 15; head.points << left << m_arrowEnd << right;
                m_strokes.push_back(shaft);
                m_strokes.push_back(head);
                OpEntry op; op.kind = 0; op.startIndex = m_strokes.size() - 2; op.count = 2; m_ops.push_back(op);
            }
        }
        m_drawingArrow = false;
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

        // 擦除文本项：命中圆与文本边框相交则删除
        QFont f;
        for (int i = m_texts.size() - 1; i >= 0; --i) {
            const auto &t = m_texts[i];
            QFont tf = f; tf.setPixelSize(t.fontSize);
            QFontMetrics fm(tf);
            QRect rect(QPoint(t.pos.x(), t.pos.y() - fm.ascent()), QSize(fm.horizontalAdvance(t.text), fm.height()));
            rect.adjust(-6, -6, 6, 6);
            int cx = p.x(); int cy = p.y();
            int nearestX = qBound(rect.left(), cx, rect.right());
            int nearestY = qBound(rect.top(), cy, rect.bottom());
            int dx = cx - nearestX;
            int dy = cy - nearestY;
            if (dx*dx + dy*dy <= r2) {
                m_texts.removeAt(i);
            }
        }
    }
    update();
}

void AnnotationOverlay::hideOverlay()
{
    hide();
    clear();
}

void AnnotationOverlay::onLikeRequested(const QString &viewerId)
{
    Q_UNUSED(viewerId);
    QString appDir = QCoreApplication::applicationDirPath();
    QString iconDir = appDir + "/maps/logo";
    QString file = iconDir + "/zan.gif";
    if (!m_likeLabel) {
        m_likeLabel = new QLabel(this);
        m_likeLabel->setStyleSheet("QLabel { background: transparent; }");
    }
    if (QFile::exists(file)) {
        if (!m_likeMovie) {
            m_likeMovie = new QMovie(file, QByteArray(), this);
        } else {
            m_likeMovie->setFileName(file);
            m_likeMovie->stop();
            QObject::disconnect(m_likeMovie, nullptr, this, nullptr);
        }
        m_likeMovie->setCacheMode(QMovie::CacheAll);
        m_likeLabel->setMovie(m_likeMovie);
        m_likeMovie->jumpToFrame(0);
        QRect area = this->rect();
        QSize sz = m_likeMovie->currentPixmap().size();
        QPoint center(area.width()/2, area.height()/2);
        int w = sz.width();
        int h = sz.height();
        m_likeLabel->setGeometry(center.x() - w/2, center.y() - h/2, w, h);
        m_likeLabel->setVisible(true);
        m_likeLabel->raise();
        m_likeMovie->start();
        int total = m_likeMovie->frameCount();
        QPointer<QLabel> lblPtr(m_likeLabel);
        QPointer<QMovie> mvPtr(m_likeMovie);
        if (total > 0) {
            QObject::connect(m_likeMovie, &QMovie::frameChanged, this, [lblPtr, mvPtr, total](int frame) {
                if (!lblPtr || !mvPtr) return;
                if (frame >= total - 1) {
                    lblPtr->setVisible(false);
                    mvPtr->stop();
                }
            }, Qt::QueuedConnection);
        } else {
            QTimer::singleShot(2000, this, [lblPtr, mvPtr]() { if (lblPtr) lblPtr->setVisible(false); if (mvPtr) mvPtr->stop(); });
        }
        return;
    }
}

void AnnotationOverlay::onCursorMoved(int x, int y)
{
    // 刷新本机用户名（系统设置可能已变更）
    QString appDir = QCoreApplication::applicationDirPath();
    QString cfg = appDir + "/config/app_config.txt";
    QFile f(cfg);
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("user_name=")) {
                m_selfName = line.mid(10).trimmed();
                break;
            }
        }
        f.close();
    }

    CursorItem item;
    item.pos = QPoint(x, y);
    item.name = m_selfName;
    item.lastMs = QDateTime::currentMSecsSinceEpoch();
    m_cursors.insert(QStringLiteral("_self"), item);
    update();
}

void AnnotationOverlay::onViewerCursor(const QString &viewerId, int x, int y, const QString &viewerName)
{
    CursorItem item;
    item.pos = QPoint(x, y);
    item.name = viewerName;
    item.lastMs = QDateTime::currentMSecsSinceEpoch();
    m_cursors.insert(viewerId, item);
    update();
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
    auto drawStroke = [&](const Stroke &stroke) {
        if (stroke.points.size() < 2) return;
        
        QPen pen(colorForId(stroke.colorId));
        pen.setWidth(qMax(1, stroke.thickness));
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);

        if (stroke.smooth && stroke.points.size() > 2) {
            // Smooth freehand drawing with Bezier curves
            QPainterPath path;
            path.moveTo(stroke.points[0]);
            for (int i = 0; i < stroke.points.size() - 1; ++i) {
                QPoint p1 = stroke.points[i];
                QPoint p2 = stroke.points[i+1];
                QPoint midPoint = (p1 + p2) / 2;
                path.quadTo(p1, midPoint);
            }
            // Ensure we reach the last point
            path.lineTo(stroke.points.last());
            painter.drawPath(path);
        } else {
            // Shapes or short lines - draw as polyline to handle transparency correctly (no overlap at joints)
            painter.drawPolyline(stroke.points.constData(), stroke.points.size());
        }
    };

    for (const auto &stroke : m_strokes) {
        drawStroke(stroke);
    }
    // 绘制当前笔划
    if (m_currentStroke.points.size() >= 2) {
        drawStroke(m_currentStroke);
    }
    if (m_drawingRect) {
        QPen pen(colorForId(m_currentShapeColor));
        pen.setWidth(3);
        painter.setPen(pen);
        QPoint tl(qMin(m_rectStart.x(), m_rectEnd.x()), qMin(m_rectStart.y(), m_rectEnd.y()));
        QPoint br(qMax(m_rectStart.x(), m_rectEnd.x()), qMax(m_rectStart.y(), m_rectEnd.y()));
        painter.drawRect(QRect(tl, br));
    }
    if (m_drawingCircle) {
        QPen pen(colorForId(m_currentShapeColor));
        pen.setWidth(3);
        painter.setPen(pen);
        QPoint tl(qMin(m_circleStart.x(), m_circleEnd.x()), qMin(m_circleStart.y(), m_circleEnd.y()));
        QPoint br(qMax(m_circleStart.x(), m_circleEnd.x()), qMax(m_circleStart.y(), m_circleEnd.y()));
        painter.drawEllipse(QRect(tl, br));
    }
    if (m_drawingArrow) {
        QPen pen(colorForId(m_currentShapeColor));
        pen.setWidth(15);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        int dx = m_arrowEnd.x() - m_arrowStart.x();
        int dy = m_arrowEnd.y() - m_arrowStart.y();
        double len = std::sqrt(static_cast<double>(dx*dx + dy*dy));
        painter.drawLine(m_arrowStart, m_arrowEnd);
        if (len > 0.0) {
            double ux = dx / len;
            double uy = dy / len;
            double headLen = std::min(24.0, std::max(10.0, len * 0.25));
            double angle = 28.0 * 3.14159265358979323846 / 180.0;
            double cosA = std::cos(angle);
            double sinA = std::sin(angle);
            double lx = cosA * ux - sinA * uy;
            double ly = sinA * ux + cosA * uy;
            double rx = cosA * ux + sinA * uy;
            double ry = -sinA * ux + cosA * uy;
            QPoint left(m_arrowEnd.x() - static_cast<int>(lx * headLen), m_arrowEnd.y() - static_cast<int>(ly * headLen));
            QPoint right(m_arrowEnd.x() - static_cast<int>(rx * headLen), m_arrowEnd.y() - static_cast<int>(ry * headLen));
            QPoint points[3] = { left, m_arrowEnd, right };
            painter.drawPolyline(points, 3);
        }
    }
    // 绘制文本项
    for (const auto &t : m_texts) {
        QPen pen(colorForId(t.colorId));
        pen.setWidth(1);
        painter.setPen(pen);
        QFont font = painter.font();
        font.setPixelSize(t.fontSize);
        painter.setFont(font);
        painter.drawText(t.pos, t.text);
    }

    if (!m_cursors.isEmpty()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (auto it = m_cursors.constBegin(); it != m_cursors.constEnd(); ++it) {
            const QString viewerId = it.key();
            const CursorItem &ci = it.value();
            const QPixmap &pmBase = m_cursorSmall.isNull() ? m_cursorPixmap : m_cursorSmall;
            if (!pmBase.isNull()) {
                if (!m_cursorColors.contains(viewerId)) {
                    int r = QRandomGenerator::global()->bounded(40, 216);
                    int g = QRandomGenerator::global()->bounded(40, 216);
                    int b = QRandomGenerator::global()->bounded(40, 216);
                    m_cursorColors.insert(viewerId, QColor(r, g, b));
                }
                QColor col = m_cursorColors.value(viewerId);
                QPixmap tint(pmBase.size());
                tint.fill(col);
                QPainter tp(&tint);
                tp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                tp.drawPixmap(0, 0, pmBase);
                tp.end();
                painter.drawPixmap(ci.pos, tint);
            }
            if (!ci.name.isEmpty()) {
                QColor col = m_cursorColors.value(viewerId, QColor(200, 80, 200));
                QFont f = painter.font();
                const QPixmap &pm2 = m_cursorSmall.isNull() ? m_cursorPixmap : m_cursorSmall;
                int baseSize = qMax(12, pm2.height() / 2);
                f.setPixelSize(baseSize);
                painter.setFont(f);
                QPoint textPos(ci.pos.x() + pm2.width() + 6, ci.pos.y() + pm2.height() - 4);
                painter.setPen(QPen(col));
                painter.drawText(textPos, ci.name);
            }
        }
    }
}
void AnnotationOverlay::onTextAnnotation(const QString &text, int x, int y, const QString &viewerId, int colorId, int fontSize)
{
    Q_UNUSED(viewerId);
    if (text.isEmpty()) return;
    TextItem item; item.text = text; item.pos = QPoint(x, y); item.colorId = colorId; item.fontSize = fontSize;
    m_texts.push_back(item);
    OpEntry op; op.kind = 1; op.startIndex = m_texts.size() - 1; op.count = 1; m_ops.push_back(op);
    update();
}
void AnnotationOverlay::onViewerNameUpdate(const QString &viewerId, const QString &viewerName)
{
    auto it = m_cursors.find(viewerId);
    if (it != m_cursors.end()) {
        it->name = viewerName;
        it->lastMs = QDateTime::currentMSecsSinceEpoch();
        update();
    } else {
        CursorItem item;
        item.pos = QPoint(0, 0);
        item.name = viewerName;
        item.lastMs = QDateTime::currentMSecsSinceEpoch();
        m_cursors.insert(viewerId, item);
        update();
    }
}

void AnnotationOverlay::onViewerExited(const QString &viewerId)
{
    if (m_cursors.contains(viewerId)) {
        m_cursors.remove(viewerId);
    }
    if (m_cursorColors.contains(viewerId)) {
        m_cursorColors.remove(viewerId);
    }
    update();
}
