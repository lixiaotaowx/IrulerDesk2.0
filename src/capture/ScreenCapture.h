#ifndef SCREENCAPTURE_H
#define SCREENCAPTURE_H

#include <QObject>
#include <QByteArray>
#include <QSize>
#include <QTimer>
#include <QPixmap>
#include <QScreen>
#include <QGuiApplication>
#include "TileManager.h"

#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

class ScreenCapture : public QObject
{
    Q_OBJECT

public:
    enum CaptureResult {
        Success,
        NoNewFrame,
        HardwareError
    };
    
    explicit ScreenCapture(QObject *parent = nullptr);
    ~ScreenCapture();
    
    bool initialize();
    void cleanup();
    
    QByteArray captureScreen();
    QSize getScreenSize() const { return m_screenSize; }
    
    // 瓦片相关方法
    bool initializeTileSystem(const QSize &tileSize = QSize(64, 64));
    void initializeTileSystem(const QSize& screenSize, const QSize& tileSize);
    void enableTileCapture(bool enable) { m_tileEnabled = enable; }
    bool isTileEnabled() const { return m_tileEnabled; }
    
    // 瓦片检测开关控制
    void setTileDetectionEnabled(bool enabled);
    bool isTileDetectionEnabled() const { return m_tileDetectionEnabled; }
    void toggleTileDetection() { setTileDetectionEnabled(!m_tileDetectionEnabled); }
    
    // 瓦片统计信息
    int getTileCount() const;
    int getChangedTileCount() const;
    TileManager& getTileManager() { return m_tileManager; }
    
    // 瓦片检测方法
    void performTileDetection(const QByteArray &frameData);
    
signals:
    void frameReady(const QByteArray &frameData);
    void error(const QString &errorMessage);

private:
    bool initializeD3D11();
    bool initializeDXGI();
    CaptureResult captureWithD3D11(QByteArray &frameData);
    QByteArray captureWithQt(); // 备用方案
    
    QSize m_screenSize;
    bool m_initialized;
    bool m_useD3D11;
    
    // 瓦片系统相关
    TileManager m_tileManager;
    bool m_tileEnabled;
    bool m_tileDetectionEnabled;  // 瓦片检测开关
    QSize m_tileSize;
    
#ifdef _WIN32
    // D3D11 相关
    ComPtr<ID3D11Device> m_d3dDevice;
    ComPtr<ID3D11DeviceContext> m_d3dContext;
    ComPtr<IDXGIOutputDuplication> m_dxgiOutputDuplication;
    ComPtr<ID3D11Texture2D> m_stagingTexture;
    
    // DXGI 相关
    ComPtr<IDXGIFactory1> m_dxgiFactory;
    ComPtr<IDXGIAdapter1> m_dxgiAdapter;
    ComPtr<IDXGIOutput> m_dxgiOutput;
    ComPtr<IDXGIOutput1> m_dxgiOutput1;
#endif
    
    // Qt备用捕获
    QScreen *m_primaryScreen;
    int m_frameCounter;
};

#endif // SCREENCAPTURE_H