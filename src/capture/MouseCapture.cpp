#include "MouseCapture.h"
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
    , m_needScaling(false)
{
    // 连接定时器信号
    connect(m_captureTimer, &QTimer::timeout, this, &MouseCapture::checkMousePosition);
    
    // 设置定时器间隔
    m_captureTimer->setInterval(m_updateInterval);
    
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
    
}

void MouseCapture::stopCapture()
{
    if (!m_isCapturing) {
        return;
    }
    
    m_isCapturing = false;
    m_captureTimer->stop();
    
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

void MouseCapture::setScreenRect(const QRect &sourceRect, const QSize &targetSize)
{
    m_sourceRect = sourceRect;
    m_targetSize = targetSize;
    // 需要缩放的条件：源区域尺寸与目标尺寸不同，且都不为空
    // 或者源区域有偏移（非0,0）
    m_needScaling = ((sourceRect.size() != targetSize || !sourceRect.topLeft().isNull()) && !sourceRect.isEmpty() && !targetSize.isEmpty());
}

QPoint MouseCapture::getSystemMousePosition() const
{
#ifdef _WIN32
    // 使用Windows API获取鼠标位置
    POINT point;
    if (GetCursorPos(&point)) {
        QPoint p(point.x, point.y);
        
        // 减去屏幕偏移（如果是多屏或非主屏）
        if (!m_sourceRect.topLeft().isNull()) {
            p -= m_sourceRect.topLeft();
        }

        // 如果需要缩放，将物理坐标映射到编码分辨率
        if (m_needScaling && m_sourceRect.width() > 0 && m_sourceRect.height() > 0) {
            // 如果目标尺寸与源尺寸不同，进行缩放
            // 增加容差：如果尺寸差异小于2像素，不进行缩放，避免浮点误差导致的微小抖动
            if (qAbs(m_sourceRect.width() - m_targetSize.width()) > 1 || 
                qAbs(m_sourceRect.height() - m_targetSize.height()) > 1) {
                int sx = qRound(double(p.x()) * double(m_targetSize.width()) / double(m_sourceRect.width()));
                int sy = qRound(double(p.y()) * double(m_targetSize.height()) / double(m_sourceRect.height()));
                return QPoint(sx, sy);
            }
        }
        return p;
    } else {
        return QPoint(-1, -1);
    }
#else
    // 使用Qt跨平台API获取鼠标位置
    QPoint p = QCursor::pos();
    
    // 减去屏幕偏移
    if (!m_sourceRect.topLeft().isNull()) {
        p -= m_sourceRect.topLeft();
    }

    // 如果需要缩放
    if (m_needScaling && m_sourceRect.width() > 0 && m_sourceRect.height() > 0) {
        if (qAbs(m_sourceRect.width() - m_targetSize.width()) > 1 || 
            qAbs(m_sourceRect.height() - m_targetSize.height()) > 1) {
            int sx = qRound(double(p.x()) * double(m_targetSize.width()) / double(m_sourceRect.width()));
            int sy = qRound(double(p.y()) * double(m_targetSize.height()) / double(m_sourceRect.height()));
            return QPoint(sx, sy);
        }
    }
    return p;
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