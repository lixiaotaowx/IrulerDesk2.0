#include "ViewerInputHandler.h"
#include "../player/WebSocketReceiver.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QDebug>

ViewerInputHandler::ViewerInputHandler(QObject *parent)
    : QObject(parent)
    , m_enabled(false)
{
}

void ViewerInputHandler::setVideoLabel(QLabel *label)
{
    if (m_videoLabel) {
        m_videoLabel->removeEventFilter(this);
    }
    m_videoLabel = label;
    if (m_videoLabel) {
        m_videoLabel->setMouseTracking(true);
        m_videoLabel->installEventFilter(this);
    }
}

void ViewerInputHandler::setReceiver(WebSocketReceiver *receiver)
{
    m_receiver = receiver;
}

void ViewerInputHandler::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void ViewerInputHandler::setSourceSize(const QSize &size)
{
    m_sourceSize = size;
}

bool ViewerInputHandler::eventFilter(QObject *obj, QEvent *event)
{
    if (!m_enabled || obj != m_videoLabel) {
        return QObject::eventFilter(obj, event);
    }

    switch (event->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
        handleMouseEvent(event);
        return false; // Allow event to propagate to allow local mouse handling (e.g. leaving window)
    case QEvent::ShortcutOverride:
        if (m_enabled) {
            event->accept(); // Prevent Qt from triggering local shortcuts (like Ctrl+Z, Ctrl+C)
            return true;
        }
        break;
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
        handleKeyEvent(event);
        return true; // Consume key events to prevent local shortcuts
    default:
        break;
    }

    return QObject::eventFilter(obj, event);
}

void ViewerInputHandler::handleMouseEvent(QEvent *event)
{
    if (!m_receiver) return;

    QString type;
    int x = 0;
    int y = 0;
    int button = 0;
    int delta = 0;

    if (QMouseEvent *me = dynamic_cast<QMouseEvent*>(event)) {
        QPoint remotePos = mapToRemote(me->pos());
        x = remotePos.x();
        y = remotePos.y();
        
        switch (event->type()) {
        case QEvent::MouseMove:
            type = "move";
            break;
        case QEvent::MouseButtonPress:
            type = "press";
            button = me->button();
            break;
        case QEvent::MouseButtonRelease:
            type = "release";
            button = me->button();
            break;
        case QEvent::MouseButtonDblClick:
            type = "dblclick"; 
            button = me->button();
            break;
        default:
            return;
        }
    } else if (QWheelEvent *we = dynamic_cast<QWheelEvent*>(event)) {
        QPoint remotePos = mapToRemote(we->position().toPoint());
        x = remotePos.x();
        y = remotePos.y();
        type = "wheel";
        delta = we->angleDelta().y();
    } else {
        return;
    }

    m_receiver->sendRemoteInput(type, x, y, button, delta);
    if (type == "press") {
        emit mouseClicked();
    }

    if (type != "move") {
         // qInfo() << "[DEBUG_MOUSE] ViewerInputHandler sent: " << type << x << y;
    }
}

void ViewerInputHandler::handleKeyEvent(QEvent *event)
{
    if (!m_receiver) return;

    QKeyEvent *ke = static_cast<QKeyEvent*>(event);
    QString type;
    if (event->type() == QEvent::KeyPress) {
        type = "key_press";
    } else if (event->type() == QEvent::KeyRelease) {
        type = "key_release";
    } else {
        return;
    }

    // Capture native scan code if available for better compatibility
    quint32 nativeScanCode = ke->nativeScanCode();
    int key = ke->key();
    int modifiers = ke->modifiers();
    QString text = ke->text();

    m_receiver->sendRemoteKeyInput(type, key, modifiers, nativeScanCode, text);
    qDebug() << "[Viewer] Key sent:" << type << key << "Scan:" << nativeScanCode;
}

QPoint ViewerInputHandler::mapToRemote(const QPoint &localPos)
{
    if (!m_videoLabel || !m_videoLabel->pixmap() || m_sourceSize.isEmpty()) {
        return QPoint(0, 0);
    }

    QSize labelSize = m_videoLabel->size();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPixmap pixmap = m_videoLabel->pixmap();
    if (pixmap.isNull()) return QPoint(0, 0);
    QSize displayedSize = pixmap.size();
#else
    const QPixmap *pixmap = m_videoLabel->pixmap();
    if (!pixmap || pixmap->isNull()) return QPoint(0, 0);
    QSize displayedSize = pixmap->size();
#endif
    
    int offsetX = (labelSize.width() - displayedSize.width()) / 2;
    int offsetY = (labelSize.height() - displayedSize.height()) / 2;
    
    int xInImg = localPos.x() - offsetX;
    int yInImg = localPos.y() - offsetY;
    
    // Clamp to image bounds
    if (xInImg < 0) xInImg = 0;
    if (xInImg >= displayedSize.width()) xInImg = displayedSize.width() - 1;
    if (yInImg < 0) yInImg = 0;
    if (yInImg >= displayedSize.height()) yInImg = displayedSize.height() - 1;
    
    double scaleX = (double)m_sourceSize.width() / displayedSize.width();
    double scaleY = (double)m_sourceSize.height() / displayedSize.height();
    
    return QPoint(xInImg * scaleX, yInImg * scaleY);
}
