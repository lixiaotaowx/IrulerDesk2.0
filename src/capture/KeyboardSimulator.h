#ifndef KEYBOARDSIMULATOR_H
#define KEYBOARDSIMULATOR_H

#include <QObject>
#include <QString>

class KeyboardSimulator : public QObject
{
    Q_OBJECT
public:
    explicit KeyboardSimulator(QObject *parent = nullptr);

    // Simulate key press/release
    // type: "key_press" or "key_release"
    // key: Qt key code (optional backup)
    // modifiers: Qt modifiers (optional backup)
    // nativeScanCode: Windows hardware scan code (preferred)
    // text: Unicode text (optional)
    void simulateKey(const QString &type, int key, int modifiers, quint32 nativeScanCode, const QString &text);

private:
    void sendKeyInput(quint16 scanCode, bool keyUp, bool extended);
    quint16 mapQtKeyToScanCode(int key);
};

#endif // KEYBOARDSIMULATOR_H
