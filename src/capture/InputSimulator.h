#ifndef INPUTSIMULATOR_H
#define INPUTSIMULATOR_H

#include <QObject>
#include <QSize>
#include <QRect>
#include <QString>

class InputSimulator : public QObject
{
    Q_OBJECT
public:
    explicit InputSimulator(QObject *parent = nullptr);
    
    // 设置目标屏幕区域和编码尺寸（用于多屏坐标映射和缩放）
    void setScreenRect(const QRect &rect, const QSize &encodeSize);

public slots:
    // 处理远程输入事件
    // type: "move", "press", "release", "wheel", "dblclick"
    // button: 1=Left, 2=Right, 4=Middle
    void onInputEvent(const QString &type, int x, int y, int button, int delta);

private:
    QRect m_screenRect;
    QSize m_encodeSize;
    
    // Windows API helper
    void sendInput(int x, int y, int flags, int mouseData = 0);
};

#endif // INPUTSIMULATOR_H
