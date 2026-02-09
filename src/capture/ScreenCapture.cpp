#include "ScreenCapture.h"
#include <QPixmap>
#include <QBuffer>
#include <QImageWriter>
#include <QGuiApplication>
#include <cstring> // for memcpy
#include <QRect>
#include <string>

#ifdef _WIN32
static bool getDisplaySettingsForScreen(QScreen *screen, DEVMODEW &dmOut)
{
    if (!screen) {
        return false;
    }
    const QString name = screen->name();
    if (name.isEmpty()) {
        return false;
    }
    QStringList candidates;
    candidates.append(name);
    if (!name.startsWith(QStringLiteral("\\\\.\\"))) {
        candidates.append(QStringLiteral("\\\\.\\") + name);
    }
    for (const QString &cand : candidates) {
        DEVMODEW dm;
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);
        const std::wstring wname = cand.toStdWString();
        if (EnumDisplaySettingsW(wname.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
            dmOut = dm;
            return true;
        }
    }
    return false;
}

static QSize getPhysicalScreenSize(QScreen *screen)
{
    DEVMODEW dm;
    if (getDisplaySettingsForScreen(screen, dm)) {
        if (dm.dmPelsWidth > 0 && dm.dmPelsHeight > 0) {
            return QSize(static_cast<int>(dm.dmPelsWidth), static_cast<int>(dm.dmPelsHeight));
        }
    }
    return QSize();
}
#endif

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
    
    m_screenSize = m_primaryScreen->size();
#ifdef _WIN32
    const QSize physicalSize = getPhysicalScreenSize(m_primaryScreen);
    if (physicalSize.isValid()) {
        m_screenSize = physicalSize;
    }
#endif
    
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
    
    RECT targetRect = { 0, 0, 0, 0 };
    bool hasTargetRect = false;
#ifdef _WIN32
    DEVMODEW dm;
    if (getDisplaySettingsForScreen(m_primaryScreen, dm)) {
        targetRect.left = dm.dmPosition.x;
        targetRect.top = dm.dmPosition.y;
        targetRect.right = dm.dmPosition.x + static_cast<LONG>(dm.dmPelsWidth);
        targetRect.bottom = dm.dmPosition.y + static_cast<LONG>(dm.dmPelsHeight);
        hasTargetRect = true;
    }
#endif
    if (!hasTargetRect) {
        qreal dpr = m_primaryScreen->devicePixelRatio();
        QRect geo = m_primaryScreen->geometry();
        targetRect = { 
            (LONG)(geo.x() * dpr), 
            (LONG)(geo.y() * dpr),
            (LONG)((geo.x() + geo.width()) * dpr),
            (LONG)((geo.y() + geo.height()) * dpr) 
        };
    }

    int selectedOutputIndex = 0;
    const QString screenName = m_primaryScreen->name();
    ComPtr<IDXGIOutput> fallbackOutput;
    for (int i = 0; i < 8; ++i) {
        ComPtr<IDXGIOutput> out;
        HRESULT hrEnum = m_dxgiAdapter->EnumOutputs(i, out.GetAddressOf());
        if (FAILED(hrEnum)) {
            break; // 无更多输出
        }
        DXGI_OUTPUT_DESC desc;
        if (SUCCEEDED(out->GetDesc(&desc))) {
            if (!screenName.isEmpty()) {
                const QString deviceName = QString::fromWCharArray(desc.DeviceName);
                QString normalizedScreenName = screenName;
                if (!normalizedScreenName.startsWith(QStringLiteral("\\\\.\\"))) {
                    normalizedScreenName = QStringLiteral("\\\\.\\") + normalizedScreenName;
                }
                if (deviceName.compare(normalizedScreenName, Qt::CaseInsensitive) == 0) {
                    m_dxgiOutput = out;
                    selectedOutputIndex = i;
                    break;
                }
            }
            int w = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
            int h = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
            if (desc.DesktopCoordinates.left == targetRect.left &&
                desc.DesktopCoordinates.top == targetRect.top &&
                w == (targetRect.right - targetRect.left) &&
                h == (targetRect.bottom - targetRect.top)) {
                if (!fallbackOutput) {
                    fallbackOutput = out;
                    selectedOutputIndex = i;
                }
            }
        }
    }
    if (!m_dxgiOutput && fallbackOutput) {
        m_dxgiOutput = fallbackOutput;
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
    
    DXGI_OUTPUT_DESC selectedDesc;
    if (SUCCEEDED(m_dxgiOutput->GetDesc(&selectedDesc))) {
        int w = selectedDesc.DesktopCoordinates.right - selectedDesc.DesktopCoordinates.left;
        int h = selectedDesc.DesktopCoordinates.bottom - selectedDesc.DesktopCoordinates.top;
        if (w > 0 && h > 0) {
            m_screenSize = QSize(w, h);
        }
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
    
    D3D11_TEXTURE2D_DESC texDesc = {};
    desktopTexture->GetDesc(&texDesc);
    bool sizeChanged = false;
    if (texDesc.Width > 0 && texDesc.Height > 0) {
        if (m_screenSize.width() != static_cast<int>(texDesc.Width) || m_screenSize.height() != static_cast<int>(texDesc.Height)) {
            m_screenSize = QSize(static_cast<int>(texDesc.Width), static_cast<int>(texDesc.Height));
            sizeChanged = true;
        }
    }
    if (m_stagingTexture) {
        D3D11_TEXTURE2D_DESC stageDesc = {};
        m_stagingTexture->GetDesc(&stageDesc);
        if (stageDesc.Width != texDesc.Width || stageDesc.Height != texDesc.Height) {
            sizeChanged = true;
        }
    }
    if (sizeChanged) {
        m_stagingTexture.Reset();
        D3D11_TEXTURE2D_DESC newDesc = {};
        newDesc.Width = m_screenSize.width();
        newDesc.Height = m_screenSize.height();
        newDesc.MipLevels = 1;
        newDesc.ArraySize = 1;
        newDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        newDesc.SampleDesc.Count = 1;
        newDesc.Usage = D3D11_USAGE_STAGING;
        newDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(m_d3dDevice->CreateTexture2D(&newDesc, nullptr, m_stagingTexture.GetAddressOf()))) {
            m_dxgiOutputDuplication->ReleaseFrame();
            return HardwareError;
        }
    }

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
    
    QPixmap screenshot = m_primaryScreen->grabWindow(0);
    if (screenshot.isNull()) {
        return QByteArray();
    }
    
    QImage image = screenshot.toImage();
    if (image.isNull()) {
        return QByteArray();
    }

    QSize expectedSize;
#ifdef _WIN32
    expectedSize = getPhysicalScreenSize(m_primaryScreen);
#endif
    if (!expectedSize.isValid()) {
        const qreal dpr = m_primaryScreen->devicePixelRatio();
        const QSize logicalSize = m_primaryScreen->geometry().size();
        expectedSize = QSize(qRound(logicalSize.width() * dpr), qRound(logicalSize.height() * dpr));
    }
    if (expectedSize.isValid() && image.size() != expectedSize) {
        image = image.scaled(expectedSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    image.setDevicePixelRatio(1.0);

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
