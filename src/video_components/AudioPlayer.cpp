#include "AudioPlayer.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <QDebug>
#include <cmath>
#include <cstring>

// RingAudioIODevice removed - switching to Push mode
// AudioPlayer manages writing to the sink's IO device directly

AudioPlayer::AudioPlayer(QObject *parent) : QObject(parent)
{
    m_mediaDevices = new QMediaDevices(this);
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged, this, &AudioPlayer::onAudioOutputsChanged);
    
    m_defaultOutPollTimer = new QTimer(this);
    m_defaultOutPollTimer->setInterval(500);
    connect(m_defaultOutPollTimer, &QTimer::timeout, this, &AudioPlayer::checkDefaultAudioOutput);
    m_defaultOutPollTimer->start();
}

AudioPlayer::~AudioPlayer()
{
    stop();
}

void AudioPlayer::stop()
{
    // 停止设备轮询定时器，防止在停止过程中重建Sink
    if (m_defaultOutPollTimer) {
        m_defaultOutPollTimer->stop();
    }

    if (m_audioSink) {
        if (m_audioSink->state() != QAudio::StoppedState) {
            m_audioSink->stop();
        }
        delete m_audioSink;
        m_audioSink = nullptr;
    }
    // In Push mode, m_audioIO is managed by m_audioSink (created by start())
    // but checking docs: start() returns a pointer to internal QIODevice.
    // We do NOT delete it manually usually, QAudioSink manages it?
    // Actually, start() returns a pointer to a QIODevice that the sink uses.
    // We should not delete it if we don't own it.
    // But wait, QAudioSink::start() returns a QIODevice* that we write to.
    // The sink owns it. We just set m_audioIO to nullptr.
    m_audioIO = nullptr;
    
    m_audioInitialized = false;
    
    // QMutexLocker locker(&m_ringMutex); // No longer needed
    // m_ringBuffer.clear();
}

void AudioPlayer::setSpeakerEnabled(bool enabled)
{
    if (m_speakerEnabled != enabled) {
        m_speakerEnabled = enabled;
        softRestartSpeakerIfEnabled();
    }
}

bool AudioPlayer::isSpeakerEnabled() const
{
    return m_speakerEnabled;
}

void AudioPlayer::setVolumePercent(int percent)
{
    m_volumePercent = percent;
    if (m_audioSink) {
        qreal vol = qreal(m_volumePercent) / 100.0;
        m_audioSink->setVolume(vol);
    }
}

int AudioPlayer::volumePercent() const
{
    return m_volumePercent;
}

void AudioPlayer::selectAudioOutputFollowSystem()
{
    if (m_followSystemOutput) return;
    m_followSystemOutput = true;
    m_outputDeviceId.clear();
    hardSwitchOnSystemChange();
    emit audioOutputSelectionChanged(true, QString());
}

void AudioPlayer::selectAudioOutputById(const QString &id)
{
    selectAudioOutputByRawId(id.toUtf8());
}

void AudioPlayer::selectAudioOutputByRawId(const QByteArray &id)
{
    if (!m_followSystemOutput && m_outputDeviceId == id) return;
    m_followSystemOutput = false;
    m_outputDeviceId = id;
    hardSwitchOnSystemChange();
    emit audioOutputSelectionChanged(false, QString::fromUtf8(m_outputDeviceId));
}

bool AudioPlayer::isAudioOutputFollowSystem() const
{
    return m_followSystemOutput;
}

QString AudioPlayer::currentAudioOutputId() const
{
    return QString::fromUtf8(m_currentOutputDeviceId);
}

void AudioPlayer::applyAudioOutputSelectionRuntime()
{
    hardSwitchOnSystemChange();
}

void AudioPlayer::processAudioData(const QByteArray &pcmData, int sampleRate, int channels, int bitsPerSample)
{
    if (pcmData.isEmpty()) return;

    static int procCount = 0;
    procCount++;
    if (procCount % 500 == 0) {
        qDebug() << "[Player] Process Audio Data #" << procCount << " Size:" << pcmData.size() << " SR:" << sampleRate;
    }

    // 更新最后一次帧参数
    m_lastFrameSampleRate = sampleRate;
    m_lastFrameChannels = channels;
    m_lastFrameBitsPerSample = bitsPerSample;

    if (!m_speakerEnabled) return;

    initAudioSinkIfNeeded(sampleRate, channels, bitsPerSample);

    if (m_audioSink) {
        if (m_audioSink->state() == QAudio::StoppedState) {
            qDebug() << "[Player] AudioSink stopped unexpectedly. State:" << m_audioSink->state()
                     << " Error:" << m_audioSink->error() << " -> Restarting...";
            m_audioIO = m_audioSink->start();
        } else if (m_audioSink->state() == QAudio::SuspendedState) {
            m_audioSink->resume();
        }
    }

    QByteArray finalData;
    if (m_needResample) {
        finalData = convertForSink(pcmData, sampleRate, channels);
    } else {
        finalData = pcmData;
    }

    if (!finalData.isEmpty()) {
        if (m_audioSink && m_audioIO) {
            {
                QMutexLocker locker(&m_ringMutex);
                m_ringBuffer.append(finalData);
                while (!m_ringBuffer.isEmpty()) {
                    qint64 w = m_audioIO->write(m_ringBuffer.constData(), m_ringBuffer.size());
                    if (w <= 0) break;
                    m_ringBuffer.remove(0, int(w));
                }
            }
            static int fullCount = 0;
            if (!m_ringBuffer.isEmpty() && ++fullCount % 20 == 0) {
                qDebug() << "[Player] Sink backlog bytes:" << m_ringBuffer.size()
                         << " State:" << m_audioSink->state()
                         << " Error:" << m_audioSink->error();
            }
        }
    }
}

void AudioPlayer::initAudioSinkIfNeeded(int sampleRate, int channels, int bitsPerSample)
{
    if (m_audioInitialized && m_audioSink) {
        // 检查参数是否变化导致需要重建
        // 简单起见，如果采样率变化，我们只更新重采样标志，不重建Sink，除非必要
        // 但如果Sink当前配置完全不兼容，可能需要重建。
        // 目前策略：Sink建立后尽量复用，通过convertForSink适配
        
        // 检查是否需要更新重采样标志
        if (sampleRate != m_sinkSampleRate || channels != m_sinkChannels) {
            m_needResample = true;
        } else {
             // 如果源格式和Sink格式一致，且Sink是Int16，源也是Int16，则不需要重采样
             // 但这里我们简单判断：只要采样率或通道不同就重采样。
             // 另外还要看bitsPerSample，如果是16位且Sink是Int16
             bool srcIsInt16 = (bitsPerSample == 16);
             bool sinkIsInt16 = (m_audioFormat.sampleFormat() == QAudioFormat::Int16);
             if (srcIsInt16 && sinkIsInt16) {
                 m_needResample = false;
             } else {
                 m_needResample = true;
             }
        }
        return;
    }

    // 确保设备轮询定时器运行
    if (m_defaultOutPollTimer && !m_defaultOutPollTimer->isActive()) {
        m_defaultOutPollTimer->start();
    }

    // 选择设备
    QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (!m_followSystemOutput && !m_outputDeviceId.isEmpty()) {
        const auto devices = QMediaDevices::audioOutputs();
        for (const auto &d : devices) {
            if (d.id() == m_outputDeviceId) {
                device = d;
                break;
            }
        }
    }
    m_currentOutputDeviceId = device.id();

    // 格式协商：优先使用源数据的格式，以避免不必要的重采样
    // 这对于消除因线性插值重采样导致的“滋滋”噪音至关重要
    QAudioFormat desiredFormat;
    desiredFormat.setSampleRate(sampleRate > 0 ? sampleRate : 48000);
    desiredFormat.setChannelCount(channels > 0 ? channels : 2);
    desiredFormat.setSampleFormat(QAudioFormat::Int16); // 目前源数据通常是 Int16

    if (device.isFormatSupported(desiredFormat)) {
        m_audioFormat = desiredFormat;
        qDebug() << "[Player] Device supports source format, using:" << m_audioFormat;
    } else {
        // 如果不支持源格式，回退到设备首选格式
        QAudioFormat preferred = device.preferredFormat();
        qDebug() << "[Player] Device does not support source format:" << desiredFormat 
                 << " Falling back to preferred:" << preferred;
        
        m_audioFormat = preferred;
        
        // 尝试保持 Int16 格式，因为我们的重采样算法对 Int16 优化
        QAudioFormat preferredInt16 = preferred;
        preferredInt16.setSampleFormat(QAudioFormat::Int16);
        if (device.isFormatSupported(preferredInt16)) {
             m_audioFormat = preferredInt16;
        }
        
        // 兜底检查
        if (m_audioFormat.sampleRate() <= 0) m_audioFormat.setSampleRate(48000);
        if (m_audioFormat.channelCount() <= 0) m_audioFormat.setChannelCount(2);
        
        // 最终检查支持情况
        if (!device.isFormatSupported(m_audioFormat)) {
             m_audioFormat = device.preferredFormat();
        }
    }

    m_sinkSampleRate = m_audioFormat.sampleRate();
    m_sinkChannels = m_audioFormat.channelCount();
    switch (m_audioFormat.sampleFormat()) {
        case QAudioFormat::Int16: m_bytesPerSample = 2; break;
        case QAudioFormat::Float: m_bytesPerSample = 4; break;
        case QAudioFormat::UInt8: m_bytesPerSample = 1; break;
        default: m_bytesPerSample = 2; break;
    }
    
    qDebug() << "[Player] Initializing AudioSink. Device:" << device.description() 
             << " Format:" << m_audioFormat 
             << " Preferred:" << device.preferredFormat();

    // 判断是否需要重采样
    if (sampleRate != m_sinkSampleRate || channels != m_sinkChannels || bitsPerSample != 16) {
        m_needResample = true;
    } else {
         bool sinkIsInt16 = (m_audioFormat.sampleFormat() == QAudioFormat::Int16);
         m_needResample = !sinkIsInt16;
    }

    m_audioSink = new QAudioSink(device, m_audioFormat, this);
    m_audioSink->setBufferSize(m_sinkSampleRate * m_sinkChannels * m_bytesPerSample / 5);
    m_audioSink->setVolume(qreal(m_volumePercent) / 100.0);

    // Push Mode: start() returns the QIODevice we write to
    m_audioIO = m_audioSink->start();
    
    m_audioInitialized = true;
}

QByteArray AudioPlayer::convertForSink(const QByteArray &srcPcm, int srcSr, int srcCh) const
{
    if (srcPcm.isEmpty()) return srcPcm;
    const int16_t *in = reinterpret_cast<const int16_t*>(srcPcm.constData());
    int totalSamples = srcPcm.size() / 2;
    int srcFrames = srcCh > 0 ? totalSamples / srcCh : 0;
    if (srcFrames <= 0) return srcPcm;
    
    // 防止除零
    if (srcSr <= 0) srcSr = 16000;

    int dstFrames = int(std::llround(double(srcFrames) * double(m_sinkSampleRate) / double(srcSr)));
    if (dstFrames <= 0) return QByteArray();
    int dstCh = m_sinkChannels;
    QByteArray out;
    bool toFloat = (m_audioFormat.sampleFormat() == QAudioFormat::Float);
    if (!toFloat) {
        out.resize(dstFrames * dstCh * 2);
        int16_t *outp = reinterpret_cast<int16_t*>(out.data());
        for (int f = 0; f < dstFrames; ++f) {
            double pos = double(f) * double(srcSr) / double(m_sinkSampleRate);
            int i = int(pos);
            if (i >= srcFrames) i = srcFrames - 1;
            double frac = pos - double(i);
            auto sampleAt = [&](int chIndex)->int {
                int i0 = i * srcCh + chIndex;
                int i1 = (i + 1 < srcFrames ? (i + 1) : i) * srcCh + chIndex;
                int s0 = in[i0];
                int s1 = in[i1];
                int s = int(std::llround(double(s0) + (double(s1 - s0) * frac)));
                if (s > 32767) s = 32767; if (s < -32768) s = -32768;
                return s;
            };
            int l = (srcCh == 1) ? sampleAt(0) : sampleAt(0);
            int r = (srcCh == 1) ? l : sampleAt(1);
            if (dstCh == 1) {
                int m = (srcCh == 2) ? ((l + r) / 2) : l;
                outp[f] = int16_t(m);
            } else {
                outp[f * 2] = int16_t(l);
                outp[f * 2 + 1] = int16_t(r);
            }
        }
    } else {
        out.resize(dstFrames * dstCh * 4);
        float *outp = reinterpret_cast<float*>(out.data());
        auto toFloat32 = [](int s)->float {
            return float(s) / 32768.0f;
        };
        for (int f = 0; f < dstFrames; ++f) {
            double pos = double(f) * double(srcSr) / double(m_sinkSampleRate);
            int i = int(pos);
            if (i >= srcFrames) i = srcFrames - 1;
            double frac = pos - double(i);
            auto sampleAt = [&](int chIndex)->int {
                int i0 = i * srcCh + chIndex;
                int i1 = (i + 1 < srcFrames ? (i + 1) : i) * srcCh + chIndex;
                int s0 = in[i0];
                int s1 = in[i1];
                int s = int(std::llround(double(s0) + (double(s1 - s0) * frac)));
                if (s > 32767) s = 32767; if (s < -32768) s = -32768;
                return s;
            };
            int l16 = (srcCh == 1) ? sampleAt(0) : sampleAt(0);
            int r16 = (srcCh == 1) ? l16 : sampleAt(1);
            if (dstCh == 1) {
                int m16 = (srcCh == 2) ? ((l16 + r16) / 2) : l16;
                outp[f] = toFloat32(m16);
            } else {
                outp[f * 2] = toFloat32(l16);
                outp[f * 2 + 1] = toFloat32(r16);
            }
        }
    }
    return out;
}

void AudioPlayer::softRestartSpeakerIfEnabled()
{
    if (!m_speakerEnabled) {
        if (m_audioSink) {
            m_audioSink->stop();
            // In Push mode, start() device is invalid after stop
            m_audioIO = nullptr; 
        }
    } else {
        if (m_audioInitialized && m_audioSink && m_audioSink->state() != QAudio::ActiveState) {
            m_audioIO = m_audioSink->start();
        } else if (!m_audioInitialized) {
            // 将在下一次数据到来时初始化
        }
    }
}

void AudioPlayer::forceRecreateSink()
{
    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
    }
    if (m_audioIO) {
        delete m_audioIO;
        m_audioIO = nullptr;
    }
    m_audioInitialized = false;
    
    // 使用最后一次的格式参数尝试重建
    initAudioSinkIfNeeded(m_lastFrameSampleRate, m_lastFrameChannels, m_lastFrameBitsPerSample);
}

void AudioPlayer::hardSwitchOnSystemChange()
{
    forceRecreateSink();
}

void AudioPlayer::onAudioOutputsChanged()
{
    if (m_followSystemOutput) {
        hardSwitchOnSystemChange();
    }
}

void AudioPlayer::checkDefaultAudioOutput()
{
    if (!m_followSystemOutput) return;
    QAudioDevice def = QMediaDevices::defaultAudioOutput();
    if (m_currentOutputDeviceId != def.id()) {
        hardSwitchOnSystemChange();
    }
}
