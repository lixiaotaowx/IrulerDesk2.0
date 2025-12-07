#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <QObject>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QMutex>
#include <QTimer>
#include <QByteArray>
#include <memory>

class RingAudioIODevice;

class AudioPlayer : public QObject
{
    Q_OBJECT
    friend class RingAudioIODevice;

public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer();

    // Audio control
    void setSpeakerEnabled(bool enabled);
    bool isSpeakerEnabled() const;
    
    void setVolumePercent(int percent);
    int volumePercent() const;
    
    // Output device selection
    void selectAudioOutputFollowSystem();
    void selectAudioOutputById(const QString &id);
    void selectAudioOutputByRawId(const QByteArray &id);
    bool isAudioOutputFollowSystem() const;
    QString currentAudioOutputId() const;
    void applyAudioOutputSelectionRuntime();

    // Audio data processing
    void processAudioData(const QByteArray &pcmData, int sampleRate, int channels, int bitsPerSample);
    
    // Cleanup
    void stop();

signals:
    void audioOutputSelectionChanged(bool followSystem, const QString &deviceId);

private slots:
    void onAudioOutputsChanged();
    void checkDefaultAudioOutput();

private:
    void initAudioSinkIfNeeded(int sampleRate, int channels, int bitsPerSample);
    QByteArray convertForSink(const QByteArray &srcPcm, int srcSr, int srcCh) const;
    void softRestartSpeakerIfEnabled();
    void forceRecreateSink();
    void hardSwitchOnSystemChange();

    // Configuration
    bool m_speakerEnabled = true;
    int m_volumePercent = 100;
    bool m_followSystemOutput = true;
    QByteArray m_outputDeviceId; // Target device ID if not following system
    QByteArray m_currentOutputDeviceId; // Actually used device ID

    // Audio State
    QAudioFormat m_audioFormat;
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_audioIO = nullptr;
    bool m_audioInitialized = false;
    QMediaDevices *m_mediaDevices = nullptr;
    QTimer *m_defaultOutPollTimer = nullptr;
    
    // Sink format
    int m_sinkSampleRate = 16000;
    int m_sinkChannels = 1;
    int m_bytesPerSample = 2;
    
    // Last frame info for resumption
    int m_lastFrameSampleRate = 16000;
    int m_lastFrameChannels = 1;
    int m_lastFrameBitsPerSample = 16;
    
    // Ring buffer
    QByteArray m_ringBuffer;
    QMutex m_ringMutex;
    
    bool m_needResample = false;
};

#endif // AUDIOPLAYER_H
