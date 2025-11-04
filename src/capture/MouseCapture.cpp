#include "MouseCapture.h"
#include <QDebug>
#include <QCursor>
#include <QGuiApplication>
#include <QJsonObject>
#include <QJsonDocument>
#include <chrono>

MouseCapture::MouseCapture(QObject *parent)
    : QObject(parent)
    , m_captureTimer(new QTimer(this))
    , m_lastPosition(-1, -1)
    , m_isCapturing(false)
    , m_updateInterval(10) // 100Hz更新频率
{
    // 连接定时器信号
    connect(m_captureTimer, &QTimer::timeout, this, &MouseCapture::checkMousePosition);
    
    // 设置定时器间隔
    m_captureTimer->setInterval(m_updateInterval);
    
    qDebug() << "[MouseCapture] 鼠标捕获模块初始化完成，更新频率:" << (1000.0 / m_updateInterval) << "Hz";
}

MouseCapture::~MouseCapture()
{
    stopCapture();
}

void MouseCapture::startCapture()
{
    if (m_isCapturing) {
        return;
    }
    
    m_isCapturing = true;
    m_lastPosition = QPoint(-1, -1); // 重置上次位置，确保第一次发送
    m_captureTimer->start();
    
    qDebug() << "[MouseCapture] 开始鼠标位置捕获";
}

void MouseCapture::stopCapture()
{
    if (!m_isCapturing) {
        return;
    }
    
    m_isCapturing = false;
    m_captureTimer->stop();
    
    qDebug() << "[MouseCapture] 停止鼠标位置捕获";
}

QPoint MouseCapture::getCurrentPosition() const
{
    return getSystemMousePosition();
}

void MouseCapture::checkMousePosition()
{
    QPoint currentPosition = getSystemMousePosition();
    
    // 只有位置发生变化时才发送消息
    if (currentPosition != m_lastPosition) {
        m_lastPosition = currentPosition;
        
        // 发送位置变化信号
        emit mousePositionChanged(currentPosition);
        
        // 生成JSON消息并发送
        QString jsonMessage = createMousePositionMessage(currentPosition);
        emit mousePositionMessage(jsonMessage);
        
        // 每100次位置更新输出一次调试信息（避免日志过多）
        static int positionUpdateCount = 0;
        positionUpdateCount++;
        if (positionUpdateCount % 100 == 0) {
            // qDebug() << "[MouseCapture] 鼠标位置更新:" << currentPosition << "，已发送" << positionUpdateCount << "次位置更新"; // 已禁用以提升性能
        }
    }
}

QPoint MouseCapture::getSystemMousePosition() const
{
#ifdef _WIN32
    // 使用Windows API获取鼠标位置
    POINT point;
    if (GetCursorPos(&point)) {
        return QPoint(point.x, point.y);
    } else {
        qWarning() << "[MouseCapture] GetCursorPos失败";
        return QPoint(-1, -1);
    }
#else
    // 使用Qt跨平台API获取鼠标位置
    return QCursor::pos();
#endif
}

QString MouseCapture::createMousePositionMessage(const QPoint &position)
{
    // 创建JSON消息
    QJsonObject messageObj;
    messageObj["type"] = "mouse_position";
    messageObj["x"] = position.x();
    messageObj["y"] = position.y();
    
    // 添加时间戳（微秒）
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    messageObj["timestamp"] = static_cast<qint64>(timestamp);
    
    // 转换为JSON字符串
    QJsonDocument doc(messageObj);
    return doc.toJson(QJsonDocument::Compact);
}