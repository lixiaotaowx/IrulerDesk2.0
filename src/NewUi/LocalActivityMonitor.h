#pragma once
#include <QObject>
#include <QByteArray>

class QTimer;

class LocalActivityMonitor : public QObject
{
    Q_OBJECT

public:
    explicit LocalActivityMonitor(QObject *parent = nullptr);
    void start();

signals:
    void activityStateChanged(bool active);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QByteArray buildScreensFingerprint() const;
    void evaluateActivity();

    QTimer *m_timer = nullptr;
    qint64 m_lastMouseEventMs = 0;
    qint64 m_lastKeyboardEventMs = 0;
    qint64 m_lastScreenChangeMs = 0;
    qint64 m_lastEvaluateMs = 0;
    double m_mouseScore = 0.0;
    double m_keyScore = 0.0;
    double m_screenScore = 0.0;
    QByteArray m_lastScreenFingerprint;
    bool m_active = true;
};
