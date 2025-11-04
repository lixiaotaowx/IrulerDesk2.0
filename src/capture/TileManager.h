#ifndef TILEMANAGER_H
#define TILEMANAGER_H

#include <QObject>
#include <QRect>
#include <QSize>
#include <QVector>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QDebug>
#include <cstdint>

// 瓦片数据序列化格式常量
namespace TileSerializationFormat {
    const quint32 MAGIC_NUMBER = 0x54494C45;  // "TILE" in hex
    const quint16 VERSION = 1;                 // 格式版本号
    const quint16 HEADER_SIZE = 16;            // 头部大小（字节）
    
    // 数据类型标识
    enum DataType : quint8 {
        TILE_INFO_ONLY = 0x01,      // 仅瓦片信息（无图像数据）
        TILE_WITH_DATA = 0x02,      // 瓦片信息+图像数据
        TILE_BATCH = 0x03           // 批量瓦片数据
    };
}

// 瓦片信息结构体
struct TileInfo {
    int x;              // 瓦片在屏幕中的X坐标
    int y;              // 瓦片在屏幕中的Y坐标
    int width;          // 瓦片宽度
    int height;         // 瓦片高度
    uint32_t hash;      // 瓦片内容的哈希值
    bool hasChanged;    // 是否发生变化
    QByteArray data;    // 瓦片的图像数据（可选，用于传输）
    
    // 构造函数
    TileInfo() : x(0), y(0), width(0), height(0), hash(0), hasChanged(false) {}
    
    TileInfo(int x, int y, int w, int h) 
        : x(x), y(y), width(w), height(h), hash(0), hasChanged(false) {}
    
    // 获取瓦片的矩形区域
    QRect rect() const {
        return QRect(x, y, width, height);
    }
    
    // 获取瓦片的唯一标识符
    QString id() const {
        return QString("%1_%2").arg(x).arg(y);
    }
};

// 瓦片管理器类
class TileManager : public QObject
{
    Q_OBJECT

public:
    explicit TileManager(QObject *parent = nullptr);
    ~TileManager();

    // 初始化瓦片管理器
    bool initialize(const QSize& screenSize, int tileWidth = 64, int tileHeight = 64);
    
    // 清理资源
    void cleanup();
    
    // 获取瓦片配置信息
    QSize getScreenSize() const { return m_screenSize; }
    QSize getTileSize() const { return QSize(m_tileWidth, m_tileHeight); }
    int getTileCount() const { return m_tiles.size(); }
    int getTileColumns() const { return m_tilesPerRow; }
    int getTileRows() const { return m_tilesPerColumn; }
    bool isInitialized() const { return m_initialized; } // 检查是否已初始化
    
    // 获取所有瓦片信息
    const QVector<TileInfo>& getTiles() const { return m_tiles; }
    QVector<TileInfo> getAllTiles() const { return m_tiles; }
    
    // 获取变化的瓦片
    QVector<TileInfo> getChangedTiles() const;
    
    // 获取瓦片尺寸信息
    int getTileWidth() const { return m_tileWidth; }
    int getTileHeight() const { return m_tileHeight; }
    int getTileCountY() const { return m_tilesPerColumn; }
    
    // 根据坐标获取瓦片索引
    int getTileIndex(int x, int y) const;
    
    // 根据索引获取瓦片信息
    const TileInfo* getTile(int index) const;
    TileInfo* getTile(int index);
    
    // 检测瓦片变化（优化版本）
    QVector<int> detectChangedTiles(const QImage& currentFrame);
    
    // 比较并更新瓦片哈希值
    QVector<int> compareAndUpdateTiles(const QImage& currentFrame);
    
    // 重置所有瓦片的变化状态
    void resetChangeFlags();
    
    // 获取变化的瓦片数量
    int getChangedTileCount() const;
    
    // 获取变化瓦片的详细信息（用于调试）
    QVector<QPair<int, QRect>> getChangedTileDetails() const;
    
    // 性能优化相关
    void setPerformanceMode(bool fastMode);
    bool isPerformanceModeEnabled() const { return m_fastMode; }
    
    // 瓦片大小优化相关
    static QSize calculateOptimalTileSize(const QSize& screenSize);
    static int getRecommendedTileCount(const QSize& screenSize);
    void setAdaptiveTileSize(bool enable) { m_adaptiveTileSize = enable; }
    bool isAdaptiveTileSizeEnabled() const { return m_adaptiveTileSize; }
    
    // 性能统计结构
    struct PerformanceStats {
        quint64 totalDetections;              // 总检测次数
        quint64 totalChangedTiles;            // 总变化瓦片数
        qint64 totalDetectionTime;            // 总检测时间 (ms)
        qint64 totalCrcCalculationTime;       // 总CRC计算时间 (ms)
        qint64 totalHashCalculationTime;      // 总哈希计算时间 (ms)
        qint64 averageDetectionTime;          // 平均检测时间 (ms)
        qint64 averageCrcTime;                // 平均CRC计算时间 (ms)
        qint64 averageHashTime;               // 平均哈希计算时间 (ms)
        qint64 maxDetectionTime;              // 最大检测时间 (ms)
        qint64 minDetectionTime;              // 最小检测时间 (ms)
        double changeRate;                    // 瓦片变化率
        double detectionFPS;                  // 检测帧率
        qint64 lastDetectionTime;             // 上次检测时间戳
    };
    
    // 获取性能统计
    PerformanceStats getPerformanceStats() const { return m_perfStats; }
    void resetPerformanceStats();
    
    // 调试信息
    void printTileInfo() const;
    
    // 瓦片数据序列化功能
    QByteArray serializeTileData(const QVector<int>& changedTileIndices, const QImage& currentFrame) const;
    QByteArray serializeAllTiles(const QImage& currentFrame) const;
    QByteArray serializeTileInfo(const TileInfo& tile) const;
    
    // 瓦片数据反序列化功能
    bool deserializeTileData(const QByteArray& data, QVector<TileInfo>& tiles, QVector<QImage>& tileImages) const;
    TileInfo deserializeTileInfo(const QByteArray& data) const;
    
    // 瓦片数据格式验证
    bool validateSerializedData(const QByteArray& data) const;

private:
    // 计算数据的CRC32哈希值
    uint32_t calculateCRC32(const uchar* data, int length) const;
    
    // 优化的瓦片哈希计算（直接访问图像数据）
    uint32_t calculateTileHashOptimized(const uchar* imageData, int bytesPerLine,
                                       int tileX, int tileY, 
                                       int tileWidth, int tileHeight) const;
    
    // 创建瓦片网格
    void createTileGrid();
    
    // 性能统计更新方法
    void updatePerformanceStats(qint64 detectionTime, qint64 hashTime, qint64 crcTime, int changedTileCount);

private:
    QSize m_screenSize;         // 屏幕尺寸
    int m_tileWidth;            // 瓦片宽度
    int m_tileHeight;           // 瓦片高度
    int m_tilesPerRow;          // 每行瓦片数量
    int m_tilesPerColumn;       // 每列瓦片数量
    
    QVector<TileInfo> m_tiles;  // 瓦片信息数组
    QMutex m_mutex;             // 线程安全锁
    
    bool m_initialized;         // 是否已初始化
    bool m_fastMode;            // 性能优化模式
    bool m_adaptiveTileSize;    // 自适应瓦片大小模式
    
    // 性能监控相关
    PerformanceStats m_perfStats;           // 性能统计数据
    QVector<qint64> m_detectionTimes;       // 最近的检测时间记录 (保留最近100次)
    QVector<qint64> m_crcTimes;             // 最近的CRC计算时间记录
    QVector<qint64> m_hashTimes;            // 最近的哈希计算时间记录
    qint64 m_lastStatsUpdateTime;           // 上次统计更新时间

signals:
    // 瓦片变化信号
    void tilesChanged(const QVector<int>& changedTileIndices);
    
    // 错误信号
    void error(const QString& message);
};

#endif // TILEMANAGER_H