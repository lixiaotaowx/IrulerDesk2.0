#include <QApplication>
#include <QTimer>
#include <QDebug>
#include <QDateTime>
#include <QRandomGenerator>
#include "src/player/VideoRenderer.h"

class TileRenderingTest : public QObject
{
    Q_OBJECT

public:
    TileRenderingTest(QObject *parent = nullptr) : QObject(parent)
    {
        // 创建视频渲染器
        m_renderer = new VideoRenderer();
        m_renderer->show();
        
        // 设置瓦片配置
        QSize frameSize(1024, 768);
        QSize tileSize(256, 256);
        m_renderer->setTileConfiguration(frameSize, tileSize);
        
        qDebug() << "[TileRenderingTest] 瓦片渲染测试开始";
        qDebug() << "[TileRenderingTest] 帧大小:" << frameSize << "瓦片大小:" << tileSize;
        
        // 启动测试定时器
        m_testTimer = new QTimer(this);
        connect(m_testTimer, &QTimer::timeout, this, &TileRenderingTest::simulateTileData);
        m_testTimer->start(100); // 每100ms发送一个瓦片
        
        // 启动瓦片更新定时器
        m_updateTimer = new QTimer(this);
        connect(m_updateTimer, &QTimer::timeout, this, &TileRenderingTest::simulateTileUpdate);
        m_updateTimer->start(500); // 每500ms更新一个瓦片
        
        m_currentTileId = 0;
        m_totalTiles = (frameSize.width() / tileSize.width()) * (frameSize.height() / tileSize.height());
        
        qDebug() << "[TileRenderingTest] 总瓦片数:" << m_totalTiles;
    }
    
    ~TileRenderingTest()
    {
        delete m_renderer;
    }

private slots:
    void simulateTileData()
    {
        if (m_currentTileId >= m_totalTiles) {
            // 所有瓦片都已发送，重新开始
            m_currentTileId = 0;
            qDebug() << "[TileRenderingTest] 一轮瓦片发送完成，重新开始";
        }
        
        // 创建模拟瓦片数据
        QRect sourceRect(0, 0, 256, 256);
        QByteArray tileData = generateTileData(m_currentTileId, sourceRect.size());
        qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
        
        // 发送瓦片数据
        m_renderer->renderTile(m_currentTileId, tileData, sourceRect, timestamp);
        
        qDebug() << "[TileRenderingTest] 发送瓦片" << m_currentTileId << "数据大小:" << tileData.size();
        
        m_currentTileId++;
    }
    
    void simulateTileUpdate()
    {
        if (m_totalTiles == 0) return;
        
        // 随机选择一个瓦片进行更新
        int tileId = QRandomGenerator::global()->bounded(m_totalTiles);
        
        // 创建增量更新数据
        QRect updateRect(64, 64, 128, 128); // 更新瓦片中心区域
        QByteArray deltaData = generateUpdateData(updateRect.size());
        qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
        
        // 发送瓦片更新
        m_renderer->updateTile(tileId, deltaData, updateRect, timestamp);
        
        qDebug() << "[TileRenderingTest] 更新瓦片" << tileId << "区域:" << updateRect;
    }

private:
    QByteArray generateTileData(int tileId, const QSize &size)
    {
        // 生成彩色瓦片数据 (ARGB32格式)
        int width = size.width();
        int height = size.height();
        QByteArray data(width * height * 4, 0);
        
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
                
                data[offset + 0] = static_cast<quint8>(blue * (1.0f - fx));   // B
                data[offset + 1] = static_cast<quint8>(green * fy);           // G
                data[offset + 2] = static_cast<quint8>(red * fx);             // R
                data[offset + 3] = alpha;                                     // A
            }
        }
        
        return data;
    }
    
    QByteArray generateUpdateData(const QSize &size)
    {
        // 生成随机更新数据 (ARGB32格式)
        int width = size.width();
        int height = size.height();
        QByteArray data(width * height * 4, 0);
        
        // 生成随机颜色
        quint8 red = QRandomGenerator::global()->bounded(256);
        quint8 green = QRandomGenerator::global()->bounded(256);
        quint8 blue = QRandomGenerator::global()->bounded(256);
        quint8 alpha = 255;
        
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int offset = (y * width + x) * 4;
                data[offset + 0] = blue;   // B
                data[offset + 1] = green;  // G
                data[offset + 2] = red;    // R
                data[offset + 3] = alpha;  // A
            }
        }
        
        return data;
    }

private:
    VideoRenderer *m_renderer;
    QTimer *m_testTimer;
    QTimer *m_updateTimer;
    int m_currentTileId;
    int m_totalTiles;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    qDebug() << "[Main] 瓦片渲染测试程序启动";
    
    TileRenderingTest test;
    
    // 设置10秒后自动退出
    QTimer::singleShot(10000, [&]() {
        qDebug() << "[Main] 测试完成，程序退出";
        app.quit();
    });
    
    return app.exec();
}

#include "test_tile_rendering.moc"