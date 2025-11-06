#include "TileManager.h"
#include <QMutexLocker>
#include <QtMath>
#include <QDataStream>
#include <QBuffer>
#include <QtGlobal>
#include <QDateTime>
#include <cstring>
#include <climits>

// CRC32查找表
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

TileManager::TileManager(QObject *parent)
    : QObject(parent)
    , m_screenSize(0, 0)
    , m_tileWidth(64)
    , m_tileHeight(64)
    , m_tilesPerRow(0)
    , m_tilesPerColumn(0)
    , m_initialized(false)
    , m_fastMode(true)  // 默认启用性能优化模式
    , m_adaptiveTileSize(false)
    , m_lastStatsUpdateTime(QDateTime::currentMSecsSinceEpoch())
{
    // 初始化性能统计
    memset(&m_perfStats, 0, sizeof(m_perfStats));
    m_perfStats.minDetectionTime = LLONG_MAX;
    m_perfStats.lastDetectionTime = QDateTime::currentMSecsSinceEpoch();
}

TileManager::~TileManager()
{
    cleanup();
}

bool TileManager::initialize(const QSize& screenSize, int tileWidth, int tileHeight)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_initialized) {
        return true;
    }
    
    if (screenSize.width() <= 0 || screenSize.height() <= 0) {
        emit error("无效的屏幕尺寸");
        return false;
    }
    
    if (tileWidth <= 0 || tileHeight <= 0) {
        emit error("无效的瓦片尺寸");
        return false;
    }
    
    m_screenSize = screenSize;
    m_tileWidth = tileWidth;
    m_tileHeight = tileHeight;
    
    // 计算瓦片网格尺寸
    m_tilesPerRow = qCeil(static_cast<double>(screenSize.width()) / tileWidth);
    m_tilesPerColumn = qCeil(static_cast<double>(screenSize.height()) / tileHeight);
    
    // 初始化参数已设置
    
    // 创建瓦片网格
    createTileGrid();
    
    m_initialized = true;
    return true;
}

void TileManager::cleanup()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_initialized) {
        return;
    }
    
    m_tiles.clear();
    m_screenSize = QSize(0, 0);
    m_tilesPerRow = 0;
    m_tilesPerColumn = 0;
    m_initialized = false;
    
}

int TileManager::getTileIndex(int x, int y) const
{
    if (!m_initialized || x < 0 || y < 0) {
        return -1;
    }
    
    int tileX = x / m_tileWidth;
    int tileY = y / m_tileHeight;
    
    if (tileX >= m_tilesPerRow || tileY >= m_tilesPerColumn) {
        return -1;
    }
    
    return tileY * m_tilesPerRow + tileX;
}

const TileInfo* TileManager::getTile(int index) const
{
    if (!m_initialized || index < 0 || index >= m_tiles.size()) {
        return nullptr;
    }
    
    return &m_tiles[index];
}

TileInfo* TileManager::getTile(int index)
{
    if (!m_initialized || index < 0 || index >= m_tiles.size()) {
        return nullptr;
    }
    
    return &m_tiles[index];
}

QVector<int> TileManager::detectChangedTiles(const QImage& currentFrame)
{
    // 使用优化的compareAndUpdateTiles方法
    return compareAndUpdateTiles(currentFrame);
}

QVector<int> TileManager::compareAndUpdateTiles(const QImage& currentFrame)
{
    QMutexLocker locker(&m_mutex);
    QVector<int> changedTiles;
    
    // 开始性能监控
    qint64 detectionStartTime = QDateTime::currentMSecsSinceEpoch();
    qint64 totalCrcTime = 0;
    qint64 totalHashTime = 0;
    
    if (!m_initialized) {
        return changedTiles;
    }

    if (currentFrame.isNull()) {
        return changedTiles;
    }
    
    // 确保图像格式一致，避免重复转换
    QImage workingImage = currentFrame;
    if (currentFrame.format() != QImage::Format_RGB32 && 
        currentFrame.format() != QImage::Format_ARGB32) {
        workingImage = currentFrame.convertToFormat(QImage::Format_RGB32);
    }
    
    const int imageWidth = workingImage.width();
    const int imageHeight = workingImage.height();
    const int bytesPerLine = workingImage.bytesPerLine();
    const uchar* imageData = workingImage.constBits();
    
    // 预分配变化瓦片列表，避免频繁内存分配
    changedTiles.reserve(m_tiles.size() / 4); // 预估25%的瓦片可能发生变化
    
    // 遍历所有瓦片进行变化检测
    for (int i = 0; i < m_tiles.size(); ++i) {
        TileInfo& tile = m_tiles[i];
        
        // 计算瓦片在图像中的有效区域
        const int tileRight = qMin(tile.x + tile.width, imageWidth);
        const int tileBottom = qMin(tile.y + tile.height, imageHeight);
        
        // 跳过超出图像范围的瓦片
        if (tile.x >= imageWidth || tile.y >= imageHeight) {
            tile.hasChanged = false;
            continue;
        }
        
        // 计算实际瓦片尺寸
        const int actualWidth = tileRight - tile.x;
        const int actualHeight = tileBottom - tile.y;
        
        if (actualWidth <= 0 || actualHeight <= 0) {
            tile.hasChanged = false;
            continue;
        }
        
        // 优化的哈希计算：直接访问图像数据，避免QImage::copy()
        qint64 hashStartTime = QDateTime::currentMSecsSinceEpoch();
        uint32_t newHash = calculateTileHashOptimized(imageData, bytesPerLine,
                                                     tile.x, tile.y, 
                                                     actualWidth, actualHeight);
        qint64 hashEndTime = QDateTime::currentMSecsSinceEpoch();
        totalHashTime += (hashEndTime - hashStartTime);
        
        // 检查哈希值是否发生变化
        if (tile.hash != newHash) {
            tile.hash = newHash;
            tile.hasChanged = true;
            changedTiles.append(i);
        } else {
            tile.hasChanged = false;
        }
    }
    
    // 计算总检测时间
    qint64 detectionEndTime = QDateTime::currentMSecsSinceEpoch();
    qint64 detectionTime = detectionEndTime - detectionStartTime;
    
    // 更新性能统计
    updatePerformanceStats(detectionTime, totalHashTime, totalCrcTime, changedTiles.size());
    
    // 只在有变化时发出信号
    if (!changedTiles.isEmpty()) {
        emit tilesChanged(changedTiles);
    }
    
    return changedTiles;
}

void TileManager::resetChangeFlags()
{
    QMutexLocker locker(&m_mutex);
    
    for (TileInfo& tile : m_tiles) {
        tile.hasChanged = false;
    }
}

int TileManager::getChangedTileCount() const
{
    // 不使用锁，因为这是const方法
    int count = 0;
    for (const TileInfo& tile : m_tiles) {
        if (tile.hasChanged) {
            count++;
        }
    }
    
    return count;
}

QVector<TileInfo> TileManager::getChangedTiles() const
{
    QVector<TileInfo> changedTiles;
    
    for (const TileInfo& tile : m_tiles) {
        if (tile.hasChanged) {
            changedTiles.append(tile);
        }
    }
    
    return changedTiles;
}

QVector<QPair<int, QRect>> TileManager::getChangedTileDetails() const
{
    QVector<QPair<int, QRect>> details;
    
    for (int i = 0; i < m_tiles.size(); ++i) {
        const TileInfo& tile = m_tiles[i];
        if (tile.hasChanged) {
            details.append(qMakePair(i, tile.rect()));
        }
    }
    
    return details;
}

// 计算最优瓦片大小
QSize TileManager::calculateOptimalTileSize(const QSize& screenSize)
{
    // 目标：在200-400个瓦片之间找到平衡点
    int screenArea = screenSize.width() * screenSize.height();
    int targetTileCount = 300; // 目标瓦片数量
    
    // 计算理想的瓦片面积
    int idealTileArea = screenArea / targetTileCount;
    int idealTileSize = qSqrt(idealTileArea);
    
    // 将瓦片大小调整到32的倍数（有利于内存对齐和SIMD优化）
    int alignedSize = ((idealTileSize + 31) / 32) * 32;
    
    // 限制瓦片大小范围：64-192像素
    alignedSize = qMax(64, qMin(192, alignedSize));
    
    // 返回计算后的最佳瓦片大小
    
    return QSize(alignedSize, alignedSize);
}

// 获取推荐的瓦片数量
int TileManager::getRecommendedTileCount(const QSize& screenSize)
{
    QSize optimalSize = calculateOptimalTileSize(screenSize);
    int tilesX = (screenSize.width() + optimalSize.width() - 1) / optimalSize.width();
    int tilesY = (screenSize.height() + optimalSize.height() - 1) / optimalSize.height();
    return tilesX * tilesY;
}

void TileManager::setPerformanceMode(bool enableFastMode)
{
    QMutexLocker locker(&m_mutex);
    m_fastMode = enableFastMode;
}

void TileManager::printTileInfo() const
{
    // 不使用锁，因为这是const方法
    // 信息打印已移除
}

uint32_t TileManager::calculateCRC32(const uchar* data, int length) const
{
    uint32_t crc = 0xFFFFFFFF;
    
    for (int i = 0; i < length; ++i) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
}

uint32_t TileManager::calculateTileHashOptimized(const uchar* imageData, int bytesPerLine,
                                               int tileX, int tileY, 
                                               int tileWidth, int tileHeight) const
{
    if (m_fastMode) {
        // 性能优化模式：采样计算哈希值，跳过部分像素
        uint32_t crc = 0xFFFFFFFF;
        const int bytesPerPixel = 4;
        const int sampleStep = 2; // 每隔2个像素采样一次
        
        for (int row = 0; row < tileHeight; row += sampleStep) {
            const uchar* rowStart = imageData + (tileY + row) * bytesPerLine + tileX * bytesPerPixel;
            
            for (int col = 0; col < tileWidth; col += sampleStep) {
                const uchar* pixelStart = rowStart + col * bytesPerPixel;
                
                // 只计算RGB三个通道，跳过Alpha通道
                for (int i = 0; i < 3; ++i) {
                    crc = crc32_table[(crc ^ pixelStart[i]) & 0xFF] ^ (crc >> 8);
                }
            }
        }
        
        return crc ^ 0xFFFFFFFF;
    } else {
        // 精确模式：计算所有像素的哈希值
        uint32_t crc = 0xFFFFFFFF;
        const int bytesPerPixel = 4;
        
        for (int row = 0; row < tileHeight; ++row) {
            const uchar* rowStart = imageData + (tileY + row) * bytesPerLine + tileX * bytesPerPixel;
            const int rowBytes = tileWidth * bytesPerPixel;
            
            for (int i = 0; i < rowBytes; ++i) {
                crc = crc32_table[(crc ^ rowStart[i]) & 0xFF] ^ (crc >> 8);
            }
        }
        
        return crc ^ 0xFFFFFFFF;
    }
}

void TileManager::createTileGrid()
{
    m_tiles.clear();
    m_tiles.reserve(m_tilesPerRow * m_tilesPerColumn);
    
    for (int row = 0; row < m_tilesPerColumn; ++row) {
        for (int col = 0; col < m_tilesPerRow; ++col) {
            int x = col * m_tileWidth;
            int y = row * m_tileHeight;
            
            // 计算实际瓦片尺寸（边缘瓦片可能较小）
            int actualWidth = qMin(m_tileWidth, m_screenSize.width() - x);
            int actualHeight = qMin(m_tileHeight, m_screenSize.height() - y);
            
            TileInfo tile(x, y, actualWidth, actualHeight);
            m_tiles.append(tile);
        }
    }
    
    // 创建完成
}

// ==================== 瓦片数据序列化功能 ====================

QByteArray TileManager::serializeTileData(const QVector<int>& changedTileIndices, const QImage& currentFrame) const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    
    if (!m_initialized || changedTileIndices.isEmpty()) {
        return QByteArray();
    }
    
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    // 写入头部信息
    stream << TileSerializationFormat::MAGIC_NUMBER;     // 魔数 (4字节)
    stream << TileSerializationFormat::VERSION;          // 版本 (2字节)
    stream << TileSerializationFormat::TILE_WITH_DATA;   // 数据类型 (1字节) - 包含图像数据
    stream << static_cast<quint8>(0);                    // 保留字节 (1字节)
    stream << static_cast<quint32>(changedTileIndices.size()); // 瓦片数量 (4字节)
    stream << static_cast<quint32>(0);                   // 保留字段 (4字节)
    
    // 写入每个变化瓦片的数据
    for (int index : changedTileIndices) {
        if (index >= 0 && index < m_tiles.size()) {
            const TileInfo& tile = m_tiles[index];
            
            // 写入瓦片基本信息
            
            // 写入瓦片基本信息
            stream << static_cast<quint32>(index);       // 瓦片索引 (4字节)
            stream << static_cast<quint16>(tile.x);      // X坐标 (2字节)
            stream << static_cast<quint16>(tile.y);      // Y坐标 (2字节)
            stream << static_cast<quint16>(tile.width);  // 宽度 (2字节)
            stream << static_cast<quint16>(tile.height); // 高度 (2字节)
            stream << tile.hash;                         // 哈希值 (4字节)
            
            
            // 提取并写入瓦片图像数据
            if (!currentFrame.isNull()) {
                QRect tileRect(tile.x, tile.y, tile.width, tile.height);
                QImage tileImage = currentFrame.copy(tileRect);
                
                
                // 将图像转换为PNG格式的字节数组（压缩）
                QByteArray imageData;
                QBuffer buffer(&imageData);
                buffer.open(QIODevice::WriteOnly);
                bool saveSuccess = tileImage.save(&buffer, "PNG");
                
                if (saveSuccess && !imageData.isEmpty()) {
                    stream << static_cast<quint32>(imageData.size()); // 图像数据大小
                    stream.writeRawData(imageData.constData(), imageData.size()); // 图像数据
                } else {
                    stream << static_cast<quint32>(0); // 无图像数据
                }
            } else {
                stream << static_cast<quint32>(0); // 无图像数据
            }
        }
    }
    
    return result;
}

// 性能统计相关方法
void TileManager::updatePerformanceStats(qint64 detectionTime, qint64 hashTime, qint64 crcTime, int changedTileCount)
{
    // 更新基本统计
    m_perfStats.totalDetections++;
    m_perfStats.totalChangedTiles += changedTileCount;
    m_perfStats.totalDetectionTime += detectionTime;
    m_perfStats.totalHashCalculationTime += hashTime;
    m_perfStats.totalCrcCalculationTime += crcTime;
    
    // 更新最大最小值
    if (detectionTime > m_perfStats.maxDetectionTime) {
        m_perfStats.maxDetectionTime = detectionTime;
    }
    if (detectionTime < m_perfStats.minDetectionTime) {
        m_perfStats.minDetectionTime = detectionTime;
    }
    
    // 记录最近的检测时间（保留最近100次）
    m_detectionTimes.append(detectionTime);
    if (m_detectionTimes.size() > 100) {
        m_detectionTimes.removeFirst();
    }
    
    // 记录最近的哈希计算时间
    if (hashTime > 0) {
        m_hashTimes.append(hashTime);
        if (m_hashTimes.size() > 100) {
            m_hashTimes.removeFirst();
        }
    }
    
    // 记录最近的CRC计算时间
    if (crcTime > 0) {
        m_crcTimes.append(crcTime);
        if (m_crcTimes.size() > 100) {
            m_crcTimes.removeFirst();
        }
    }
    
    // 计算平均值
    if (m_perfStats.totalDetections > 0) {
        m_perfStats.averageDetectionTime = m_perfStats.totalDetectionTime / m_perfStats.totalDetections;
        m_perfStats.averageHashTime = m_perfStats.totalHashCalculationTime / m_perfStats.totalDetections;
        m_perfStats.averageCrcTime = m_perfStats.totalCrcCalculationTime / m_perfStats.totalDetections;
        
        // 计算变化率
        m_perfStats.changeRate = static_cast<double>(m_perfStats.totalChangedTiles) / 
                                 (m_perfStats.totalDetections * m_tiles.size()) * 100.0;
    }
    
    // 计算检测帧率
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 timeDiff = currentTime - m_perfStats.lastDetectionTime;
    if (timeDiff > 0) {
        m_perfStats.detectionFPS = 1000.0 / timeDiff;
    }
    m_perfStats.lastDetectionTime = currentTime;
    
    // 输出统计日志已移除
}

void TileManager::resetPerformanceStats()
{
    QMutexLocker locker(&m_mutex);
    
    memset(&m_perfStats, 0, sizeof(m_perfStats));
    m_perfStats.minDetectionTime = LLONG_MAX;
    m_perfStats.lastDetectionTime = QDateTime::currentMSecsSinceEpoch();
    
    m_detectionTimes.clear();
    m_hashTimes.clear();
    m_crcTimes.clear();
    m_lastStatsUpdateTime = QDateTime::currentMSecsSinceEpoch();
    
}

bool TileManager::deserializeTileData(const QByteArray& data, QVector<TileInfo>& tiles, QVector<QImage>& tileImages) const
{
    if (data.isEmpty()) {
        return false;
    }
    
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    // 验证数据格式
    if (!validateSerializedData(data)) {
        return false;
    }
    
    // 读取头部信息
    quint32 magicNumber;
    quint16 version;
    quint8 dataType;
    quint8 reserved1;
    quint32 tileCount;
    quint32 reserved2;
    
    stream >> magicNumber >> version >> dataType >> reserved1 >> tileCount >> reserved2;
    
    // 清空输出容器
    tiles.clear();
    tileImages.clear();
    tiles.reserve(tileCount);
    tileImages.reserve(tileCount);
    
    // 读取瓦片数据
    for (quint32 i = 0; i < tileCount; ++i) {
        // 读取瓦片数据
        
        if (dataType == TileSerializationFormat::TILE_BATCH || 
            dataType == TileSerializationFormat::TILE_WITH_DATA) {
            
            // 读取瓦片基本信息
            quint32 index;
            quint16 x, y, width, height;
            quint32 hash; // 修复：使用quint32与TileInfo结构体中的uint32_t保持一致
            
            stream >> index;
            stream >> x;
            stream >> y;
            stream >> width;
            stream >> height;
            stream >> hash;
            
            
            // 创建瓦片信息
            TileInfo tile(x, y, width, height);
            tile.hash = hash;
            tiles.append(tile);
            
            // 只有 TILE_WITH_DATA 类型才读取图像数据
            if (dataType == TileSerializationFormat::TILE_WITH_DATA) {
                
                quint32 imageDataSize;
                stream >> imageDataSize;
                
                if (imageDataSize > 0 && imageDataSize < 10000000) { // 添加合理性检查
                    QByteArray imageData(imageDataSize, 0);
                    stream.readRawData(imageData.data(), imageDataSize);
                    
                    // 从PNG数据创建图像
                    QImage tileImage;
                    if (tileImage.loadFromData(imageData, "PNG")) {
                        tileImages.append(tileImage);
                    } else {
                        tileImages.append(QImage()); // 添加空图像保持索引一致
                    }
                } else {
                    tileImages.append(QImage()); // 无图像数据
                }
            } else {
                // TILE_BATCH 类型不包含图像数据
                tileImages.append(QImage());
            }
        } else if (dataType == TileSerializationFormat::TILE_INFO_ONLY) {
            // 只读取瓦片信息，无图像数据
            quint16 x, y, width, height;
            quint32 hash; // 修复：使用quint32与TileInfo结构体中的uint32_t保持一致
            
            stream >> x >> y >> width >> height >> hash;
            
            TileInfo tile(x, y, width, height);
            tile.hash = hash;
            tiles.append(tile);
        }
    }
    
    return true;
}

TileInfo TileManager::deserializeTileInfo(const QByteArray& data) const
{
    TileInfo result;
    
    if (data.isEmpty() || !validateSerializedData(data)) {
        return result;
    }
    
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    // 跳过头部信息
    quint32 magicNumber;
    quint16 version;
    quint8 dataType;
    quint8 reserved1;
    quint32 tileCount;
    quint32 reserved2;
    
    stream >> magicNumber >> version >> dataType >> reserved1 >> tileCount >> reserved2;
    
    // 读取瓦片信息
    if (tileCount > 0) {
        quint16 x, y, width, height;
        quint32 hash; // 修复：使用quint32与TileInfo结构体中的uint32_t保持一致
        
        stream >> x >> y >> width >> height >> hash;
        
        result = TileInfo(x, y, width, height);
        result.hash = hash;
    }
    
    return result;
}

bool TileManager::validateSerializedData(const QByteArray& data) const
{
    if (data.size() < TileSerializationFormat::HEADER_SIZE) {
        return false;
    }
    
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    // 验证魔数
    quint32 magicNumber;
    stream >> magicNumber;
    if (magicNumber != TileSerializationFormat::MAGIC_NUMBER) {
        return false;
    }
    
    // 验证版本
    quint16 version;
    stream >> version;
    if (version != TileSerializationFormat::VERSION) {
        return false;
    }
    
    // 验证数据类型
    quint8 dataType;
    stream >> dataType;
    if (dataType != TileSerializationFormat::TILE_INFO_ONLY &&
        dataType != TileSerializationFormat::TILE_WITH_DATA &&
        dataType != TileSerializationFormat::TILE_BATCH) {
        return false;
    }
    
    return true;
}

QByteArray TileManager::serializeAllTiles(const QImage& currentFrame) const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    
    if (!m_initialized) {
        return QByteArray();
    }
    
    // 创建所有瓦片的索引列表
    QVector<int> allIndices;
    allIndices.reserve(m_tiles.size());
    for (int i = 0; i < m_tiles.size(); ++i) {
        allIndices.append(i);
    }
    
    return serializeTileData(allIndices, currentFrame);
}

QByteArray TileManager::serializeTileInfo(const TileInfo& tile) const
{
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    // 写入头部信息
    stream << TileSerializationFormat::MAGIC_NUMBER;     // 魔数
    stream << TileSerializationFormat::VERSION;          // 版本
    stream << TileSerializationFormat::TILE_INFO_ONLY;   // 数据类型
    stream << static_cast<quint8>(0);                    // 保留字节
    stream << static_cast<quint32>(1);                   // 瓦片数量
    stream << static_cast<quint32>(0);                   // 保留字段
    
    // 写入瓦片信息
    stream << static_cast<quint16>(tile.x);
    stream << static_cast<quint16>(tile.y);
    stream << static_cast<quint16>(tile.width);
    stream << static_cast<quint16>(tile.height);
    stream << tile.hash;
    
    return result;
}