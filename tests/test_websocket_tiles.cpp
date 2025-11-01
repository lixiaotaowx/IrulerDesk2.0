#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <QVector>
#include <QByteArray>
#include <QDataStream>
#include <QBuffer>
#include <QImage>
#include "WebSocketSender.h"
#include "TileManager.h"

class TileTransferTest : public QObject
{
    Q_OBJECT

public:
    TileTransferTest(QObject *parent = nullptr) : QObject(parent)
    {
        m_webSocketSender = new WebSocketSender(this);
        
        // 连接信号
        connect(m_webSocketSender, &WebSocketSender::connected, this, &TileTransferTest::onConnected);
        connect(m_webSocketSender, &WebSocketSender::disconnected, this, &TileTransferTest::onDisconnected);
        connect(m_webSocketSender, &WebSocketSender::tileDataSent, this, &TileTransferTest::onTileDataSent);
        connect(m_webSocketSender, &WebSocketSender::tileUpdateSent, this, &TileTransferTest::onTileUpdateSent);
        connect(m_webSocketSender, &WebSocketSender::tileMetadataSent, this, &TileTransferTest::onTileMetadataSent);
        connect(m_webSocketSender, &WebSocketSender::error, this, &TileTransferTest::onError);
    }

public slots:
    void startTest()
    {
        qDebug() << "[测试] 开始WebSocket瓦片传输测试";
        
        // 连接到WebSocket服务器
        m_webSocketSender->connectToServer("ws://localhost:8080");
        
        // 设置测试超时
        QTimer::singleShot(10000, this, &TileTransferTest::onTestTimeout);
    }

private slots:
    void onConnected()
    {
        qDebug() << "[测试] WebSocket连接成功";
        
        // 开始推流
        m_webSocketSender->startStreaming();
        
        // 延迟执行测试
        QTimer::singleShot(1000, this, &TileTransferTest::runTileTests);
    }
    
    void onDisconnected()
    {
        qDebug() << "[测试] WebSocket连接断开";
    }
    
    void onError(const QString &errorMessage)
    {
        qDebug() << "[测试] WebSocket错误:" << errorMessage;
    }
    
    void onTileDataSent(int tileCount, int dataSize)
    {
        qDebug() << "[测试] 瓦片数据发送成功 - 瓦片数量:" << tileCount << "数据大小:" << dataSize;
        m_testResults["tile_data"] = true;
        checkTestCompletion();
    }
    
    void onTileUpdateSent(int updatedTileCount)
    {
        qDebug() << "[测试] 瓦片更新发送成功 - 更新数量:" << updatedTileCount;
        m_testResults["tile_update"] = true;
        checkTestCompletion();
    }
    
    void onTileMetadataSent(int totalTileCount)
    {
        qDebug() << "[测试] 瓦片元数据发送成功 - 总数量:" << totalTileCount;
        m_testResults["tile_metadata"] = true;
        checkTestCompletion();
    }
    
    void runTileTests()
    {
        qDebug() << "[测试] 开始执行瓦片传输测试";
        
        // 测试1: 发送瓦片元数据
        testTileMetadata();
        
        // 测试2: 发送瓦片数据
        QTimer::singleShot(500, this, &TileTransferTest::testTileData);
        
        // 测试3: 发送瓦片更新
        QTimer::singleShot(1000, this, &TileTransferTest::testTileUpdate);
    }
    
    void testTileMetadata()
    {
        qDebug() << "[测试] 测试瓦片元数据传输";
        
        // 创建测试瓦片信息
        QVector<TileInfo> tiles;
        for (int i = 0; i < 4; ++i) {
            TileInfo tile;
            tile.x = (i % 2) * 64;
            tile.y = (i / 2) * 64;
            tile.width = 64;
            tile.height = 64;
            tile.hash = 0x12345678 + i;
            tiles.append(tile);
        }
        
        m_webSocketSender->sendTileMetadata(tiles);
    }
    
    void testTileData()
    {
        qDebug() << "[测试] 测试瓦片数据传输";
        
        // 创建测试瓦片索引
        QVector<int> tileIndices = {0, 1, 2, 3};
        
        // 创建序列化的瓦片数据
        QByteArray serializedData;
        QDataStream stream(&serializedData, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_5_15);
        
        // 写入瓦片数量
        stream << static_cast<quint32>(tileIndices.size());
        
        // 为每个瓦片创建测试图像数据
        for (int index : tileIndices) {
            // 创建64x64的测试图像
            QImage testImage(64, 64, QImage::Format_RGB32);
            testImage.fill(QColor(255, 0, 0)); // 红色填充
            
            // 序列化图像
            QByteArray imageData;
            QBuffer buffer(&imageData);
            buffer.open(QIODevice::WriteOnly);
            testImage.save(&buffer, "PNG");
            
            // 写入图像数据大小和数据
            stream << static_cast<quint32>(imageData.size());
            stream.writeRawData(imageData.constData(), imageData.size());
        }
        
        m_webSocketSender->sendTileData(tileIndices, serializedData);
    }
    
    void testTileUpdate()
    {
        qDebug() << "[测试] 测试瓦片更新传输";
        
        // 创建更新的瓦片信息
        QVector<TileInfo> updatedTiles;
        QVector<QByteArray> tileImages;
        
        for (int i = 0; i < 2; ++i) {
            TileInfo tile;
            tile.x = i * 64;
            tile.y = 0;
            tile.width = 64;
            tile.height = 64;
            tile.hash = 0x87654321 + i;
            updatedTiles.append(tile);
            
            // 创建测试图像
            QImage testImage(64, 64, QImage::Format_RGB32);
            testImage.fill(QColor(0, 255, 0)); // 绿色填充
            
            QByteArray imageData;
            QBuffer buffer(&imageData);
            buffer.open(QIODevice::WriteOnly);
            testImage.save(&buffer, "PNG");
            
            tileImages.append(imageData);
        }
        
        m_webSocketSender->sendTileUpdate(updatedTiles, tileImages);
    }
    
    void checkTestCompletion()
    {
        if (m_testResults.size() >= 3) {
            qDebug() << "[测试] 所有瓦片传输测试完成";
            printTestResults();
            QTimer::singleShot(1000, qApp, &QCoreApplication::quit);
        }
    }
    
    void onTestTimeout()
    {
        qDebug() << "[测试] 测试超时";
        printTestResults();
        qApp->quit();
    }
    
    void printTestResults()
    {
        qDebug() << "=== 测试结果 ===";
        qDebug() << "瓦片元数据传输:" << (m_testResults.contains("tile_metadata") ? "成功" : "失败");
        qDebug() << "瓦片数据传输:" << (m_testResults.contains("tile_data") ? "成功" : "失败");
        qDebug() << "瓦片更新传输:" << (m_testResults.contains("tile_update") ? "成功" : "失败");
        
        // 打印统计信息
        qDebug() << "总发送字节数:" << m_webSocketSender->getTotalBytesSent();
        qDebug() << "总发送瓦片数:" << m_webSocketSender->getTotalTilesSent();
        qDebug() << "瓦片数据字节数:" << m_webSocketSender->getTotalTileDataSent();
    }

private:
    WebSocketSender *m_webSocketSender;
    QMap<QString, bool> m_testResults;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    qDebug() << "WebSocket瓦片传输功能测试程序";
    qDebug() << "注意: 需要WebSocket服务器运行在 ws://localhost:8080";
    
    TileTransferTest test;
    
    // 延迟启动测试
    QTimer::singleShot(100, &test, &TileTransferTest::startTest);
    
    return app.exec();
}

#include "test_websocket_tiles.moc"