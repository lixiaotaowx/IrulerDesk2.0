#include "CursorOverlay.h"
#include <QPainter>
#include <QPaintEvent>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QRandomGenerator>

CursorOverlay::CursorOverlay(QWidget *parent)
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

void CursorOverlay::alignToScreen(QScreen *screen)
{
    if (!screen) return;
    setGeometry(screen->geometry());
}

void CursorOverlay::clear()
{
    m_cursors.clear();
    update();
}

void CursorOverlay::onCursorMoved(int x, int y)
{
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

void CursorOverlay::onViewerCursor(const QString &viewerId, int x, int y, const QString &viewerName)
{
    CursorItem item;
    item.pos = QPoint(x, y);
    item.name = viewerName;
    item.lastMs = QDateTime::currentMSecsSinceEpoch();
    m_cursors.insert(viewerId, item);
    update();
}

void CursorOverlay::onViewerNameUpdate(const QString &viewerId, const QString &viewerName)
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

void CursorOverlay::onViewerExited(const QString &viewerId)
{
    if (m_cursors.contains(viewerId)) {
        m_cursors.remove(viewerId);
    }
    if (m_cursorColors.contains(viewerId)) {
        m_cursorColors.remove(viewerId);
    }
    update();
}

void CursorOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
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
