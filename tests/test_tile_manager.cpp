#include "src/capture/TileManager.h"
#include <QCoreApplication>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    qDebug() << "开始测试TileManager类...";
    
    // 创建TileManager实例
    TileManager tileManager;
    
    // 初始化瓦片管理器
    QSize screenSize(1920, 1080);
    QSize tileSize(64, 64);
    
    if (tileManager.initialize(screenSize, tileSize)) {
        qDebug() << "TileManager初始化成功！";
        
        // 打印瓦片信息
        tileManager.printTileInfo();
        
        // 测试获取瓦片数量
        int totalTiles = tileManager.getTileCount();
        qDebug() << "总瓦片数:" << totalTiles;
        
        // 测试获取变化瓦片数量
        int changedTiles = tileManager.getChangedTileCount();
        qDebug() << "变化瓦片数:" << changedTiles;
        
        qDebug() << "TileManager类测试成功！";
    } else {
        qDebug() << "TileManager初始化失败！";
        return 1;
    }
    
    return 0;
}