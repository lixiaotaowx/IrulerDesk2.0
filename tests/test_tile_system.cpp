#include "src/capture/ScreenCapture.h"
#include <QCoreApplication>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    qDebug() << "=== 测试瓦片系统 ===";
    
    // 创建ScreenCapture实例
    ScreenCapture screenCapture;
    
    // 初始化屏幕捕获
    if (!screenCapture.initialize()) {
        qCritical() << "ScreenCapture初始化失败！";
        return 1;
    }
    
    qDebug() << "ScreenCapture初始化成功，屏幕尺寸:" << screenCapture.getScreenSize();
    
    // 测试不同的瓦片尺寸
    QList<QSize> tileSizes = {
        QSize(64, 64),   // 默认尺寸
        QSize(128, 128), // 较大尺寸
        QSize(32, 32)    // 较小尺寸
    };
    
    for (const QSize &tileSize : tileSizes) {
        qDebug() << "\n--- 测试瓦片尺寸:" << tileSize << "---";
        
        if (screenCapture.initializeTileSystem(tileSize)) {
            qDebug() << "瓦片系统初始化成功！";
            qDebug() << "总瓦片数:" << screenCapture.getTileCount();
            qDebug() << "变化瓦片数:" << screenCapture.getChangedTileCount();
        } else {
            qWarning() << "瓦片系统初始化失败！";
        }
    }
    
    qDebug() << "\n=== 测试完成 ===";
    return 0;
}