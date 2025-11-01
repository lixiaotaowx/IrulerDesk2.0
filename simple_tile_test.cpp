#include <QApplication>
#include <QTimer>
#include <QDebug>
#include <QDateTime>
#include <QRandomGenerator>
#include "src/player/VideoRenderer.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    qDebug() << "[Main] 瓦片渲染测试程序启动";
    
    // 创建视频渲染器
    VideoRenderer renderer;
    renderer.show();
    
    // 设置瓦片配置
    QSize frameSize(1024, 768);
    QSize tileSize(256, 256);
    renderer.setTileConfiguration(frameSize, tileSize);
    
    qDebug() << "[Test] 瓦片配置设置完成 - 帧大小:" << frameSize << "瓦片大小:" << tileSize;
    
    int totalTiles = (frameSize.width() / tileSize.width()) * (frameSize.height() / tileSize.height());
    qDebug() << "[Test] 总瓦片数:" << totalTiles;
    
    // 生成并发送所有瓦片
    for (int tileId = 0; tileId < totalTiles; ++tileId) {
        // 创建模拟瓦片数据 (ARGB32格式)
        int width = 256;
        int height = 256;
        QByteArray tileData(width * height * 4, 0);
        
        // 根据瓦片ID生成不同颜色
        quint8 red = (tileId * 50) % 256;
        quint8 green = (tileId * 80) % 256;
        quint8 blue = (tileId * 120) % 256;
        quint8 alpha = 255;
        
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int offset = (y * width + x) * 4;
                
                // 创建渐变效果
                float fx = static_cast<float>(x) / width;
                float fy = static_cast<float>(y) / height;
                
                tileData[offset + 0] = static_cast<quint8>(blue * (1.0f - fx));   // B
                tileData[offset + 1] = static_cast<quint8>(green * fy);           // G
                tileData[offset + 2] = static_cast<quint8>(red * fx);             // R
                tileData[offset + 3] = alpha;                                     // A
            }
        }
        
        QRect sourceRect(0, 0, width, height);
        qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
        
        // 发送瓦片数据
        renderer.renderTile(tileId, tileData, sourceRect, timestamp);
        
        qDebug() << "[Test] 发送瓦片" << tileId << "数据大小:" << tileData.size();
    }
    
    qDebug() << "[Test] 所有瓦片已发送，等待渲染...";
    
    // 创建定时器进行瓦片更新测试
    QTimer updateTimer;
    QObject::connect(&updateTimer, &QTimer::timeout, [&]() {
        // 随机选择一个瓦片进行更新
        int tileId = QRandomGenerator::global()->bounded(totalTiles);
        
        // 创建增量更新数据
        QRect updateRect(64, 64, 128, 128); // 更新瓦片中心区域
        QByteArray deltaData(updateRect.width() * updateRect.height() * 4, 0);
        
        // 生成随机颜色
        quint8 red = QRandomGenerator::global()->bounded(256);
        quint8 green = QRandomGenerator::global()->bounded(256);
        quint8 blue = QRandomGenerator::global()->bounded(256);
        quint8 alpha = 255;
        
        for (int y = 0; y < updateRect.height(); ++y) {
            for (int x = 0; x < updateRect.width(); ++x) {
                int offset = (y * updateRect.width() + x) * 4;
                deltaData[offset + 0] = blue;   // B
                deltaData[offset + 1] = green;  // G
                deltaData[offset + 2] = red;    // R
                deltaData[offset + 3] = alpha;  // A
            }
        }
        
        qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
        
        // 发送瓦片更新
        renderer.updateTile(tileId, deltaData, updateRect, timestamp);
        
        qDebug() << "[Test] 更新瓦片" << tileId << "区域:" << updateRect;
    });
    
    updateTimer.start(1000); // 每秒更新一个瓦片
    
    // 设置10秒后自动退出
    QTimer::singleShot(10000, [&]() {
        qDebug() << "[Test] 测试完成，程序退出";
        app.quit();
    });
    
    qDebug() << "[Test] 开始事件循环，测试将运行10秒...";
    
    return app.exec();
}