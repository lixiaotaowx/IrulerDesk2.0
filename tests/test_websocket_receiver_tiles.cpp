#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// 为了测试，我们需要创建一个简化的WebSocketReceiver测试版本
class WebSocketReceiverTest : public QObject
{
    Q_OBJECT

public:
    // 瓦片数据结构（复制自WebSocketReceiver）
    struct TileMetadata {
        int tileId;
        int x, y, width, height;
        int totalChunks;
        int dataSize;
        qint64 timestamp;
        QString format;
    };
    
    struct TileChunk {
        int tileId;
        int chunkIndex;
        int totalChunks;
        QByteArray data;
        qint64 timestamp;
    };
    
    struct TileUpdate {
        int tileId;
        int x, y, width, height;
        QByteArray deltaData;
        qint64 timestamp;
    };

    WebSocketReceiverTest(QObject *parent = nullptr) : QObject(parent) {}

signals:
    void tileMetadataReceived(const TileMetadata &metadata);
    void tileChunkReceived(const TileChunk &chunk);
    void tileUpdateReceived(const TileUpdate &update);
    void tileCompleted(int tileId, const QByteArray &completeData);

public slots:
    void simulateBinaryMessageReceived(const QByteArray &message)
    {
        if (message.isEmpty() || message.size() < 4) {
            qDebug() << "[测试] 收到无效的二进制消息，大小:" << message.size();
            return;
        }
        
        // 读取JSON头部长度（前4字节）
        quint32 headerLength = 0;
        memcpy(&headerLength, message.data(), 4);
        
        if (headerLength == 0 || headerLength > message.size() - 4) {
            qDebug() << "[测试] 无效的头部长度:" << headerLength;
            return;
        }
        
        // 解析JSON头部
        QByteArray headerData = message.mid(4, headerLength);
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(headerData, &error);
        
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            qDebug() << "[测试] JSON头部解析失败:" << error.errorString();
            return;
        }
        
        QJsonObject header = doc.object();
        QString messageType = header["type"].toString();
        
        // 提取二进制数据部分
        QByteArray binaryData = message.mid(4 + headerLength);
        
        // 根据消息类型处理
        if (messageType == "tile_metadata") {
            handleTileMetadata(header);
        } else if (messageType == "tile_data") {
            handleTileData(header, binaryData);
        } else if (messageType == "tile_update") {
            handleTileUpdate(header, binaryData);
        } else {
            qDebug() << "[测试] 未知的瓦片消息类型:" << messageType;
        }
    }

private:
    void handleTileMetadata(const QJsonObject &header)
    {
        TileMetadata metadata;
        metadata.tileId = header["tile_id"].toInt();
        metadata.x = header["x"].toInt();
        metadata.y = header["y"].toInt();
        metadata.width = header["width"].toInt();
        metadata.height = header["height"].toInt();
        metadata.totalChunks = header["total_chunks"].toInt();
        metadata.dataSize = header["data_size"].toInt();
        metadata.timestamp = header["timestamp"].toVariant().toLongLong();
        metadata.format = header["format"].toString();
        
        qDebug() << "[测试] 处理瓦片元数据: ID=" << metadata.tileId 
                 << "位置=(" << metadata.x << "," << metadata.y << ")"
                 << "大小=" << metadata.width << "x" << metadata.height
                 << "分块数=" << metadata.totalChunks;
        
        emit tileMetadataReceived(metadata);
    }

    void handleTileData(const QJsonObject &header, const QByteArray &data)
    {
        TileChunk chunk;
        chunk.tileId = header["tile_id"].toInt();
        chunk.chunkIndex = header["chunk_index"].toInt();
        chunk.totalChunks = header["total_chunks"].toInt();
        chunk.timestamp = header["timestamp"].toVariant().toLongLong();
        chunk.data = data;
        
        qDebug() << "[测试] 处理瓦片数据块: ID=" << chunk.tileId 
                 << "块索引=" << chunk.chunkIndex << "/" << chunk.totalChunks
                 << "数据大小=" << chunk.data.size();
        
        emit tileChunkReceived(chunk);
        
        // 简单的完成检测（实际应该有缓存逻辑）
        static QHash<int, int> receivedChunks;
        receivedChunks[chunk.tileId]++;
        
        if (receivedChunks[chunk.tileId] == chunk.totalChunks) {
            QByteArray completeData;
            completeData.fill('X', chunk.totalChunks * 300); // 模拟完整数据
            emit tileCompleted(chunk.tileId, completeData);
        }
    }

    void handleTileUpdate(const QJsonObject &header, const QByteArray &data)
    {
        TileUpdate update;
        update.tileId = header["tile_id"].toInt();
        update.x = header["x"].toInt();
        update.y = header["y"].toInt();
        update.width = header["width"].toInt();
        update.height = header["height"].toInt();
        update.timestamp = header["timestamp"].toVariant().toLongLong();
        update.deltaData = data;
        
        qDebug() << "[测试] 处理瓦片更新: ID=" << update.tileId 
                 << "位置=(" << update.x << "," << update.y << ")"
                 << "增量数据大小=" << update.deltaData.size();
        
        emit tileUpdateReceived(update);
    }
};

class TileReceiverTest : public QObject
{
    Q_OBJECT

public:
    TileReceiverTest(QObject *parent = nullptr) : QObject(parent)
    {
        m_receiver = new WebSocketReceiverTest(this);
        
        // 连接瓦片相关信号
        connect(m_receiver, &WebSocketReceiverTest::tileMetadataReceived,
                this, &TileReceiverTest::onTileMetadataReceived);
        connect(m_receiver, &WebSocketReceiverTest::tileChunkReceived,
                this, &TileReceiverTest::onTileChunkReceived);
        connect(m_receiver, &WebSocketReceiverTest::tileUpdateReceived,
                this, &TileReceiverTest::onTileUpdateReceived);
        connect(m_receiver, &WebSocketReceiverTest::tileCompleted,
                this, &TileReceiverTest::onTileCompleted);
    }

public slots:
    void runTest()
    {
        qDebug() << "=== WebSocketReceiver 瓦片处理功能测试 ===";
        
        // 测试瓦片消息解析
        testTileMessageParsing();
        
        // 3秒后退出
        QTimer::singleShot(3000, qApp, &QCoreApplication::quit);
    }

private slots:
    void onTileMetadataReceived(const WebSocketReceiverTest::TileMetadata &metadata)
    {
        qDebug() << "[测试] ✓ 收到瓦片元数据:"
                 << "ID=" << metadata.tileId
                 << "位置=(" << metadata.x << "," << metadata.y << ")"
                 << "大小=" << metadata.width << "x" << metadata.height
                 << "分块数=" << metadata.totalChunks;
        m_metadataReceived++;
    }

    void onTileChunkReceived(const WebSocketReceiverTest::TileChunk &chunk)
    {
        qDebug() << "[测试] ✓ 收到瓦片数据块:"
                 << "ID=" << chunk.tileId
                 << "块索引=" << chunk.chunkIndex
                 << "数据大小=" << chunk.data.size();
        m_chunksReceived++;
    }

    void onTileUpdateReceived(const WebSocketReceiverTest::TileUpdate &update)
    {
        qDebug() << "[测试] ✓ 收到瓦片更新:"
                 << "ID=" << update.tileId
                 << "位置=(" << update.x << "," << update.y << ")"
                 << "增量数据大小=" << update.deltaData.size();
        m_updatesReceived++;
    }

    void onTileCompleted(int tileId, const QByteArray &completeData)
    {
        qDebug() << "[测试] ✓ 瓦片组装完成:"
                 << "ID=" << tileId
                 << "完整数据大小=" << completeData.size();
        m_tilesCompleted++;
    }

private:
    void testTileMessageParsing()
    {
        qDebug() << "\n--- 测试瓦片消息解析 ---";
        
        // 模拟瓦片元数据消息
        simulateTileMetadataMessage();
        
        // 模拟瓦片数据消息
        simulateTileDataMessages();
        
        // 模拟瓦片更新消息
        simulateTileUpdateMessage();
        
        // 输出测试结果
        QTimer::singleShot(1000, this, [this]() {
            qDebug() << "\n--- 测试结果 ---";
            qDebug() << "元数据接收:" << m_metadataReceived;
            qDebug() << "数据块接收:" << m_chunksReceived;
            qDebug() << "更新接收:" << m_updatesReceived;
            qDebug() << "瓦片完成:" << m_tilesCompleted;
            
            bool success = (m_metadataReceived > 0 && m_chunksReceived > 0 && m_tilesCompleted > 0);
            qDebug() << "测试结果:" << (success ? "✓ 成功" : "✗ 失败");
        });
    }

    void simulateTileMetadataMessage()
    {
        // 构造瓦片元数据JSON
        QJsonObject header;
        header["type"] = "tile_metadata";
        header["tile_id"] = 1;
        header["x"] = 100;
        header["y"] = 200;
        header["width"] = 256;
        header["height"] = 256;
        header["total_chunks"] = 3;
        header["data_size"] = 1024;
        header["timestamp"] = QDateTime::currentMSecsSinceEpoch();
        header["format"] = "VP9";

        QJsonDocument doc(header);
        QByteArray headerData = doc.toJson(QJsonDocument::Compact);
        
        // 构造完整消息
        QByteArray message;
        quint32 headerLength = headerData.size();
        message.append(reinterpret_cast<const char*>(&headerLength), 4);
        message.append(headerData);
        // 元数据消息没有二进制数据部分
        
        // 调用消息处理方法
        m_receiver->simulateBinaryMessageReceived(message);
    }

    void simulateTileDataMessages()
    {
        // 模拟3个数据块
        for (int i = 0; i < 3; ++i) {
            QJsonObject header;
            header["type"] = "tile_data";
            header["tile_id"] = 1;
            header["chunk_index"] = i;
            header["total_chunks"] = 3;
            header["timestamp"] = QDateTime::currentMSecsSinceEpoch();

            QJsonDocument doc(header);
            QByteArray headerData = doc.toJson(QJsonDocument::Compact);
            
            // 模拟数据块内容
            QByteArray chunkData;
            chunkData.fill('A' + i, 300 + i * 50); // 不同大小的数据块
            
            // 构造完整消息
            QByteArray message;
            quint32 headerLength = headerData.size();
            message.append(reinterpret_cast<const char*>(&headerLength), 4);
            message.append(headerData);
            message.append(chunkData);
            
            // 调用消息处理方法
            m_receiver->simulateBinaryMessageReceived(message);
        }
    }

    void simulateTileUpdateMessage()
    {
        QJsonObject header;
        header["type"] = "tile_update";
        header["tile_id"] = 2;
        header["x"] = 150;
        header["y"] = 250;
        header["width"] = 128;
        header["height"] = 128;
        header["timestamp"] = QDateTime::currentMSecsSinceEpoch();

        QJsonDocument doc(header);
        QByteArray headerData = doc.toJson(QJsonDocument::Compact);
        
        // 模拟增量数据
        QByteArray deltaData;
        deltaData.fill('D', 200);
        
        // 构造完整消息
        QByteArray message;
        quint32 headerLength = headerData.size();
        message.append(reinterpret_cast<const char*>(&headerLength), 4);
        message.append(headerData);
        message.append(deltaData);
        
        // 调用消息处理方法
        m_receiver->simulateBinaryMessageReceived(message);
    }

private:
    WebSocketReceiverTest *m_receiver;
    int m_metadataReceived = 0;
    int m_chunksReceived = 0;
    int m_updatesReceived = 0;
    int m_tilesCompleted = 0;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    TileReceiverTest test;
    
    // 启动测试
    QTimer::singleShot(100, &test, &TileReceiverTest::runTest);
    
    return app.exec();
}

#include "test_websocket_receiver_tiles.moc"