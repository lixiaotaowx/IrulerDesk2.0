#include "KeyboardSimulator.h"
#include <windows.h>
#include <QDebug>

KeyboardSimulator::KeyboardSimulator(QObject *parent)
    : QObject(parent)
{
}

void KeyboardSimulator::simulateKey(const QString &type, int key, int modifiers, quint32 nativeScanCode, const QString &text)
{
    bool keyUp = (type == "key_release");
    bool extended = false;
    
    // Check extended status based on key (common for both paths)
    switch (key) {
        case Qt::Key_Insert:
        case Qt::Key_Delete:
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
        case Qt::Key_Left:
        case Qt::Key_Up:
        case Qt::Key_Right:
        case Qt::Key_Down:
        case Qt::Key_NumLock:
        case Qt::Key_Print:
        // case Qt::Key_Divide: 
        case Qt::Key_Enter: 
        case Qt::Key_AltGr:
        case Qt::Key_Meta: 
            extended = true;
            break;
    }

    // Special handling for AltGr if not caught by switch
    if (key == Qt::Key_AltGr) {
        extended = true;
    }

    quint16 sc = 0;
    if (nativeScanCode > 0) {
        sc = (quint16)nativeScanCode;
    } else {
        sc = mapQtKeyToScanCode(key);
    }

    if (sc > 0) {
        sendKeyInput(sc, keyUp, extended);
    } else {
        qWarning() << "[KeyboardSim] No scan code for key:" << key;
    }
}

void KeyboardSimulator::sendKeyInput(quint16 scanCode, bool keyUp, bool extended)
{
    INPUT input;
    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = (WORD)scanCode;
    input.ki.dwFlags = KEYEVENTF_SCANCODE;
    
    if (keyUp) {
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    if (extended) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }

    UINT uSent = SendInput(1, &input, sizeof(INPUT));
    if (uSent != 1) {
        qWarning() << "[KeyboardSim] SendInput failed. Error:" << GetLastError() << " Scan:" << scanCode;
    } else {
        qDebug() << "[KeyboardSim] Simulated Scan:" << scanCode << " Up:" << keyUp << " Ext:" << extended;
    }
}

quint16 KeyboardSimulator::mapQtKeyToScanCode(int key)
{
    // 简单的备用映射，仅当 nativeScanCode 为 0 时使用
    switch (key) {
        case Qt::Key_Space: return 0x39;
        case Qt::Key_Backspace: return 0x0E;
        case Qt::Key_Tab: return 0x0F;
        case Qt::Key_Return: return 0x1C;
        case Qt::Key_Escape: return 0x01;
        
        // Modifiers
        case Qt::Key_Shift: return 0x2A;
        case Qt::Key_Control: return 0x1D;
        case Qt::Key_Alt: return 0x38;
        case Qt::Key_AltGr: return 0x38;
        case Qt::Key_Meta: return 0x5B; // Left Win
        
        // A-Z
        case Qt::Key_A: return 0x1E;
        case Qt::Key_B: return 0x30;
        case Qt::Key_C: return 0x2E;
        case Qt::Key_D: return 0x20;
        case Qt::Key_E: return 0x12;
        case Qt::Key_F: return 0x21;
        case Qt::Key_G: return 0x22;
        case Qt::Key_H: return 0x23;
        case Qt::Key_I: return 0x17;
        case Qt::Key_J: return 0x24;
        case Qt::Key_K: return 0x25;
        case Qt::Key_L: return 0x26;
        case Qt::Key_M: return 0x32;
        case Qt::Key_N: return 0x31;
        case Qt::Key_O: return 0x18;
        case Qt::Key_P: return 0x19;
        case Qt::Key_Q: return 0x10;
        case Qt::Key_R: return 0x13;
        case Qt::Key_S: return 0x1F;
        case Qt::Key_T: return 0x14;
        case Qt::Key_U: return 0x16;
        case Qt::Key_V: return 0x2F;
        case Qt::Key_W: return 0x11;
        case Qt::Key_X: return 0x2D;
        case Qt::Key_Y: return 0x15;
        case Qt::Key_Z: return 0x2C;
        
        // 0-9
        case Qt::Key_0: return 0x0B;
        case Qt::Key_1: return 0x02;
        case Qt::Key_2: return 0x03;
        case Qt::Key_3: return 0x04;
        case Qt::Key_4: return 0x05;
        case Qt::Key_5: return 0x06;
        case Qt::Key_6: return 0x07;
        case Qt::Key_7: return 0x08;
        case Qt::Key_8: return 0x09;
        case Qt::Key_9: return 0x0A;
        
        // F1-F12
        case Qt::Key_F1: return 0x3B;
        case Qt::Key_F2: return 0x3C;
        case Qt::Key_F3: return 0x3D;
        case Qt::Key_F4: return 0x3E;
        case Qt::Key_F5: return 0x3F;
        case Qt::Key_F6: return 0x40;
        case Qt::Key_F7: return 0x41;
        case Qt::Key_F8: return 0x42;
        case Qt::Key_F9: return 0x43;
        case Qt::Key_F10: return 0x44;
        case Qt::Key_F11: return 0x57;
        case Qt::Key_F12: return 0x58;
        
        default: return 0;
    }
}
