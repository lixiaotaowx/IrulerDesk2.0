#include "ScreenCapture.h"
#include <QDebug>
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
{
}

ScreenCapture::~ScreenCapture()
{
    cleanup();
}

bool ScreenCapture::initialize()
{
    qDebug() << "[ScreenCapture] 初始化屏幕捕获...";
    
    // 获取主屏幕信息
    m_primaryScreen = QGuiApplication::primaryScreen();
    if (!m_primaryScreen) {
        qCritical() << "[ScreenCapture] 无法获取主屏幕";
        return false;
    }
    
    m_screenSize = m_primaryScreen->size();
    qDebug() << "[ScreenCapture] 屏幕尺寸:" << m_screenSize;
    
#ifdef _WIN32
    // 尝试使用D3D11进行硬件加速捕获
    if (initializeD3D11()) {
        m_useD3D11 = true;
        qDebug() << "[ScreenCapture] 使用D3D11硬件加速捕获";
    } else {
        qWarning() << "[ScreenCapture] D3D11初始化失败，使用Qt软件捕获";
        m_useD3D11 = false;
    }
#else
    m_useD3D11 = false;
    qDebug() << "[ScreenCapture] 使用Qt软件捕获";
#endif
    
    m_initialized = true;
    return true;
}

void ScreenCapture::cleanup()
{
    if (!m_initialized) {
        return;
    }
    
    qDebug() << "[ScreenCapture] 清理屏幕捕获资源...";
    
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
    
#ifdef _WIN32
    if (m_useD3D11) {
        QByteArray d3d11Data;
        CaptureResult result = captureWithD3D11(d3d11Data);
        
        if (result == Success) {
            return d3d11Data;
        } else if (result == HardwareError) {
            qWarning() << "[ScreenCapture] D3D11硬件错误，回退到Qt捕获";
            m_useD3D11 = false;
        }
    }
#endif
    
    return captureWithQt();
}

#ifdef _WIN32
bool ScreenCapture::initializeD3D11()
{
    qDebug() << "[ScreenCapture] 开始初始化D3D11硬件加速捕获...";
    
    // 创建DXGI Factory
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)m_dxgiFactory.GetAddressOf());
    if (FAILED(hr)) {
        qWarning() << "[ScreenCapture] 创建DXGI Factory失败:" << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }
    qDebug() << "[ScreenCapture] DXGI Factory创建成功";
    
    // 获取适配器
    hr = m_dxgiFactory->EnumAdapters1(0, m_dxgiAdapter.GetAddressOf());
    if (FAILED(hr)) {
        qWarning() << "[ScreenCapture] 获取DXGI适配器失败:" << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }
    qDebug() << "[ScreenCapture] DXGI适配器获取成功";
    
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
        qWarning() << "[ScreenCapture] 创建D3D11设备失败:" << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }
    qDebug() << "[ScreenCapture] D3D11设备创建成功";
    
    // 获取输出
    hr = m_dxgiAdapter->EnumOutputs(0, m_dxgiOutput.GetAddressOf());
    if (FAILED(hr)) {
        qWarning() << "[ScreenCapture] 获取DXGI输出失败:" << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        qWarning() << "[ScreenCapture] 可能原因：没有连接显示器或显卡驱动问题";
        return false;
    }
    qDebug() << "[ScreenCapture] DXGI输出获取成功";
    
    // 获取Output1接口
    hr = m_dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)m_dxgiOutput1.GetAddressOf());
    if (FAILED(hr)) {
        qWarning() << "[ScreenCapture] 获取IDXGIOutput1接口失败:" << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        qWarning() << "[ScreenCapture] 可能原因：系统不支持DXGI 1.2或更高版本";
        return false;
    }
    qDebug() << "[ScreenCapture] IDXGIOutput1接口获取成功";
    
    // 创建桌面复制
    hr = m_dxgiOutput1->DuplicateOutput(m_d3dDevice.Get(), m_dxgiOutputDuplication.GetAddressOf());
    if (FAILED(hr)) {
        qWarning() << "[ScreenCapture] 创建桌面复制失败:" << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        if (hr == static_cast<HRESULT>(0x887A0004)) { // DXGI_ERROR_UNSUPPORTED
            qWarning() << "[ScreenCapture] 错误：桌面复制不支持，可能是远程桌面或虚拟机环境";
        } else if (hr == static_cast<HRESULT>(0x887A0022)) { // DXGI_ERROR_NOT_CURRENTLY_AVAILABLE
            qWarning() << "[ScreenCapture] 错误：桌面复制当前不可用，可能有其他程序正在使用";
        }
        return false;
    }
    qDebug() << "[ScreenCapture] 桌面复制创建成功";
    
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
        qWarning() << "[ScreenCapture] 创建暂存纹理失败:" << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }
    qDebug() << "[ScreenCapture] 暂存纹理创建成功";
    
    return true;
}

ScreenCapture::CaptureResult ScreenCapture::captureWithD3D11(QByteArray &frameData)
{
    if (!m_d3dDevice || !m_dxgiOutputDuplication || !m_stagingTexture) {
        qWarning() << "[ScreenCapture] D3D11组件未初始化 - Device:" << (m_d3dDevice ? "OK" : "NULL") 
                   << ", OutputDuplication:" << (m_dxgiOutputDuplication ? "OK" : "NULL")
                   << ", StagingTexture:" << (m_stagingTexture ? "OK" : "NULL");
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
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            qWarning() << "[ScreenCapture] 桌面复制访问丢失，需要重新初始化";
        } else {
            qWarning() << "[ScreenCapture] AcquireNextFrame失败:" << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        }
        return HardwareError;
    }
    
    // 获取纹理接口
    ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)desktopTexture.GetAddressOf());
    if (FAILED(hr)) {
        qWarning() << "[ScreenCapture] QueryInterface ID3D11Texture2D失败:" << QString("0x%1").arg(hr, 8, 16, QChar('0'));
        m_dxgiOutputDuplication->ReleaseFrame();
        return HardwareError;
    }
    
    // 复制到暂存纹理
    m_d3dContext->CopyResource(m_stagingTexture.Get(), desktopTexture.Get());
    
    // 映射暂存纹理
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = m_d3dContext->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        qWarning() << "[ScreenCapture] Map暂存纹理失败:" << QString("0x%1").arg(hr, 8, 16, QChar('0'));
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
        qWarning() << "[ScreenCapture] m_primaryScreen为空";
        return QByteArray();
    }
    
    // 使用Qt进行屏幕截图
    QPixmap screenshot = m_primaryScreen->grabWindow(0);
    if (screenshot.isNull()) {
        qWarning() << "[ScreenCapture] Qt屏幕截图失败";
        return QByteArray();
    }
    
    // 转换为QImage
    QImage image = screenshot.toImage();
    if (image.isNull()) {
        qWarning() << "[ScreenCapture] 转换为QImage失败";
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