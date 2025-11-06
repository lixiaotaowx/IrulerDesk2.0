#include "ScreenCapture.h"
#include <QPixmap>
#include <QBuffer>
#include <QImageWriter>
#include <QGuiApplication>

ScreenCapture::ScreenCapture(QObject *parent)
    : QObject(parent)
    , m_initialized(false)
    , m_useD3D11(false)
    , m_primaryScreen(nullptr)
    , m_frameCounter(0)
    , m_tileEnabled(false)
    , m_tileDetectionEnabled(true)  // 默认启用瓦片检测
    , m_tileSize(64, 64)
    , m_targetScreenIndex(-1)
{
}

ScreenCapture::~ScreenCapture()
{
    cleanup();
}

bool ScreenCapture::initialize()
{
    
    
    // 选择目标屏幕（默认主屏幕；如设置了索引则使用对应屏幕）
    const auto screens = QGuiApplication::screens();
    if (m_targetScreenIndex >= 0 && m_targetScreenIndex < screens.size()) {
        m_primaryScreen = screens[m_targetScreenIndex];
    } else {
        m_primaryScreen = QGuiApplication::primaryScreen();
    }
    if (!m_primaryScreen) {
        return false;
    }
    
    m_screenSize = m_primaryScreen->size();
    
#ifdef _WIN32
    // 尝试使用D3D11进行硬件加速捕获
    if (initializeD3D11()) {
        m_useD3D11 = true;
    } else {
        m_useD3D11 = false;
    }
#else
    m_useD3D11 = false;
    
#endif
    
    m_initialized = true;
    return true;
}

void ScreenCapture::cleanup()
{
    if (!m_initialized) {
        return;
    }
    
    
#ifdef _WIN32
    if (m_dxgiOutputDuplication) {
        m_dxgiOutputDuplication->ReleaseFrame();
        m_dxgiOutputDuplication.Reset();
    }
    
    m_stagingTexture.Reset();
    m_dxgiOutput1.Reset();
    m_dxgiOutput.Reset();
    m_dxgiAdapter.Reset();
    m_dxgiFactory.Reset();
    m_d3dContext.Reset();
    m_d3dDevice.Reset();
#endif
    
    m_initialized = false;
}

QByteArray ScreenCapture::captureScreen()
{
    if (!m_initialized) {
        return QByteArray();
    }
    
    m_frameCounter++;
    
    QByteArray frameData;
    
#ifdef _WIN32
    if (m_useD3D11) {
        CaptureResult result = captureWithD3D11(frameData);
        
        if (result == Success) {
            // D3D11捕获成功，进行瓦片检测
            if (m_tileEnabled && m_tileDetectionEnabled && m_tileManager.isInitialized()) {
                performTileDetection(frameData);
            }
            return frameData;
        } else if (result == HardwareError) {
            m_useD3D11 = false;
        }
    }
#endif
    
    frameData = captureWithQt();
    
    // Qt捕获成功，进行瓦片检测
    if (!frameData.isEmpty() && m_tileEnabled && m_tileDetectionEnabled && m_tileManager.isInitialized()) {
        performTileDetection(frameData);
    }
    
    return frameData;
}

#ifdef _WIN32
bool ScreenCapture::initializeD3D11()
{
    
    
    // 创建DXGI Factory
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)m_dxgiFactory.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    
    // 获取适配器
    hr = m_dxgiFactory->EnumAdapters1(0, m_dxgiAdapter.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    
    // 创建D3D11设备 - 优化：直接使用发布模式，避免调试开销
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        m_dxgiAdapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        0, // 移除调试标志以提升性能和减少启动时间
        nullptr,
        0,
        D3D11_SDK_VERSION,
        m_d3dDevice.GetAddressOf(),
        &featureLevel,
        m_d3dContext.GetAddressOf()
    );
    
    if (FAILED(hr)) {
        return false;
    }
    
    // 选择对应DXGI输出：根据Qt屏幕geometry匹配
    int selectedOutputIndex = 0;
    RECT targetRect = { m_primaryScreen->geometry().x(), m_primaryScreen->geometry().y(),
                        m_primaryScreen->geometry().x() + m_primaryScreen->geometry().width(),
                        m_primaryScreen->geometry().y() + m_primaryScreen->geometry().height() };
    for (int i = 0; i < 8; ++i) {
        ComPtr<IDXGIOutput> out;
        HRESULT hrEnum = m_dxgiAdapter->EnumOutputs(i, out.GetAddressOf());
        if (FAILED(hrEnum)) {
            break; // 无更多输出
        }
        DXGI_OUTPUT_DESC desc;
        if (SUCCEEDED(out->GetDesc(&desc))) {
            int w = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
            int h = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
            if (desc.DesktopCoordinates.left == targetRect.left &&
                desc.DesktopCoordinates.top == targetRect.top &&
                w == (targetRect.right - targetRect.left) &&
                h == (targetRect.bottom - targetRect.top)) {
                m_dxgiOutput = out; // 选择匹配输出
                selectedOutputIndex = i;
                break;
            }
        }
    }
    if (!m_dxgiOutput) {
        // 未匹配到则回退使用第0个输出
        hr = m_dxgiAdapter->EnumOutputs(0, m_dxgiOutput.GetAddressOf());
    } else {
        hr = S_OK;
    }
    if (FAILED(hr)) {
        return false;
    }
    
    // 获取Output1接口
    hr = m_dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)m_dxgiOutput1.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    
    // 创建桌面复制
    hr = m_dxgiOutput1->DuplicateOutput(m_d3dDevice.Get(), m_dxgiOutputDuplication.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    
    // 创建暂存纹理
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = m_screenSize.width();
    stagingDesc.Height = m_screenSize.height();
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    
    hr = m_d3dDevice->CreateTexture2D(&stagingDesc, nullptr, m_stagingTexture.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    
    return true;
}

ScreenCapture::CaptureResult ScreenCapture::captureWithD3D11(QByteArray &frameData)
{
    if (!m_d3dDevice || !m_dxgiOutputDuplication || !m_stagingTexture) {
        return HardwareError;
    }
    
    // qDebug() << "[ScreenCapture] 开始D3D11捕获帧...";
    
    HRESULT hr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<IDXGIResource> desktopResource;
    
    // 获取下一帧
    hr = m_dxgiOutputDuplication->AcquireNextFrame(0, &frameInfo, desktopResource.GetAddressOf());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        // 没有新帧，返回空数据
        // qDebug() << "[ScreenCapture] D3D11 WAIT_TIMEOUT - 没有新帧";
        return NoNewFrame;
    }
    
    if (FAILED(hr)) {
        return HardwareError;
    }
    
    // 获取纹理接口
    ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)desktopTexture.GetAddressOf());
    if (FAILED(hr)) {
        m_dxgiOutputDuplication->ReleaseFrame();
        return HardwareError;
    }
    
    // 复制到暂存纹理
    m_d3dContext->CopyResource(m_stagingTexture.Get(), desktopTexture.Get());
    
    // 映射暂存纹理
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = m_d3dContext->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        m_dxgiOutputDuplication->ReleaseFrame();
        return HardwareError;
    }
    
    // 创建帧数据
    int width = m_screenSize.width();
    int height = m_screenSize.height();
    int bytesPerPixel = 4; // RGBA
    
    frameData.resize(width * height * bytesPerPixel);
    
    const unsigned char* srcData = static_cast<const unsigned char*>(mappedResource.pData);
    unsigned char* dstData = reinterpret_cast<unsigned char*>(frameData.data());
    
    // D3D11使用BGRA格式，在小端序系统上与libyuv期望的ARGB内存布局相同，直接复制
    for (int y = 0; y < height; ++y) {
        const unsigned char* srcRow = srcData + y * mappedResource.RowPitch;
        unsigned char* dstRow = dstData + y * width * bytesPerPixel;
        
        // 直接按行复制像素数据，无需颜色通道转换
        memcpy(dstRow, srcRow, width * bytesPerPixel);
    }
    
    // 取消映射
    m_d3dContext->Unmap(m_stagingTexture.Get(), 0);
    
    // 释放帧
    m_dxgiOutputDuplication->ReleaseFrame();
    
    // qDebug() << "[ScreenCapture] D3D11捕获成功";
    return Success;
}
#endif

QByteArray ScreenCapture::captureWithQt()
{
    if (!m_primaryScreen) {
        return QByteArray();
    }
    
    // 使用Qt进行屏幕截图
    QPixmap screenshot = m_primaryScreen->grabWindow(0);
    if (screenshot.isNull()) {
        return QByteArray();
    }
    
    // 转换为QImage
    QImage image = screenshot.toImage();
    if (image.isNull()) {
        return QByteArray();
    }
    
    // 确保格式为ARGB32 (VP9编码器期望ARGB格式)
    if (image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_ARGB32);
    }
    
    // 返回原始像素数据
    QByteArray frameData;
    frameData.resize(image.sizeInBytes());
    memcpy(frameData.data(), image.constBits(), image.sizeInBytes());
    
    return frameData;
}

// 瓦片系统相关方法实现
bool ScreenCapture::initializeTileSystem(const QSize &tileSize)
{
    if (!m_initialized) {
        return false;
    }
    
    // 如果启用自适应瓦片大小，计算最优尺寸
    QSize actualTileSize = tileSize;
    if (m_tileManager.isAdaptiveTileSizeEnabled()) {
        actualTileSize = TileManager::calculateOptimalTileSize(m_screenSize);
    }
    
    m_tileSize = actualTileSize;
    
    // qDebug() << "[ScreenCapture] 初始化瓦片系统..."; // 已禁用以提升性能
    
    
    if (m_tileManager.initialize(m_screenSize, m_tileSize.width(), m_tileSize.height())) {
        m_tileManager.printTileInfo();
        return true;
    } else {
        return false;
    }
}

// 重载的瓦片系统初始化方法（接受屏幕尺寸和瓦片尺寸）
void ScreenCapture::initializeTileSystem(const QSize& screenSize, const QSize& tileSize)
{
    // 如果启用自适应瓦片大小，计算最优尺寸
    QSize actualTileSize = tileSize;
    if (m_tileManager.isAdaptiveTileSizeEnabled()) {
        actualTileSize = TileManager::calculateOptimalTileSize(screenSize);
    }
    
    m_tileSize = actualTileSize;
    
    
    m_tileManager.initialize(screenSize, actualTileSize.width(), actualTileSize.height());
    
    int totalTiles = m_tileManager.getTileCount();
}

int ScreenCapture::getTileCount() const
{
    return m_tileManager.getTileCount();
}

int ScreenCapture::getChangedTileCount() const
{
    return m_tileManager.getChangedTileCount();
}

void ScreenCapture::performTileDetection(const QByteArray &frameData)
{
    if (frameData.isEmpty() || !m_tileManager.isInitialized()) {
        return;
    }
    
    // 将字节数组转换为QImage进行瓦片检测
    QImage image(reinterpret_cast<const uchar*>(frameData.constData()), 
                 m_screenSize.width(), m_screenSize.height(), 
                 QImage::Format_ARGB32);
    
    if (image.isNull()) {
        return;
    }
    
    // 执行瓦片变化检测
    m_tileManager.compareAndUpdateTiles(image);
}

// 瓦片检测开关控制方法实现
void ScreenCapture::setTileDetectionEnabled(bool enabled)
{
    if (m_tileDetectionEnabled != enabled) {
        m_tileDetectionEnabled = enabled;
        
        // 如果禁用瓦片检测，重置瓦片状态
        if (!enabled && m_tileManager.isInitialized()) {
            m_tileManager.resetChangeFlags();
        }
    }
}