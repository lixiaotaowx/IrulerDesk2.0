#ifndef RAINBOWTOOLBUTTON_H
#define RAINBOWTOOLBUTTON_H

#include <QPushButton>
#include <QTimer>

class RainbowToolButton : public QPushButton
{
    Q_OBJECT
public:
    explicit RainbowToolButton(QWidget *parent = nullptr);
    ~RainbowToolButton() override;

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void updateAnimation();

private:
    QTimer *m_timer;
    int m_angle;
};

#endif // RAINBOWTOOLBUTTON_H
