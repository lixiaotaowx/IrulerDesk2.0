#pragma once

#include <QObject>
#include <QAudioSource>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QIODevice>
#include <QTimer>

class MicLevelMonitor : public QObject
{
    Q_OBJECT
public:
    explicit MicLevelMonitor(QObject *parent = nullptr);
    ~MicLevelMonitor();

    void start();
    void stop();

signals:
    void levelChanged(float level);

private slots:
    void processAudio();

private:
    QScopedPointer<QAudioSource> m_audioSource;
    QIODevice *m_ioDevice = nullptr;
    QTimer *m_processTimer = nullptr;
    float m_smoothLevel = 0.0f;
};
