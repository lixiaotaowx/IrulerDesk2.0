#pragma once

#include <QWidget>

class VolumeLevelBar : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(float level READ level WRITE setLevel)

public:
    explicit VolumeLevelBar(QWidget *parent = nullptr);
    ~VolumeLevelBar() override = default;

    float level() const { return m_level; }
    void setLevel(float level);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    float m_level = 0.0f;
    int m_barCount = 10;
    QColor m_activeColor;
    QColor m_inactiveColor;
};
