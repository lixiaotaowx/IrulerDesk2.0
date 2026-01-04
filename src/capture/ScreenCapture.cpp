#include "ScreenCapture.h"
#include <QPixmap>
#include <QBuffer>
#include <QImageWriter>
#include <QGuiApplication>
#include <cstring> // for memcpy
#include <QRect>

ScreenCapture::ScreenCapture(QObject *parent)
    : QObject(parent)
    , m_initialized(false)
    , m_useD3D11(false)
    , m_primaryScreen(nullptr)
    , m_frameCounter(0)
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
    
    // 获取物理分辨率（处理DPI缩放）
    qreal dpr = m_primaryScreen->devicePixelRatio();
    m_screenSize = m_primaryScreen->size() * dpr;
    // qDebug() << "[ScreenCapture] Screen size:" << m_screenSize << "DPR:" << dpr;
    
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
            return frameData;
        } else if (result == HardwareError) {
            m_useD3D11 = false;
        }
    }
#endif
    
    frameData = captureWithQt();
    
    return frameData;
}

    // 瓦片系统相关方法已移除


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
    // 注意：Qt的geometry是逻辑坐标，DXGI是物理坐标，需要转换
    qreal dpr = m_primaryScreen->devicePixelRatio();
    QRect geo = m_primaryScreen->geometry();
    RECT targetRect = { 
        (LONG)(geo.x() * dpr), 
        (LONG)(geo.y() * dpr),
        (LONG)((geo.x() + geo.width()) * dpr),
        (LONG)((geo.y() + geo.height()) * dpr) 
    };

    int selectedOutputIndex = 0;
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
    // [Fix] Ensure staging texture matches the desktop texture description
    // m_screenSize might be derived from logical size * dpr, which might be slightly off or misinterpreted.
    // Better to use the description from the output duplication if possible, but we don't have it yet.
    // However, we rely on m_screenSize being correct. If m_screenSize is logical (small) but desktop is physical (large),
    // creating a small staging texture and copying a large desktop texture into it will fail or crop?
    // CopyResource requires same dimensions. If dimensions differ, the call is ignored or invalid.
    // If CopyResource fails, we fallback to Qt.
    // But if m_screenSize IS physical, then it should work.
    
    // Let's ensure m_screenSize is updated to physical size if we can detect it better here?
    // Actually, let's just use m_screenSize as calculated. If CopyResource fails later, we know why.
    
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

    // [Fix] Verify texture dimensions match staging texture
    D3D11_TEXTURE2D_DESC desc;
    desktopTexture->GetDesc(&desc);
    if (desc.Width != m_screenSize.width() || desc.Height != m_screenSize.height()) {
        // Dimensions mismatch (e.g. D3D returned physical 3840, but we expected logical 1920)
        // We must recreate the staging texture to match the actual source.
        // qDebug() << "Texture size mismatch! Source:" << desc.Width << "x" << desc.Height << " Expected:" << m_screenSize.width() << "x" << m_screenSize.height();
        
        m_screenSize.setWidth(desc.Width);
        m_screenSize.setHeight(desc.Height);
        
        // Release old staging
        m_stagingTexture.Reset();
        
        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width = desc.Width;
        stagingDesc.Height = desc.Height;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        
        hr = m_d3dDevice->CreateTexture2D(&stagingDesc, nullptr, m_stagingTexture.GetAddressOf());
        if (FAILED(hr)) {
            m_dxgiOutputDuplication->ReleaseFrame();
            return HardwareError;
        }
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
    QPixmap screenshot;
    
    // [Fix] Use explicit logical coordinates for capture.
    // Passing physical coordinates causes Qt to capture an area larger than the screen (zoom out).
    screenshot = m_primaryScreen->grabWindow(0, 
        m_primaryScreen->geometry().x(), 
        m_primaryScreen->geometry().y(), 
        m_primaryScreen->size().width(), 
        m_primaryScreen->size().height());

    if (screenshot.isNull()) {
        return QByteArray();
    }
    
    // [Fix] Force devicePixelRatio to 1.0 to ensure consistent pixel dimensions
    screenshot.setDevicePixelRatio(1.0);
    
    // 转换为QImage
    QImage image = screenshot.toImage();
    if (image.isNull()) {
        return QByteArray();
    }

    // 更新屏幕尺寸（Qt捕获的尺寸可能与D3D11不同，或者是逻辑/物理尺寸变化）
    if (image.size() != m_screenSize) {
        m_screenSize = image.size();
    }

    // 确保格式为Format_ARGB32（与D3D11保持一致，且便于后续处理）
    if (image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_ARGB32);
    }
    
    // 紧凑打包数据（去除可能的填充字节，确保main_capture能正确解析）
    int width = image.width();
    int height = image.height();
    int bpp = 4;
    
    QByteArray frameData;
    frameData.resize(width * height * bpp);
    
    if (image.bytesPerLine() == width * bpp) {
        memcpy(frameData.data(), image.constBits(), frameData.size());
    } else {
        const uchar* src = image.constBits();
        uchar* dst = reinterpret_cast<uchar*>(frameData.data());
        for (int y = 0; y < height; ++y) {
            memcpy(dst + y * width * bpp, src + y * image.bytesPerLine(), width * bpp);
        }
    }
    
    return frameData;
}

