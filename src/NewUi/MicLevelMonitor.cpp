#include "MicLevelMonitor.h"
#include <QAudioFormat>
#include <QtMath>
#include <QDebug>

MicLevelMonitor::MicLevelMonitor(QObject *parent)
    : QObject(parent)
{
}

MicLevelMonitor::~MicLevelMonitor()
{
    stop();
}

void MicLevelMonitor::start()
{
    stop();

    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        qWarning() << "No default audio input device found.";
        return;
    }

    QAudioFormat format;
    format.setSampleRate(8000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    if (!inputDevice.isFormatSupported(format)) {
        qWarning() << "Default audio format not supported. Trying preferred format.";
        format = inputDevice.preferredFormat();
    }

    m_audioSource.reset(new QAudioSource(inputDevice, format));
    // Use a small buffer size for low latency updates
    m_audioSource->setBufferSize(1024);

    m_ioDevice = m_audioSource->start();
    
    if (!m_processTimer) {
        m_processTimer = new QTimer(this);
        connect(m_processTimer, &QTimer::timeout, this, &MicLevelMonitor::processAudio);
    }
    m_smoothLevel = 0.0f;
    m_processTimer->start(50); // Check every 50ms
}

void MicLevelMonitor::stop()
{
    if (m_processTimer) {
        m_processTimer->stop();
    }
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource.reset();
    }
    m_ioDevice = nullptr;
    m_smoothLevel = 0.0f;
    emit levelChanged(0.0f);
}

void MicLevelMonitor::processAudio()
{
    if (!m_ioDevice || !m_audioSource) return;

    const qint64 bytesReady = m_audioSource->bytesAvailable();
    if (bytesReady == 0) return;

    QByteArray data = m_ioDevice->readAll();
    if (data.isEmpty()) return;

    // Calculate RMS amplitude
    double rms = 0.0;
    
    QAudioFormat fmt = m_audioSource->format();
    if (fmt.sampleFormat() == QAudioFormat::Float) {
        const float *samples = reinterpret_cast<const float*>(data.constData());
        int sampleCount = data.size() / sizeof(float);
        if (sampleCount <= 0) return;

        double sum = 0.0;
        for (int i = 0; i < sampleCount; ++i) sum += samples[i];
        double avg = sum / sampleCount;

        double sumSq = 0.0;
        for (int i = 0; i < sampleCount; ++i) {
            double val = samples[i] - avg;
            sumSq += val * val;
        }
        rms = qSqrt(sumSq / sampleCount);
        // Float is already normalized -1.0 to 1.0 (usually), but sometimes it exceeds.
        // RMS of full sine wave is 0.707.
    } 
    else if (fmt.sampleFormat() == QAudioFormat::UInt8) {
        const quint8 *samples = reinterpret_cast<const quint8*>(data.constData());
        int sampleCount = data.size() / sizeof(quint8);
        if (sampleCount <= 0) return;

        double sum = 0.0;
        for (int i = 0; i < sampleCount; ++i) sum += (samples[i] - 128);
        double avg = sum / sampleCount;

        double sumSq = 0.0;
        for (int i = 0; i < sampleCount; ++i) {
            double val = (samples[i] - 128) - avg;
            sumSq += val * val;
        }
        rms = qSqrt(sumSq / sampleCount);
        // Normalize to 0-1 (approx)
        // Max amplitude is 128.
        rms = rms / 128.0;
    }
    else {
        // Assume Int16 (default or fallback)
        const qint16 *samples = reinterpret_cast<const qint16*>(data.constData());
        int sampleCount = data.size() / sizeof(qint16);
        if (sampleCount <= 0) return;

        double sum = 0.0;
        for (int i = 0; i < sampleCount; ++i) sum += samples[i];
        double avg = sum / sampleCount;

        double sumSq = 0.0;
        for (int i = 0; i < sampleCount; ++i) {
            double val = samples[i] - avg;
            sumSq += val * val;
        }
        rms = qSqrt(sumSq / sampleCount);
        // Normalize to 0-1
        rms = rms / 32768.0;
    }
    
    // Normalize RMS to 0-1
    float rmsNormal = static_cast<float>(rms);
    
    // Convert to Decibels (dB)
    // Avoid log(0)
    float db = -100.0f;
    if (rmsNormal > 0.000001f) {
        db = 20.0f * std::log10(rmsNormal);
    }

    // Map dB to 0.0 - 1.0 range
    // Range: -60dB (silence) to -10dB (max)
    // Adjusted based on feedback: "always 80-90%" -> Input is loud or noise floor is high.
    // -60dB is a lower floor. -10dB as max means you have to speak reasonably loud to hit 100%.
    const float minDb = -60.0f;
    const float maxDb = -5.0f;
    
    float targetLevel = 0.0f;
    if (db < minDb) {
        targetLevel = 0.0f;
    } else if (db > maxDb) {
        targetLevel = 1.0f;
    } else {
        targetLevel = (db - minDb) / (maxDb - minDb);
    }

    // Apply smoothing (Attack / Decay)
    // Instant attack for responsiveness
    if (targetLevel > m_smoothLevel) {
        m_smoothLevel = targetLevel; 
    } else {
        // Linear decay: drop by 0.05 per 50ms (takes ~1 sec to fall from 1.0 to 0.0)
        m_smoothLevel -= 0.05f;
        if (m_smoothLevel < 0.0f) {
            m_smoothLevel = 0.0f;
        }
        // If target is really low (silence), let it drop faster? 
        // Or stick to linear decay for "VU meter" feel.
    }
    
    // Force zero if below noise floor to avoid flickering at bottom
    if (m_smoothLevel < 0.05f) {
        m_smoothLevel = 0.0f;
    }
    
    emit levelChanged(m_smoothLevel);
}
