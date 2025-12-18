#ifndef WEBSOCKETRECEIVER_H
#define WEBSOCKETRECEIVER_H

#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QtMultimedia/QAudioSource>
#include <QtMultimedia/QAudioSink>
#include <QtMultimedia/QAudioFormat>
#include <QtMultimedia/QMediaDevices>
#include <QUrl>
#include <QByteArray>
#include <QRecursiveMutex>
#include <QPoint>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QQueue>
#include <opus/opus.h>

class WebSocketReceiver : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketReceiver(QObject *parent = nullptr);
    ~WebSocketReceiver();
    
    // 连接到WebSocket服务器
    bool connectToServer(const QString &url);
    
    // 发送观看请求
    void sendWatchRequest(const QString &viewerId, const QString &targetId);
    
    // 设置会话信息（不发送网络请求，仅用于本地状态记录，如viewerId/targetId）
    // 用于在信令通道已经完成鉴权和同意流程后，初始化接收端状态
    void setSessionInfo(const QString &viewerId, const QString &targetId);
    
    // 发送远程批注事件：phase为"down"/"move"/"up"，坐标为源屏幕坐标，附带颜色ID
    void sendAnnotationEvent(const QString &phase, int x, int y, int colorId = 0);
    void sendTextAnnotation(const QString &text, int x, int y, int colorId, int fontSize);
    // 发送切换屏幕请求（滚动到下一屏幕）
    void sendSwitchScreenNext();
    // 按索引切换屏幕（不断流热切换，不修改配置）
    void sendSwitchScreenIndex(int index);
    // 发送质量设置（高/中/低）控制被观看者的编码质量
    void sendSetQuality(const QString &quality);
    // 发送音频测试开关（观看端控制被观看者是否发送测试音）
    void sendAudioToggle(bool enabled);
    void sendAudioGain(int percent);
    void setTalkEnabled(bool enabled);
    void sendViewerListenMute(bool mute);
    void sendLikeEvent();
    void setLocalInputDeviceFollowSystem();
    void setLocalInputDeviceById(const QString &id);
    bool isLocalInputFollowSystem() const { return m_followSystemInput; }
    QString localInputDeviceId() const { return m_localInputDeviceId; }
    void setViewerName(const QString &name);
    void sendViewerCursor(int x, int y);
    // 暂停推流但保留连接
    void sendStopStreaming();
    void sendRequestKeyFrame();
    // 通知采集端：该观众主动退出
    void sendViewerExit();
    
    // 停止音频处理（停止定时器，清空队列），用于在不完全断开连接的情况下静音
    void stopAudio();
public:
    // 断开连接
    void disconnectFromServer();

    // 检查连接状态
    bool isConnected() const;

private:
    bool m_audioStopped = false; // 防止音频被意外重启的标志
    
    // 获取接收统计信息
    struct ReceiverStats {
        quint64 totalFrames;
        quint64 totalBytes;
        double averageFrameSize;
        qint64 connectionTime;
        
        // 网络性能统计
        double networkLatency;            // 网络延迟 (ms)
        double jitter;                    // 网络抖动 (ms)
        quint64 reconnectionCount;        // 重连次数
        qint64 totalDowntime;             // 总断线时间 (ms)
    };
    ReceiverStats getStats() const { return m_stats; }

signals:
    void connected();
    void disconnected();
    void frameReceived(const QByteArray &frameData);
    void frameReceivedWithTimestamp(const QByteArray &frameData, qint64 captureTimestamp);
    void mousePositionReceived(const QPoint &position, qint64 timestamp, const QString &name = QString()); // 新增：鼠标位置信号
    void connectionError(const QString &error);
    void connectionStatusChanged(const QString &status);
    void statsUpdated(const ReceiverStats &stats);
    void streamingStarted(); // Signal when server confirms streaming is OK
    void approvalRequired(const QString &targetId);
    void watchRequestRejected(const QString &targetId);
    void watchRequestAccepted(const QString &targetId);
    
    // 新增：音频帧信号（PCM）
    void audioFrameReceived(const QByteArray &pcmData, int sampleRate, int channels, int bitsPerSample, qint64 timestamp);
    void audioGainChanged(int percent);
    void avatarUpdateReceived(const QString &userId, int iconId);

private slots:
    void onConnected();
    void onDisconnected();
    void onBinaryMessageReceived(const QByteArray &message);
    void onTextMessageReceived(const QString &message);
    void onError(QAbstractSocket::SocketError error);
    void onSslErrors(const QList<QSslError> &errors);
    void onStateChanged(QAbstractSocket::SocketState state);
    void attemptReconnect();
    void updateStats();
    
private:
    void setupWebSocket();
    void startReconnectTimer();
    void stopReconnectTimer();
    
    QWebSocket *m_webSocket;
    QString m_serverUrl;
    bool m_connected;
    bool m_reconnectEnabled;
    
    // 重连机制
    QTimer *m_reconnectTimer;
    int m_reconnectAttempts;
    int m_maxReconnectAttempts;
    int m_reconnectInterval; // 毫秒
    
    // 统计信息
    ReceiverStats m_stats;
    QTimer *m_statsTimer;
    qint64 m_connectionStartTime;
    QList<int> m_frameSizes;
    
    // 性能监控相关变量
    QList<qint64> m_latencyMeasurements;  // 延迟测量记录
    qint64 m_lastStatsUpdateTime;         // 上次统计更新时间
    qint64 m_totalDowntimeStart;          // 断线开始时间
    
    QRecursiveMutex m_mutex;

    // 最近一次观看请求信息，用于重连后自动重发
    QString m_lastViewerId;
    QString m_lastTargetId;
    bool m_autoResendWatchRequest = true;
    QString m_lastViewerName;

    // 音频：Opus 解码器状态
    OpusDecoder *m_opusDecoder = nullptr;
    int m_opusSampleRate = 16000;
    int m_opusChannels = 1;
    bool m_opusInitialized = false;
    // 简易抖动缓冲（20ms节拍定时解码）
    QTimer *m_audioTimer = nullptr;
    QQueue<QByteArray> m_opusQueue;
    QQueue<int> m_opusSeqQueue;
    int m_audioFrameSamples = 0; // 每帧采样数（20ms）
    qint64 m_audioLastTimestamp = 0;
    qint64 m_nextAudioTick = 0; // 下一次音频处理的理想时间点
    int m_audioPrebufferFrames = 10;
    int m_audioTargetBufferFrames = 15;
    int m_audioMinBufferFrames = 6;
    int m_audioMaxBufferFrames = 30;
    int m_audioUnderflowCount = 0;
    int m_producerSilenceCount = 0; // Prevent infinite PLC
    bool m_hasAudioStarted = false; // Flag for initial vs re-buffer logic
    int m_consecutiveUnderruns = 0; // Counter for soft stop logic
    bool m_producerBuffering = true; // Added for jitter buffering
    int m_lastOpusSeq = -1;
    void initOpusDecoderIfNeeded(int sampleRate, int channels);
    QMap<QString, OpusDecoder*> m_peerDecoders;
    QMap<QString, QQueue<QByteArray>> m_peerQueues;
    QMap<QString, int> m_peerSampleRates;
    QMap<QString, int> m_peerChannels;
    QMap<QString, int> m_peerFrameSamples;
    QMap<QString, int> m_peerSilenceCounts;
    QMap<QString, qint64> m_peerLastActiveTimes;
    QMap<QString, bool> m_peerBuffering; // New: Per-peer buffering state

    QAudioSource *m_localAudioSource = nullptr;
    QIODevice *m_localAudioInput = nullptr;
    QTimer *m_localAudioTimer = nullptr;
    OpusEncoder *m_localOpusEnc = nullptr;
    int m_localOpusSampleRate = 48000;
    int m_localOpusFrameSize = 48000 / 50;
    int m_localMicGainPercent = 100;
    bool m_followSystemInput = true;
    QString m_localInputDeviceId;
    QAudioFormat m_localInputFormat;
    QByteArray m_currentInputDeviceId;
    QTimer *m_inputPollTimer = nullptr;
    QByteArray m_rawInputBuffer;
    bool m_talkActive = false;
    void recreateLocalAudioSource();
    void handleLocalAudioState(QAudio::State st);
    int bytesPerSample(QAudioFormat::SampleFormat f) const;
    bool produceOpusFrame(QByteArray &out);

};

#endif // WEBSOCKETRECEIVER_H
