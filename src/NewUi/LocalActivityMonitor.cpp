#include "LocalActivityMonitor.h"

#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QtGlobal>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static qint64 systemIdleMs()
{
#ifdef _WIN32
    LASTINPUTINFO info;
    info.cbSize = sizeof(info);
    if (GetLastInputInfo(&info)) {
        const ULONGLONG now = GetTickCount64();
        return static_cast<qint64>(now - info.dwTime);
    }
#endif
    return -1;
}

static bool isSignificantScreenChange(const QByteArray &current, const QByteArray &last)
{
    if (current.isEmpty()) {
        return false;
    }
    if (last.isEmpty()) {
        return true;
    }
    if (current.size() != last.size()) {
        return true;
    }
    const int size = current.size();
    int diffCount = 0;
    for (int i = 0; i < size; ++i) {
        const int delta = qAbs(static_cast<int>(static_cast<uchar>(current[i])) -
                               static_cast<int>(static_cast<uchar>(last[i])));
        if (delta > 12) {
            ++diffCount;
        }
    }
    const double ratio = size > 0 ? static_cast<double>(diffCount) / static_cast<double>(size) : 0.0;
    // 降低阈值到 1% 以检测较小的屏幕变化（如菜单弹出、滚动等），但仍能排除光标闪烁等微小噪点
    return ratio >= 0.01;
}

LocalActivityMonitor::LocalActivityMonitor(QObject *parent)
    : QObject(parent)
{
}

void LocalActivityMonitor::start()
{
    if (qApp) {
        qApp->installEventFilter(this);
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_lastMouseEventMs = nowMs;
    m_lastKeyboardEventMs = nowMs;
    m_lastScreenChangeMs = nowMs;
    m_lastEvaluateMs = nowMs;
    m_lastScreenFingerprint = buildScreensFingerprint();
    m_active = true;
    m_mouseScore = 1.0;
    m_keyScore = 1.0;
    m_screenScore = 1.0;

    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(10000);
        connect(m_timer, &QTimer::timeout, this, [this]() {
            evaluateActivity();
        });
    }
    m_timer->start();
}

bool LocalActivityMonitor::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    if (event) {
        const QEvent::Type type = event->type();
        if (type == QEvent::MouseMove || type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease ||
            type == QEvent::MouseButtonDblClick || type == QEvent::Wheel) {
            m_lastMouseEventMs = QDateTime::currentMSecsSinceEpoch();
            m_mouseScore = 1.0;
        } else if (type == QEvent::KeyPress || type == QEvent::KeyRelease) {
            m_lastKeyboardEventMs = QDateTime::currentMSecsSinceEpoch();
            m_keyScore = 1.0;
        }
    }
    return false;
}

void LocalActivityMonitor::evaluateActivity()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsedMs = m_lastEvaluateMs > 0 ? (now - m_lastEvaluateMs) : 0;
    m_lastEvaluateMs = now;
    const double decay = elapsedMs > 0 ? qMin(1.0, static_cast<double>(elapsedMs) / 10000.0) : 0.0;
    m_mouseScore = qMax(0.0, m_mouseScore - decay);
    m_keyScore = qMax(0.0, m_keyScore - decay);
    m_screenScore = qMax(0.0, m_screenScore - decay);
    const qint64 idleMs = systemIdleMs();
    if (idleMs >= 0 && idleMs < 10000) {
        m_mouseScore = 1.0;
        m_keyScore = 1.0;
    }
    const QByteArray fingerprint = buildScreensFingerprint();
    if (isSignificantScreenChange(fingerprint, m_lastScreenFingerprint)) {
        m_lastScreenFingerprint = fingerprint;
        m_lastScreenChangeMs = now;
        m_screenScore = 1.0;
    }
    const bool active = (m_mouseScore > 0.0) || (m_keyScore > 0.0) || (m_screenScore > 0.0);
    if (active != m_active) {
        m_active = active;
        emit activityStateChanged(active);
    }
}

QByteArray LocalActivityMonitor::buildScreensFingerprint() const
{
    QByteArray out;
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (!screen) {
            continue;
        }
        QPixmap pm = screen->grabWindow(0);
        if (pm.isNull()) {
            continue;
        }
    QImage img = pm.toImage().convertToFormat(QImage::Format_Grayscale8);
    if (!img.isNull()) {
        img = img.scaled(QSize(64, 36), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
        const int w = img.width();
        const int h = img.height();
        out.append(reinterpret_cast<const char*>(&w), sizeof(int));
        out.append(reinterpret_cast<const char*>(&h), sizeof(int));
    if (w > 0 && h > 0 && img.constBits()) {
        const int step = 4;
        for (int y = 0; y < h; ++y) {
            const uchar *line = img.constScanLine(y);
            for (int x = 0; x < w; x += step) {
                out.append(static_cast<char>(line[x]));
            }
        }
    }
    }
    return out;
}
