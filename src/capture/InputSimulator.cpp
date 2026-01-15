#include "InputSimulator.h"
#include <windows.h>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>

InputSimulator::InputSimulator(QObject *parent)
    : QObject(parent)
    , m_screenRect(QRect(0, 0, 1920, 1080))
    , m_encodeSize(QSize(1920, 1080))
{
}

void InputSimulator::setScreenRect(const QRect &rect, const QSize &encodeSize)
{
    if (rect.isValid()) {
        m_screenRect = rect;
    }
    if (encodeSize.isValid()) {
        m_encodeSize = encodeSize;
    }
}

void InputSimulator::onInputEvent(const QString &type, int x, int y, int button, int delta)
{
    int commonFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE;

    if (type == "move") {
        sendInput(x, y, commonFlags);
    } else if (type == "press") {
        int flags = commonFlags;
        if (button == 1) flags |= MOUSEEVENTF_LEFTDOWN;
        else if (button == 2) flags |= MOUSEEVENTF_RIGHTDOWN;
        else if (button == 4) flags |= MOUSEEVENTF_MIDDLEDOWN;
        
        sendInput(x, y, flags);
    } else if (type == "release") {
        int flags = commonFlags;
        if (button == 1) flags |= MOUSEEVENTF_LEFTUP;
        else if (button == 2) flags |= MOUSEEVENTF_RIGHTUP;
        else if (button == 4) flags |= MOUSEEVENTF_MIDDLEUP;
        
        sendInput(x, y, flags);
    } else if (type == "dblclick") {
        // Qt sends Press -> Release -> DblClick -> Release
        // So we treat DblClick as the second Press
        onInputEvent("press", x, y, button, 0);
    } else if (type == "wheel") {
        // Move mouse to position first
        sendInput(x, y, MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE);
        // Then scroll
        sendInput(x, y, MOUSEEVENTF_WHEEL, delta);
    }
}

void InputSimulator::sendInput(int x, int y, int flags, int mouseData)
{
    INPUT input;
    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_MOUSE;
    
    // Calculate absolute coordinates on the virtual desktop
    // Scale input coordinates from encode space to physical screen space
    int scaledX = x;
    int scaledY = y;

    if (m_encodeSize.isValid() && !m_encodeSize.isEmpty()) {
        // Use double for precision during scaling
        double scaleX = (double)m_screenRect.width() / (double)m_encodeSize.width();
        double scaleY = (double)m_screenRect.height() / (double)m_encodeSize.height();
        
        scaledX = qRound(x * scaleX);
        scaledY = qRound(y * scaleY);
    }

    int absX = m_screenRect.x() + scaledX;
    int absY = m_screenRect.y() + scaledY;

    // Get Virtual Desktop metrics from Qt (Logical) to match m_screenRect (Logical)
    int vLeft = 0;
    int vTop = 0;
    int vWidth = 1920;
    int vHeight = 1080;

    QScreen *primary = QGuiApplication::primaryScreen();
    if (primary) {
        QRect virtualGeo = primary->virtualGeometry();
        vLeft = virtualGeo.x();
        vTop = virtualGeo.y();
        vWidth = virtualGeo.width();
        vHeight = virtualGeo.height();
    } else {
        // Fallback to System Metrics if Qt fails (unlikely)
        vLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
        vTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
        vWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        vHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }

    if (vWidth == 0) vWidth = 1920;
    if (vHeight == 0) vHeight = 1080;

    // Normalize to 0-65535 based on Virtual Desktop
    input.mi.dx = (long)((double)(absX - vLeft) * 65535.0 / (double)vWidth);
    input.mi.dy = (long)((double)(absY - vTop) * 65535.0 / (double)vHeight);
    
    if ((flags & MOUSEEVENTF_MOVE) == 0) {
         // qInfo() << "[DEBUG_MOUSE] SendInput: x=" << x << " y=" << y << " absX=" << absX << " dx=" << input.mi.dx;
    }

    input.mi.dwFlags = flags;
    input.mi.mouseData = mouseData;
    input.mi.dwExtraInfo = GetMessageExtraInfo();
    
    if (flags & MOUSEEVENTF_WHEEL) {
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.dx = 0; 
        input.mi.dy = 0;
    }

    UINT result = SendInput(1, &input, sizeof(INPUT));
    if (result == 0) {
        qWarning() << "[DEBUG_MOUSE] SendInput failed. Error:" << GetLastError();
    }
}
