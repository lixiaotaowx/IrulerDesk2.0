#include "VideoWindow.h"
#include "ui/SnippetOverlay.h"
#include <iostream>
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <QSignalBlocker>
#include <QCoreApplication>
#include <QDebug>
#include <QMenu>
#include <QAction>
#include <QAbstractItemView>
#include <QFile>
#include <QMovie>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QClipboard>
#include <QGuiApplication>
#include "common/AppConfig.h"
#ifdef _WIN32
#define NOMINMAX
#endif
#include <windows.h>
#include <iostream>
#include <algorithm>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace {

enum WindowCompositionAttribute {
    WCA_ACCENT_POLICY = 19
};

struct ACCENT_POLICY {
    int AccentState;
    int AccentFlags;
    int GradientColor;
    int AnimationId;
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    int Attrib;
    void *pvData;
    unsigned int cbData;
};

enum AccentState {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI *)(HWND, WINDOWCOMPOSITIONATTRIBDATA *);
using RtlGetVersionFn = LONG (WINAPI *)(OSVERSIONINFOW*);

static bool isWindows11OrGreater() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (!fn) return false;

    OSVERSIONINFOW rovi = { 0 };
    rovi.dwOSVersionInfoSize = sizeof(rovi);
    if (fn(&rovi) == 0) { // STATUS_SUCCESS
        return rovi.dwBuildNumber >= 22000;
    }
    return false;
}

static bool tryEnableAcrylicBlur(HWND hwnd, int abgrGradientColor)
{
    if (!hwnd) return false;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return false;
    auto fn = reinterpret_cast<SetWindowCompositionAttributeFn>(GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!fn) return false;

    ACCENT_POLICY policy{};
    WINDOWCOMPOSITIONATTRIBDATA data{};
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &policy;
    data.cbData = sizeof(policy);

    if (isWindows11OrGreater()) {
        policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
        policy.AccentFlags = 0;
        policy.GradientColor = abgrGradientColor;
        if (fn(hwnd, &data)) {
            return true;
        }
    }

    // Windows 10 or fallback
    // Use darker tint (Black 0x000000) with high alpha (0xE6) to avoid "whiteness"
    policy.AccentState = ACCENT_ENABLE_BLURBEHIND;
    policy.AccentFlags = 0;
    policy.GradientColor = 0xE6000000;

    return fn(hwnd, &data) != FALSE;
}

struct DwmMargins {
    int cxLeftWidth;
    int cxRightWidth;
    int cyTopHeight;
    int cyBottomHeight;
};

using DwmExtendFrameIntoClientAreaFn = HRESULT(WINAPI *)(HWND, const DwmMargins *);
using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);

static bool tryEnableDwmBackdropSimple(HWND hwnd, int backdropType, bool darkMode)
{
    if (!hwnd) return false;
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (!dwmapi) return false;
    auto setAttr = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
    if (!setAttr) {
        FreeLibrary(dwmapi);
        return false;
    }

    const int useDarkMode = darkMode ? 1 : 0;
    setAttr(hwnd, 20, &useDarkMode, sizeof(useDarkMode));

    HRESULT hrBackdrop = E_FAIL;
    if (backdropType >= 0) {
        hrBackdrop = setAttr(hwnd, 38, &backdropType, sizeof(backdropType));
    }

    FreeLibrary(dwmapi);
    return SUCCEEDED(hrBackdrop);
}

static void tryExtendGlassFrame(HWND hwnd)
{
    if (!hwnd) return;
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (!dwmapi) return;
    auto extendFrame = reinterpret_cast<DwmExtendFrameIntoClientAreaFn>(GetProcAddress(dwmapi, "DwmExtendFrameIntoClientArea"));
    if (!extendFrame) {
        FreeLibrary(dwmapi);
        return;
    }
    const DwmMargins margins{-1, -1, -1, -1};
    extendFrame(hwnd, &margins);
    FreeLibrary(dwmapi);
}

static void ensureLayeredForAcrylic(HWND hwnd)
{
    if (!hwnd) return;
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_LAYERED) == 0) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    }
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
}

static void applyRoundedRegion(HWND hwnd, bool enabled, int radiusPx)
{
    if (!hwnd) return;
    if (!enabled) {
        SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }
    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) return;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    const int r = radiusPx > 0 ? radiusPx : 0;
    const int d = r * 2;
    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, d, d);
    if (!rgn) return;
    if (!SetWindowRgn(hwnd, rgn, TRUE)) {
        DeleteObject(rgn);
    }
}

static constexpr int kHwndCornerRadiusPx = 10;

static bool trySetDwmCornerPreference(HWND hwnd, int cornerPreference, int radiusPx)
{
    if (!hwnd) return false;
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (!dwmapi) return false;
    auto setAttr = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
    if (!setAttr) {
        FreeLibrary(dwmapi);
        return false;
    }
    const HRESULT hrCorner = setAttr(hwnd, 33, &cornerPreference, sizeof(cornerPreference));
    HRESULT hrRadius = E_FAIL;
    if (radiusPx > 0) {
        hrRadius = setAttr(hwnd, 40, &radiusPx, sizeof(radiusPx));
    }
    FreeLibrary(dwmapi);
    return SUCCEEDED(hrCorner) || SUCCEEDED(hrRadius);
}

static void applyHwndCornerStyle(HWND hwnd, bool rounded)
{
    if (!hwnd) return;
    if (!rounded) {
        trySetDwmCornerPreference(hwnd, 1, 0);
        SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }
    trySetDwmCornerPreference(hwnd, 2, kHwndCornerRadiusPx);
    applyRoundedRegion(hwnd, true, kHwndCornerRadiusPx);
}

static constexpr int kAcrylicTintAbgr = 0x30FFFFFF;

} // namespace

VideoWindow::VideoWindow(QWidget *parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_titleBar(nullptr)
    , m_titleBarLayout(nullptr)
    , m_titleLabel(nullptr)
    , m_minimizeButton(nullptr)
    , m_maximizeButton(nullptr)
    , m_closeButton(nullptr)
    , m_colorButton(nullptr)
    , m_micButton(nullptr)
    , m_speakerButton(nullptr)
    , m_videoDisplayWidget(nullptr)
    , m_dragging(false)
    , m_isMaximized(false)
{
    setupUI();
    
    // 设置窗口属性
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground);
    
    // 设置16:9比例的视频内容区域尺寸（标题栏高度额外计算）
    int titleBarHeight = 28;  // 标题栏高度
    int videoWidth = 1280;
    int videoHeight = 720;  // 视频内容区域16:9比例
    int totalHeight = videoHeight + titleBarHeight;  // 总窗口高度 = 视频高度 + 标题栏高度
    
    // 最小尺寸：视频区域960x540 + 标题栏高度
    int minVideoWidth = 960;
    int minVideoHeight = 540;
    int minTotalHeight = minVideoHeight + titleBarHeight;
    
    setMinimumSize(minVideoWidth, minTotalHeight);
    resize(videoWidth, totalHeight);
    
    // 居中显示
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect screenGeometry = screen->availableGeometry();
        int x = (screenGeometry.width() - width()) / 2;
        int y = (screenGeometry.height() - height()) / 2;
        move(x, y);
    }
}

VideoWindow::~VideoWindow()
{
    if (m_textSizeLongPressTimer && m_textSizeLongPressTimer->isActive()) {
        m_textSizeLongPressTimer->stop();
    }
    if (m_volumeLongPressTimer && m_volumeLongPressTimer->isActive()) {
        m_volumeLongPressTimer->stop();
    }
    if (m_textSizeSlider) m_textSizeSlider->setVisible(false);
    if (m_volumeSlider) m_volumeSlider->setVisible(false);
    if (m_clipToast) {
        m_clipToast->hide();
    }
}

VideoDisplayWidget* VideoWindow::getVideoDisplayWidget() const
{
    return m_videoDisplayWidget;
}

void VideoWindow::startTitleTimer(const QString &userName)
{
    m_targetUserName = userName;
    m_startTime = QDateTime::currentMSecsSinceEpoch();
    
    if (!m_titleTimer) {
        m_titleTimer = new QTimer(this);
        connect(m_titleTimer, &QTimer::timeout, this, &VideoWindow::updateTitle);
    }
    m_titleTimer->start(1000); // Update every second
    updateTitle(); // Update immediately
}

void VideoWindow::updateTitle()
{
    if (!m_titleLabel) return;
    
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 elapsed = (now - m_startTime) / 1000;
    
    QTime t(0, 0, 0);
    t = t.addSecs(elapsed);
    
    QString timeStr = t.toString("HH:mm:ss");
    QString title = QString("%1  %2").arg(m_targetUserName).arg(timeStr);
    
    m_titleLabel->setText(title);
}

void VideoWindow::setMicChecked(bool checked)
{
    if (m_micButton) m_micButton->setChecked(checked);
}
void VideoWindow::setMicCheckedSilently(bool checked)
{
    if (!m_micButton) return;
    QSignalBlocker blocker(m_micButton);
    m_micButton->setChecked(checked);
    m_micButton->setIcon(checked ? m_micIconOn : m_micIconOff);
    m_micButton->setToolTip(checked ? QStringLiteral("麦克风：开") : QStringLiteral("麦克风：关"));
}
bool VideoWindow::isMicChecked() const
{
    return m_micButton ? m_micButton->isChecked() : false;
}
void VideoWindow::setSpeakerChecked(bool checked)
{
    if (m_speakerButton) m_speakerButton->setChecked(checked);
}
bool VideoWindow::isSpeakerChecked() const
{
    return m_speakerButton ? m_speakerButton->isChecked() : false;
}

void VideoWindow::setAudioCallRestoreVisible(bool visible)
{
    if (m_audioCallRestoreButton) {
        m_audioCallRestoreButton->setVisible(visible);
    }
}

void VideoWindow::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
    setupCustomTitleBar();
    
    // 创建视频显示组件
    m_videoDisplayWidget = new VideoDisplayWidget(this);
    m_videoDisplayWidget->setShowControls(false);
    m_videoDisplayWidget->setAutoResize(true);
    m_videoDisplayWidget->setStyleSheet(
        "VideoDisplayWidget {"
        "    background-color: transparent;"
        "    border: none;"
        "}"
    );
    // 双击全屏切换
    connect(m_videoDisplayWidget, &VideoDisplayWidget::fullscreenToggleRequested,
            this, &VideoWindow::toggleFullscreen);
    // 颜色变化时更新标题栏按钮外观
    connect(m_videoDisplayWidget, &VideoDisplayWidget::annotationColorChanged,
            this, [this](int colorId){ updateColorButtonVisual(colorId); });
    
    connect(m_videoDisplayWidget, &VideoDisplayWidget::avatarUpdateReceived, this, &VideoWindow::avatarUpdateReceived);
    connect(m_videoDisplayWidget, &VideoDisplayWidget::receivingStopped, this, &VideoWindow::receivingStopped);
    
    m_mainLayout->addWidget(m_videoDisplayWidget, 1);

    m_fullscreenBar = new QWidget(this);
    m_fullscreenBar->setFixedHeight(28);
    m_fullscreenBar->setStyleSheet("QWidget { background: transparent; }");
    m_fullscreenBar->setVisible(false);
    m_fullscreenBarLayout = new QHBoxLayout(m_fullscreenBar);
    m_fullscreenBarLayout->setContentsMargins(12, 0, 4, 0);
    m_fullscreenBarLayout->setSpacing(0);
    m_fullscreenPill = new QWidget(m_fullscreenBar);
    m_fullscreenPill->setObjectName("fullscreenPill");
    m_fullscreenPill->setStyleSheet("#fullscreenPill { background-color: rgba(0,0,0,120); border-radius: 14px; } ");
    m_fullscreenPillLayout = new QHBoxLayout(m_fullscreenPill);
    m_fullscreenPillLayout->setContentsMargins(10, 2, 10, 2);
    m_fullscreenPillLayout->setSpacing(6);
    m_fullscreenBarLayout->addStretch();
    m_fullscreenBarLayout->addWidget(m_fullscreenPill, 0, Qt::AlignCenter);
    m_fullscreenBarLayout->addStretch();
}

void VideoWindow::setupCustomTitleBar()
{
    // 创建标题栏 - 更窄更精致
    m_titleBar = new QWidget(this);
    m_titleBar->setFixedHeight(28);  // 从40减少到28，更窄
    m_titleBar->setStyleSheet(
        "QWidget {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "                stop:0 #3a3a3a, stop:1 #2a2a2a);"
        "    border-top-left-radius: 6px;"  // 减小圆角
        "    border-top-right-radius: 6px;"
        "}"
    );
    
    m_titleBarLayout = new QHBoxLayout(m_titleBar);
    m_titleBarLayout->setContentsMargins(12, 0, 4, 0);  // 减少边距
    m_titleBarLayout->setSpacing(0);
    
    // 标题标签 - 更小的字体
    m_titleLabel = new QLabel("视频播放器", m_titleBar);
    m_titleLabel->setStyleSheet(
        "QLabel {"
        "    color: #ffffff;"
        "    font-size: 12px;"  // 从14px减少到12px
        "    font-weight: 500;"  // 从bold改为medium
        "    background: transparent;"
        "}"
    );
    
    m_titleBarLayout->addWidget(m_titleLabel);
    m_titleBarLayout->addStretch();
    
    // 创建窗口控制按钮 - 更小更精致
    QString buttonStyle = 
        "QPushButton {"
        "    background-color: transparent;"
        "    border: none;"
        "    width: 24px;"   // 从30px减少到24px
        "    height: 24px;"
        "    border-radius: 12px;"  // 相应调整圆角
        "    font-size: 14px;"      // 从16px减少到14px
        "    font-weight: 500;"     // 从bold改为medium
        "    margin: 1px;"          // 从2px减少到1px
        "}"
        "QPushButton:hover {"
        "    background-color: rgba(255, 255, 255, 0.15);"  // 稍微增加透明度
        "}"
        "QPushButton:pressed {"
        "    background-color: rgba(255, 255, 255, 0.25);"
        "}";
    
    // 麦克风按钮（置于标题栏最右侧区域，靠近最小化）
    m_micButton = new QPushButton("", m_titleBar);
    m_micButton->setCheckable(true);
    m_micButton->setChecked(true);
    m_micButton->setToolTip(QStringLiteral("点击开启/关闭麦克风"));
    // 使用与头像相同的相对路径策略：appDir/maps/logo
    QString appDir = QCoreApplication::applicationDirPath();
    QString iconDir = appDir + "/maps/logo";
    setWindowIcon(QIcon(iconDir + "/iruler.ico"));
    m_micIconOff = QIcon(iconDir + "/Mic_off.png");
    m_micIconOn  = QIcon(iconDir + "/Mic_on.png");
    m_micButton->setIcon(m_micIconOn);
    m_micButton->setIconSize(QSize(16, 16));
    // 与窗口控制按钮风格一致
    QString micButtonStyle = 
        "QPushButton {"
        "    background-color: transparent;"
        "    border: none;"
        "    width: 24px;"
        "    height: 24px;"
        "    border-radius: 12px;"
        "    font-size: 14px;"
        "    font-weight: 500;"
        "    margin: 1px;"
        "}"
        "QPushButton:hover {"
        "    background-color: rgba(255, 255, 255, 0.15);"
        "}"
        "QPushButton:pressed {"
        "    background-color: rgba(255, 255, 255, 0.25);"
        "}";
    QString selectedToolStyle =
        "QPushButton {"
        "    background-color: rgba(32, 64, 120, 0.35);"
        "    border: none;"
        "    width: 24px;"
        "    height: 24px;"
        "    border-radius: 12px;"
        "    font-size: 14px;"
        "    font-weight: 500;"
        "    margin: 1px;"
        "}"
        "QPushButton:hover {"
        "    background-color: rgba(32, 64, 120, 0.45);"
        "}"
        "QPushButton:pressed {"
        "    background-color: rgba(32, 64, 120, 0.55);"
        "}";
    m_micButton->setStyleSheet(micButtonStyle);
    connect(m_micButton, &QPushButton::toggled, this, &VideoWindow::onMicToggled);
    m_micButton->installEventFilter(this);

    m_colorButton = new ColorCircleButton(m_titleBar);
    m_colorButton->setToolTip(QStringLiteral("批注颜色"));
    m_colorButton->setFixedSize(13, 13);
    m_colorButton->setCursor(Qt::PointingHandCursor);
    connect(m_colorButton, &ColorCircleButton::clicked, this, &VideoWindow::onColorClicked);
    if (m_videoDisplayWidget) { updateColorButtonVisual(m_videoDisplayWidget->annotationColorId()); } else { updateColorButtonVisual(0); }

    // 扬声器按钮（本地播放开/关，不影响推流端）
    m_speakerButton = new QPushButton("", m_titleBar);
    m_speakerButton->setCheckable(true);
    m_speakerButton->setChecked(true);
    m_speakerButton->setToolTip(QStringLiteral("点击开启/关闭扬声器"));
    m_speakerIconOff = QIcon(iconDir + "/laba_off.png");
    m_speakerIconOn  = QIcon(iconDir + "/laba_on.png");
    m_speakerButton->setIcon(m_speakerIconOn);
    m_speakerButton->setIconSize(QSize(16, 16));
    m_speakerButton->setStyleSheet(micButtonStyle);
    connect(m_speakerButton, &QPushButton::toggled, this, &VideoWindow::onSpeakerToggled);
    m_speakerButton->installEventFilter(this);

    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setVisible(false);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int v){ if (!m_videoDisplayWidget) return; if (m_volumeTargetMic) { m_videoDisplayWidget->setMicGainPercent(v); } else { m_videoDisplayWidget->setVolumePercent(v); } });
    m_volumeLongPressTimer = new QTimer(this);
    m_volumeLongPressTimer->setSingleShot(true);
    connect(m_volumeLongPressTimer, &QTimer::timeout, this, [this]() {
        if (!m_leftPressing) return;
        int current = 100;
        if (m_videoDisplayWidget) {
            current = m_volumeTargetMic ? m_videoDisplayWidget->micGainPercent() : m_videoDisplayWidget->volumePercent();
        }
        m_volumeSlider->setValue(current);
        QPushButton *btn = m_volumeTargetMic ? m_micButton : m_speakerButton;
        QPoint g = btn->mapToGlobal(QPoint(btn->width()/2, btn->height()));
        QPoint l = mapFromGlobal(g);
        int w = 160;
        int h = 20;
        int x = std::max(0, std::min(l.x() - w/2, this->width() - w));
        int y = std::max(0, std::min(l.y() + 4, this->height() - h));
        m_volumeSlider->setGeometry(x, y, w, h);
        m_volumeSlider->setStyleSheet(
            "QSlider::groove:horizontal { background: #ff9800; height: 3px; border-radius: 2px; }"
            "QSlider::handle:horizontal { background: #1565C0; width: 8px; margin: -8px 0; border-radius: 3px; }"
        );
        m_volumeSlider->setVisible(true);
        m_volumeSlider->raise();
        m_volumeDragActive = true;
        if (m_volumeTargetMic) {
            std::cout << "[UI] mic longpress success" << std::endl;
        } else {
            std::cout << "[UI] volume longpress success" << std::endl;
        }
        std::cout << "[UI] volume slider show x=" << x << " y=" << y << std::endl;
    });

    m_textSizeSlider = new QSlider(Qt::Horizontal, this);
    m_textSizeSlider->setRange(8, 72);
    m_textSizeSlider->setValue(m_textFontSize);
    m_textSizeSlider->setVisible(false);
    connect(m_textSizeSlider, &QSlider::valueChanged, this, [this](int v){
        m_textFontSize = v;
        if (m_textSizeFloatLabel) m_textSizeFloatLabel->setText(QString::number(m_textFontSize));
        updateTextSizeFloatLabelPos();
        if (m_videoDisplayWidget) m_videoDisplayWidget->setTextFontSize(m_textFontSize);
    });
    m_textSizeLongPressTimer = new QTimer(this);
    m_textSizeLongPressTimer->setSingleShot(true);
    connect(m_textSizeLongPressTimer, &QTimer::timeout, this, [this]() {
        if (!m_leftPressing) return;
        int current = m_textFontSize;
        m_textSizeSlider->setValue(current);
        QPushButton *btn = m_textButton;
        QPoint g = btn->mapToGlobal(QPoint(btn->width()/2, btn->height()));
        QPoint l = mapFromGlobal(g);
        int w = 160;
        int h = 20;
        int x = std::max(0, std::min(l.x() - w/2, this->width() - w));
        int y = std::max(0, std::min(l.y() + 4, this->height() - h));
        m_textSizeSlider->setGeometry(x, y, w, h);
        m_textSizeSlider->setStyleSheet(
            "QSlider::groove:horizontal { background: #4CAF50; height: 3px; border-radius: 2px; }"
            "QSlider::handle:horizontal { background: #1565C0; width: 8px; margin: -8px 0; border-radius: 3px; }"
        );
        m_textSizeSlider->setVisible(true);
        m_textSizeSlider->raise();
        m_textSizeDragActive = true;
        if (!m_textSizeFloatLabel) {
            m_textSizeFloatLabel = new QLabel(QString::number(m_textFontSize), this);
            m_textSizeFloatLabel->setStyleSheet("QLabel { color: #ffffff; background-color: rgba(0,0,0,160); border: 1px solid rgba(255,255,255,120); padding: 1px 4px; font-size: 12px; }");
        }
        m_textSizeFloatLabel->setText(QString::number(m_textFontSize));
        updateTextSizeFloatLabelPos();
        m_textSizeFloatLabel->setVisible(true);
        m_textSizeFloatLabel->raise();
        std::cout << "[UI] text size longpress success" << std::endl;
    });

    // 最小化按钮
    m_minimizeButton = new QPushButton("−", m_titleBar);
    m_minimizeButton->setStyleSheet(buttonStyle + 
        "QPushButton:hover { background-color: rgba(255, 193, 7, 0.7); }");  // 稍微降低透明度
    connect(m_minimizeButton, &QPushButton::clicked, this, &VideoWindow::onMinimizeClicked);
    
    // 最大化按钮
    m_maximizeButton = new QPushButton("□", m_titleBar);
    m_maximizeButton->setStyleSheet(buttonStyle + 
        "QPushButton:hover { background-color: rgba(40, 167, 69, 0.7); }");
    connect(m_maximizeButton, &QPushButton::clicked, this, &VideoWindow::onMaximizeClicked);
    
    // 关闭按钮
    m_closeButton = new QPushButton("×", m_titleBar);
    m_closeButton->setStyleSheet(buttonStyle + 
        "QPushButton:hover { background-color: rgba(220, 53, 69, 0.8); }");
    connect(m_closeButton, &QPushButton::clicked, this, &VideoWindow::onCloseClicked);
    
    m_penButton = new RainbowToolButton(m_titleBar);
    m_penButton->setIcon(QIcon(iconDir + "/pen.png"));
    m_penButton->setIconSize(QSize(16, 16));
    // m_penButton->setStyleSheet(micButtonStyle); // Use internal paintEvent
    m_penButton->setToolTip(QStringLiteral("画笔"));
    m_penButton->setCheckable(true);
    m_penButton->setChecked(false);

    m_rectButton = new RainbowToolButton(m_titleBar);
    m_rectButton->setIcon(QIcon(iconDir + "/sele_box.png"));
    m_rectButton->setIconSize(QSize(16, 16));
    // m_rectButton->setStyleSheet(micButtonStyle);
    m_rectButton->setToolTip(QStringLiteral("矩形"));
    m_rectButton->setCheckable(true);
    m_rectButton->setChecked(false);

    m_circleButton = new RainbowToolButton(m_titleBar);
    m_circleButton->setIcon(QIcon(iconDir + "/sele.png"));
    m_circleButton->setIconSize(QSize(16, 16));
    // m_circleButton->setStyleSheet(micButtonStyle);
    m_circleButton->setToolTip(QStringLiteral("圆形"));
    m_circleButton->setCheckable(true);
    m_circleButton->setChecked(false);

    m_arrowButton = new RainbowToolButton(m_titleBar);
    m_arrowButton->setIcon(QIcon(iconDir + "/arrow.png"));
    m_arrowButton->setIconSize(QSize(16, 16));
    // m_arrowButton->setStyleSheet(micButtonStyle);
    m_arrowButton->setToolTip(QStringLiteral("箭头"));
    m_arrowButton->setCheckable(true);
    m_arrowButton->setChecked(false);

    m_textButton = new RainbowToolButton(m_titleBar);
    m_textButton->setIcon(QIcon(iconDir + "/text.png"));
    m_textButton->setIconSize(QSize(16, 16));
    // m_textButton->setStyleSheet(micButtonStyle);
    m_textButton->setToolTip(QStringLiteral("文字"));
    m_textButton->installEventFilter(this);
    m_textButton->setCheckable(true);
    m_textButton->setChecked(false);

    m_eraserButton = new RainbowToolButton(m_titleBar);
    m_eraserButton->setIcon(QIcon(iconDir + "/xiangpi.png"));
    m_eraserButton->setIconSize(QSize(16, 16));
    // m_eraserButton->setStyleSheet(micButtonStyle);
    m_eraserButton->setToolTip(QStringLiteral("橡皮擦"));
    m_eraserButton->setCheckable(true);
    m_eraserButton->setChecked(false);

    m_undoButton = new QPushButton("", m_titleBar);
    m_undoButton->setIcon(QIcon(iconDir + "/z.png"));
    m_undoButton->setIconSize(QSize(16, 16));
    m_undoButton->setStyleSheet(micButtonStyle);
    m_undoButton->setToolTip(QStringLiteral("撤销"));

    m_clearButton = new QPushButton("清", m_titleBar);
    // m_clearButton->setIcon(QIcon(iconDir + "/good.png"));
    // m_clearButton->setIconSize(QSize(16, 16));
    m_clearButton->setStyleSheet(micButtonStyle);
    m_clearButton->setToolTip(QStringLiteral("清理绘制"));
    connect(m_clearButton, &QPushButton::clicked, this, [this]() {
        if (m_videoDisplayWidget) m_videoDisplayWidget->sendClear();
    });

    m_toolBar = new QWidget(m_titleBar);
    m_toolBarLayout = new QHBoxLayout(m_toolBar);
    m_toolBarLayout->setContentsMargins(0, 0, 0, 0);
    m_toolBarLayout->setSpacing(2);
    // 颜色切换放在第一个
    m_toolBarLayout->addWidget(m_colorButton);
    m_toolBarLayout->addSpacing(2);
    m_toolBarLayout->addWidget(m_penButton);
    m_toolBarLayout->addSpacing(2);
    m_toolBarLayout->addWidget(m_rectButton);
    m_toolBarLayout->addSpacing(2);
    m_toolBarLayout->addWidget(m_circleButton);
    m_toolBarLayout->addSpacing(2);
    m_toolBarLayout->addWidget(m_arrowButton);
    m_toolBarLayout->addSpacing(2);
    m_toolBarLayout->addWidget(m_textButton);
    m_toolBarLayout->addSpacing(2);
    
    m_toolBarLayout->addWidget(m_eraserButton);
    m_toolBarLayout->addSpacing(2);
    m_toolBarLayout->addWidget(m_undoButton);
    m_toolBarLayout->addSpacing(2);
    m_cameraButton = new QPushButton("", m_titleBar);
    m_cameraButton->setIcon(QIcon(iconDir + "/camera.png"));
    m_cameraButton->setIconSize(QSize(16, 16));
    m_cameraButton->setStyleSheet(micButtonStyle);
    m_cameraButton->setToolTip(QStringLiteral("截图到剪贴板"));
    m_toolBarLayout->addWidget(m_cameraButton);
    m_toolBarLayout->addSpacing(2);

    m_snippetButton = new QPushButton("", m_titleBar);
    m_snippetButton->setIcon(QIcon(iconDir + "/sel.png"));
    m_snippetButton->setIconSize(QSize(16, 16));
    m_snippetButton->setStyleSheet(micButtonStyle);
    m_snippetButton->setToolTip(QStringLiteral("框选截图"));
    connect(m_snippetButton, &QPushButton::clicked, this, [this]() {
         QScreen *screen = this->screen();
         if (screen) {
             QPixmap fullPix = screen->grabWindow(0);
             SnippetOverlay *overlay = new SnippetOverlay(fullPix);
             overlay->setGeometry(screen->geometry());
             overlay->show();
         }
    });
    m_toolBarLayout->addWidget(m_snippetButton);
    m_toolBarLayout->addSpacing(2);

    m_toolBarLayout->addWidget(m_clearButton);

    m_titleCenter = new QWidget(m_titleBar);
    m_titleCenterLayout = new QHBoxLayout(m_titleCenter);
    m_titleCenterLayout->setContentsMargins(0, 0, 0, 0);
    m_titleCenterLayout->setSpacing(0);

    m_audioCallRestoreButton = new QPushButton(m_titleCenter);
    m_audioCallRestoreButton->setText(QStringLiteral("通话中"));
    m_audioCallRestoreButton->setFixedHeight(20);
    m_audioCallRestoreButton->setMinimumWidth(58);
    m_audioCallRestoreButton->setCursor(Qt::PointingHandCursor);
    m_audioCallRestoreButton->setToolTip(QStringLiteral("通话中（点击查看通话窗口）"));
    m_audioCallRestoreButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #2e7d32;"
        "  color: rgba(255,255,255,230);"
        "  border-radius: 10px;"
        "  border: none;"
        "  padding: 0 10px;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover { background-color: #2b6f2e; }"
        "QPushButton:pressed { background-color: #245d27; }"));
    m_audioCallRestoreButton->setVisible(false);
    connect(m_audioCallRestoreButton, &QPushButton::clicked, this, &VideoWindow::audioCallRestoreClicked);

    m_titleCenterLayout->addWidget(m_audioCallRestoreButton, 0, Qt::AlignCenter);
    m_titleCenterLayout->addSpacing(14);
    m_titleCenterLayout->addWidget(m_toolBar);
    m_titleBarLayout->addWidget(m_titleCenter, 0, Qt::AlignCenter);
    m_titleBarLayout->addStretch();
    m_titleBarLayout->addWidget(m_speakerButton);
    m_titleBarLayout->addSpacing(2);
    m_titleBarLayout->addWidget(m_micButton);
    m_titleBarLayout->addSpacing(2);
    m_titleBarLayout->addWidget(m_minimizeButton);
    m_titleBarLayout->addSpacing(2);  // 按钮间添加小间距
    m_titleBarLayout->addWidget(m_maximizeButton);
    m_titleBarLayout->addSpacing(2);
    m_titleBarLayout->addWidget(m_closeButton);
    
    m_mainLayout->addWidget(m_titleBar);

    auto updateToolStyles = [this, micButtonStyle, selectedToolStyle]() {
        // RainbowToolButton handles its own styling
        // m_penButton->setStyleSheet(m_penButton->isChecked() ? selectedToolStyle : micButtonStyle);
        // m_rectButton->setStyleSheet(m_rectButton->isChecked() ? selectedToolStyle : micButtonStyle);
        // m_circleButton->setStyleSheet(m_circleButton->isChecked() ? selectedToolStyle : micButtonStyle);
        // m_textButton->setStyleSheet(m_textButton->isChecked() ? selectedToolStyle : micButtonStyle);
        // m_eraserButton->setStyleSheet(m_eraserButton->isChecked() ? selectedToolStyle : micButtonStyle);
        m_undoButton->setStyleSheet(micButtonStyle);
        m_cameraButton->setStyleSheet(micButtonStyle);
        m_snippetButton->setStyleSheet(micButtonStyle);
        m_clearButton->setStyleSheet(micButtonStyle);
        // m_arrowButton->setStyleSheet(m_arrowButton->isChecked() ? selectedToolStyle : micButtonStyle);
    };

    connect(m_penButton, &QPushButton::toggled, this, [this, updateToolStyles](bool checked) {
        if (checked) {
            QSignalBlocker b1(m_rectButton);
            QSignalBlocker b2(m_circleButton);
            QSignalBlocker b3(m_textButton);
            QSignalBlocker b4(m_eraserButton);
            QSignalBlocker b5(m_arrowButton);
            m_rectButton->setChecked(false);
            m_circleButton->setChecked(false);
            m_textButton->setChecked(false);
            m_eraserButton->setChecked(false);
            m_arrowButton->setChecked(false);
            if (m_videoDisplayWidget) { m_videoDisplayWidget->setToolMode(0); m_videoDisplayWidget->setAnnotationEnabled(true); }
        } else {
            if (m_videoDisplayWidget) { m_videoDisplayWidget->setAnnotationEnabled(false); m_videoDisplayWidget->setToolMode(0); }
        }
        if (m_textSizeSlider) m_textSizeSlider->setVisible(false);
        if (m_textSizeFloatLabel) m_textSizeFloatLabel->setVisible(false);
        updateToolStyles();
    });
    connect(m_rectButton, &QPushButton::toggled, this, [this, updateToolStyles](bool checked) {
        if (checked) {
            QSignalBlocker b1(m_penButton);
            QSignalBlocker b2(m_circleButton);
            QSignalBlocker b3(m_textButton);
            QSignalBlocker b4(m_eraserButton);
            QSignalBlocker b5(m_arrowButton);
            m_penButton->setChecked(false);
            m_circleButton->setChecked(false);
            m_textButton->setChecked(false);
            m_eraserButton->setChecked(false);
            m_arrowButton->setChecked(false);
            if (m_videoDisplayWidget) { m_videoDisplayWidget->setToolMode(2); m_videoDisplayWidget->setAnnotationEnabled(true); }
        } else {
            if (m_videoDisplayWidget) { m_videoDisplayWidget->setAnnotationEnabled(false); m_videoDisplayWidget->setToolMode(0); }
        }
        if (m_textSizeSlider) m_textSizeSlider->setVisible(false);
        if (m_textSizeFloatLabel) m_textSizeFloatLabel->setVisible(false);
        updateToolStyles();
    });
    connect(m_circleButton, &QPushButton::toggled, this, [this, updateToolStyles](bool checked) {
        if (checked) {
            QSignalBlocker b1(m_penButton);
            QSignalBlocker b2(m_rectButton);
            QSignalBlocker b3(m_arrowButton);
            QSignalBlocker b4(m_textButton);
            QSignalBlocker b5(m_eraserButton);
            m_penButton->setChecked(false);
            m_rectButton->setChecked(false);
            m_arrowButton->setChecked(false);
            m_textButton->setChecked(false);
            m_eraserButton->setChecked(false);
            if (m_videoDisplayWidget) { m_videoDisplayWidget->setToolMode(3); m_videoDisplayWidget->setAnnotationEnabled(true); }
        } else {
            if (m_videoDisplayWidget) { m_videoDisplayWidget->setAnnotationEnabled(false); m_videoDisplayWidget->setToolMode(0); }
        }
        if (m_textSizeSlider) m_textSizeSlider->setVisible(false);
        if (m_textSizeFloatLabel) m_textSizeFloatLabel->setVisible(false);
        updateToolStyles();
    });
    connect(m_arrowButton, &QPushButton::toggled, this, [this, updateToolStyles](bool checked) {
        if (checked) {
            QSignalBlocker b1(m_penButton);
            QSignalBlocker b2(m_rectButton);
            QSignalBlocker b3(m_circleButton);
            QSignalBlocker b4(m_textButton);
            QSignalBlocker b5(m_eraserButton);
            m_penButton->setChecked(false);
            m_rectButton->setChecked(false);
            m_circleButton->setChecked(false);
            m_textButton->setChecked(false);
            m_eraserButton->setChecked(false);
            if (m_videoDisplayWidget) { m_videoDisplayWidget->setToolMode(5); m_videoDisplayWidget->setAnnotationEnabled(true); }
        } else {
            if (m_videoDisplayWidget) { m_videoDisplayWidget->setAnnotationEnabled(false); m_videoDisplayWidget->setToolMode(0); }
        }
        if (m_textSizeSlider) m_textSizeSlider->setVisible(false);
        if (m_textSizeFloatLabel) m_textSizeFloatLabel->setVisible(false);
        updateToolStyles();
    });
    connect(m_textButton, &QPushButton::toggled, this, [this, updateToolStyles](bool checked) {
        if (checked) {
            QSignalBlocker b1(m_penButton);
            QSignalBlocker b2(m_rectButton);
            QSignalBlocker b3(m_circleButton);
            QSignalBlocker b4(m_eraserButton);
            QSignalBlocker b5(m_arrowButton);
            m_penButton->setChecked(false);
            m_rectButton->setChecked(false);
            m_circleButton->setChecked(false);
            m_eraserButton->setChecked(false);
            m_arrowButton->setChecked(false);
            if (m_videoDisplayWidget) { m_videoDisplayWidget->setToolMode(4); m_videoDisplayWidget->setAnnotationEnabled(true); m_videoDisplayWidget->setTextFontSize(m_textFontSize); }
        } else {
            if (m_videoDisplayWidget) { m_videoDisplayWidget->setAnnotationEnabled(false); m_videoDisplayWidget->setToolMode(0); }
            if (m_textSizeSlider) m_textSizeSlider->setVisible(false);
            if (m_textSizeFloatLabel) m_textSizeFloatLabel->setVisible(false);
        }
        updateToolStyles();
    });
    connect(m_eraserButton, &QPushButton::toggled, this, [this, updateToolStyles](bool checked) {
        if (checked) {
            QSignalBlocker b1(m_penButton);
            QSignalBlocker b2(m_rectButton);
            QSignalBlocker b3(m_circleButton);
            QSignalBlocker b4(m_textButton);
            QSignalBlocker b5(m_arrowButton);
            m_penButton->setChecked(false);
            m_rectButton->setChecked(false);
            m_circleButton->setChecked(false);
            m_textButton->setChecked(false);
            m_arrowButton->setChecked(false);
            if (m_videoDisplayWidget) { m_videoDisplayWidget->setToolMode(1); m_videoDisplayWidget->setAnnotationEnabled(true); }
        } else {
            if (m_videoDisplayWidget) { m_videoDisplayWidget->setAnnotationEnabled(false); m_videoDisplayWidget->setToolMode(0); }
        }
        if (m_textSizeFloatLabel) m_textSizeFloatLabel->setVisible(false);
        updateToolStyles();
    });
    connect(m_cameraButton, &QPushButton::clicked, this, [this]() {
        if (!m_videoDisplayWidget) return;
        QImage img = m_videoDisplayWidget->captureToImage();
        if (!img.isNull()) {
            QClipboard *cb = QGuiApplication::clipboard();
            if (cb) cb->setImage(img);
            showClipboardToast();
        }
    });
    connect(m_undoButton, &QPushButton::clicked, this, [this]() {
        if (m_videoDisplayWidget) m_videoDisplayWidget->sendUndo();
    });
}

void VideoWindow::mousePressEvent(QMouseEvent *event)
{
    const QPoint localPos = event->pos();
    const int border = 8;
    const bool inResizeBorder =
        localPos.x() <= border || localPos.x() >= width() - border ||
        localPos.y() <= border || localPos.y() >= height() - border;

    if (event->button() == Qt::LeftButton && m_titleBar->geometry().contains(localPos) && !m_isMaximized && !inResizeBorder) {
        m_dragging = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    // 右键由VideoDisplayWidget处理其上下文菜单，这里不拦截
    QWidget::mousePressEvent(event);
}

void VideoWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton && m_dragging && !m_isMaximized) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void VideoWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    }
}

void VideoWindow::paintEvent(QPaintEvent *event)
{
    // Only paint border, background handled by Acrylic
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(60, 60, 60), 1));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
    
    QWidget::paintEvent(event);
}

bool VideoWindow::event(QEvent *event)
{
#ifdef _WIN32
    if (event && (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange)) {
        QTimer::singleShot(0, this, [this]() {
            HWND hwnd = reinterpret_cast<HWND>(winId());
            setAttribute(Qt::WA_TranslucentBackground, true);
            setAttribute(Qt::WA_NoSystemBackground, true);
            setAutoFillBackground(false);
            tryExtendGlassFrame(hwnd);
            ensureLayeredForAcrylic(hwnd);
            if (!tryEnableAcrylicBlur(hwnd, kAcrylicTintAbgr)) {
                if (!tryEnableDwmBackdropSimple(hwnd, 3, true)) {
                    SetLayeredWindowAttributes(hwnd, 0, 235, LWA_ALPHA);
                }
            }
            applyHwndCornerStyle(hwnd, !(windowState() & Qt::WindowMaximized));
        });
    }
#endif
    return QWidget::event(event);
}

void VideoWindow::onMinimizeClicked()
{
    showMinimized();
}

// 原生事件拦截：确保物理右键（即便系统交换左右键）仍触发清屏
bool VideoWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType);
    MSG *msg = static_cast<MSG*>(message);
    if (!msg) {
        return QWidget::nativeEvent(eventType, message, result);
    }

    if (msg->message == WM_NCHITTEST) {
        if (m_isMaximized || isFullScreen()) {
            return QWidget::nativeEvent(eventType, message, result);
        }

        const POINTS pts = MAKEPOINTS(msg->lParam);
        const QPoint globalPos(pts.x, pts.y);
        const QPoint localPos = mapFromGlobal(globalPos);

        const int border = 8;
        const bool left = localPos.x() >= 0 && localPos.x() <= border;
        const bool right = localPos.x() >= width() - border && localPos.x() <= width();
        const bool top = localPos.y() >= 0 && localPos.y() <= border;
        const bool bottom = localPos.y() >= height() - border && localPos.y() <= height();

        if (top && left) { *result = HTTOPLEFT; return true; }
        if (top && right) { *result = HTTOPRIGHT; return true; }
        if (bottom && left) { *result = HTBOTTOMLEFT; return true; }
        if (bottom && right) { *result = HTBOTTOMRIGHT; return true; }
        if (left) { *result = HTLEFT; return true; }
        if (right) { *result = HTRIGHT; return true; }
        if (top) { *result = HTTOP; return true; }
        if (bottom) { *result = HTBOTTOM; return true; }
    }

    return QWidget::nativeEvent(eventType, message, result);
}

bool VideoWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_speakerButton || obj == m_micButton) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_leftPressing = true;
                m_volumeTargetMic = (obj == m_micButton);
                m_volumeDragStartPos = me->globalPosition().toPoint();
                m_volumeDragStartValue = m_volumeSlider->value();
                m_volumeLongPressTimer->start(500);
                return false;
            } else if (me->button() == Qt::RightButton) {
                if (obj == m_speakerButton) {
                    return true;
                }
                if (obj == m_micButton) {
                    QMenu menu;
                    QAction *follow = menu.addAction(QStringLiteral("跟随系统"));
                    follow->setCheckable(true);
                    bool fs = m_videoDisplayWidget ? m_videoDisplayWidget->isMicInputFollowSystem() : true;
                    QString curr = m_videoDisplayWidget ? m_videoDisplayWidget->currentMicInputDeviceId() : QString();
                    follow->setChecked(fs);
                    const auto ins = QMediaDevices::audioInputs();
                    for (const auto &d : ins) {
                        QAction *a = menu.addAction(d.description());
                        a->setCheckable(true);
                        if (!fs && !curr.isEmpty() && d.id() == curr) a->setChecked(true);
                        a->setData(d.id());
                    }
                    QAction *chosen = menu.exec(me->globalPosition().toPoint());
                    if (!chosen) return true;
                    if (chosen == follow) {
                        if (m_videoDisplayWidget) m_videoDisplayWidget->selectMicInputFollowSystem();
                    } else {
                        QString id = chosen->data().toString();
                        if (m_videoDisplayWidget) m_videoDisplayWidget->selectMicInputById(id);
                    }
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseMove) {
            QMouseEvent *mm = static_cast<QMouseEvent*>(event);
            if (m_volumeDragActive && (mm->buttons() & Qt::LeftButton)) {
                int dx = mm->globalPosition().toPoint().x() - m_volumeDragStartPos.x();
                int v = m_volumeDragStartValue + dx / 2;
                if (v < 0) v = 0;
                if (v > 100) v = 100;
                m_volumeSlider->setValue(v);
                if (m_videoDisplayWidget) { if (m_volumeTargetMic) { m_videoDisplayWidget->setMicGainPercent(v); } else { m_videoDisplayWidget->setVolumePercent(v); } }
                return true;
            }
            return false;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *mr = static_cast<QMouseEvent*>(event);
            if (mr->button() == Qt::LeftButton) {
                m_leftPressing = false;
                if (m_volumeLongPressTimer->isActive()) {
                    m_volumeLongPressTimer->stop();
                }
                if (m_volumeDragActive) {
                    m_volumeDragActive = false;
                    if (m_volumeTargetMic) { std::cout << "[UI] mic set final=" << m_volumeSlider->value() << std::endl; }
                    else { std::cout << "[UI] volume set final=" << m_volumeSlider->value() << std::endl; }
                    QTimer::singleShot(1000, this, [this](){ if (m_volumeSlider) m_volumeSlider->setVisible(false); });
                    return true;
                }
                return false;
            }
        }
    }
    if (obj == m_textButton) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_leftPressing = true;
                m_textSizeDragStartPos = me->globalPosition().toPoint();
                m_textSizeDragStartValue = m_textFontSize;
                if (m_textSizeLongPressTimer) m_textSizeLongPressTimer->start(500);
                return false;
            }
        } else if (event->type() == QEvent::MouseMove) {
            QMouseEvent *mm = static_cast<QMouseEvent*>(event);
            if (m_textSizeDragActive && (mm->buttons() & Qt::LeftButton)) {
                int dx = mm->globalPosition().toPoint().x() - m_textSizeDragStartPos.x();
                int v = m_textSizeDragStartValue + dx / 2;
                if (v < 8) v = 8;
                if (v > 72) v = 72;
                if (m_textSizeSlider) m_textSizeSlider->setValue(v);
                m_textFontSize = v;
                if (m_textSizeFloatLabel) m_textSizeFloatLabel->setText(QString::number(m_textFontSize));
                updateTextSizeFloatLabelPos();
                if (m_videoDisplayWidget) m_videoDisplayWidget->setTextFontSize(m_textFontSize);
                return true;
            }
            return false;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *mr = static_cast<QMouseEvent*>(event);
            if (mr->button() == Qt::LeftButton) {
                m_leftPressing = false;
                if (m_textSizeLongPressTimer && m_textSizeLongPressTimer->isActive()) {
                    m_textSizeLongPressTimer->stop();
                }
                if (m_textSizeDragActive) {
                    m_textSizeDragActive = false;
                    QTimer::singleShot(800, this, [this](){ if (m_textSizeSlider) m_textSizeSlider->setVisible(false); if (m_textSizeFloatLabel) m_textSizeFloatLabel->setVisible(false); });
                    return true;
                }
                return false;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void VideoWindow::updateTextSizeFloatLabelPos()
{
    if (!m_textSizeSlider || !m_textSizeFloatLabel) return;
    QRect r = m_textSizeSlider->geometry();
    int min = m_textSizeSlider->minimum();
    int max = m_textSizeSlider->maximum();
    int v = m_textSizeSlider->value();
    double t = (max == min) ? 0.0 : (double)(v - min) / (double)(max - min);
    int w = m_textSizeFloatLabel->sizeHint().width();
    int h = m_textSizeFloatLabel->sizeHint().height();
    int x = r.x() + (int)std::round(t * r.width()) - w / 2;
    if (x < r.x()) x = r.x();
    if (x > r.x() + r.width() - w) x = r.x() + r.width() - w;
    int y = r.y() + r.height() + 2;
    m_textSizeFloatLabel->setGeometry(x, y, w, h);
}

void VideoWindow::onMaximizeClicked()
{
    if (m_isMaximized) {
        // 恢复窗口
        setGeometry(m_normalGeometry);
        m_maximizeButton->setText("□");
        m_isMaximized = false;
    } else {
        // 最大化窗口
        m_normalGeometry = geometry();
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            setGeometry(screen->availableGeometry());
        }
        m_maximizeButton->setText("❐");
        m_isMaximized = true;
    }
}

void VideoWindow::onCloseClicked()
{
    if (m_videoDisplayWidget) {
        m_videoDisplayWidget->stopReceiving(false);
    }
    emit closeClicked();
    hide();
}

void VideoWindow::onMicToggled(bool checked)
{
    m_micButton->setIcon(checked ? m_micIconOn : m_micIconOff);
    m_micButton->setToolTip(checked ? QStringLiteral("麦克风：开") : QStringLiteral("麦克风：关"));
    if (m_videoDisplayWidget) {
        // m_videoDisplayWidget->sendAudioToggle(checked);
        m_videoDisplayWidget->setTalkEnabled(checked);
    }
    const QString configPath = AppConfig::configFilePathInAppDir();
    QFile f(configPath);
    QStringList lines;
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) { QTextStream in(&f); while (!in.atEnd()) lines << in.readLine(); f.close(); }
    bool rep = false; for (int i = 0; i < lines.size(); ++i) { if (lines[i].startsWith("mic_enabled=")) { lines[i] = QString("mic_enabled=%1").arg(checked ? "true" : "false"); rep = true; break; } }
    if (!rep) lines << QString("mic_enabled=%1").arg(checked ? "true" : "false");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) { QTextStream out(&f); for (const QString &ln : lines) out << ln << "\n"; f.close(); }
    emit micToggled(checked);
}

void VideoWindow::onSpeakerToggled(bool checked)
{
    // 本地播放控制（不影响推流端）
    m_speakerButton->setIcon(checked ? m_speakerIconOn : m_speakerIconOff);
    m_speakerButton->setToolTip(checked ? QStringLiteral("扬声器：开") : QStringLiteral("扬声器：关"));
    if (m_videoDisplayWidget) {
        m_videoDisplayWidget->setSpeakerEnabled(checked);
    }
    const QString configPath = AppConfig::configFilePathInAppDir();
    QFile f(configPath);
    QStringList lines;
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) { QTextStream in(&f); while (!in.atEnd()) lines << in.readLine(); f.close(); }
    bool rep = false; for (int i = 0; i < lines.size(); ++i) { if (lines[i].startsWith("speaker_enabled=")) { lines[i] = QString("speaker_enabled=%1").arg(checked ? "true" : "false"); rep = true; break; } }
    if (!rep) lines << QString("speaker_enabled=%1").arg(checked ? "true" : "false");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) { QTextStream out(&f); for (const QString &ln : lines) out << ln << "\n"; f.close(); }
    emit speakerToggled(checked);
}

void VideoWindow::onColorClicked()
{
    int current = m_videoDisplayWidget ? m_videoDisplayWidget->annotationColorId() : 0;
    int next = (current + 1) % 4; // 0:红,1:绿,2:蓝,3:黄
    if (m_videoDisplayWidget) {
        m_videoDisplayWidget->setAnnotationColorId(next);
    }
    updateColorButtonVisual(next);
}

void VideoWindow::updateColorButtonVisual(int colorId)
{
    QString tip;
    switch (colorId) {
    case 0: tip = QStringLiteral("批注颜色：红"); break;
    case 1: tip = QStringLiteral("批注颜色：绿"); break;
    case 2: tip = QStringLiteral("批注颜色：蓝"); break;
    default: tip = QStringLiteral("批注颜色：黄"); break;
    }
    if (m_colorButton) {
        m_colorButton->setColorId(colorId);
        m_colorButton->setToolTip(tip);
    }
}

void VideoWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
        if (m_titleBar) m_titleBar->show();
        detachToolbarToTitleBar();
        if (m_fullscreenBar) m_fullscreenBar->setVisible(false);
    } else {
        // 记录当前几何以便恢复
        m_normalGeometry = geometry();
        showFullScreen();
        if (m_titleBar) m_titleBar->hide();
        attachToolbarToFullscreenBar();
        if (m_fullscreenBar) {
            m_fullscreenBar->setGeometry(0, 0, width(), m_fullscreenBar->height());
            m_fullscreenBar->raise();
            m_fullscreenBar->setVisible(true);
        }
    }
}


void VideoWindow::showClipboardToast()
{
    if (!m_videoDisplayWidget) return;
    if (!m_clipToast) {
        m_clipToast = new QLabel(m_videoDisplayWidget);
        m_clipToast->setStyleSheet("QLabel { color: #ffffff; background-color: rgba(0,0,0,160); border: 1px solid rgba(255,255,255,100); padding: 6px 12px; border-radius: 6px; }");
        m_clipToast->setAlignment(Qt::AlignCenter);
    }
    m_clipToast->setText(QStringLiteral("已存入粘贴板！"));
    QRect area = m_videoDisplayWidget->rect();
    QSize sz = m_clipToast->sizeHint();
    int x = area.width()/2 - sz.width()/2;
    int y = area.height() - sz.height() - 24;
    m_clipToast->setGeometry(x, y, sz.width(), sz.height());
    auto effect = new QGraphicsOpacityEffect(m_clipToast);
    m_clipToast->setGraphicsEffect(effect);
    m_clipToast->setVisible(true);
    m_clipToast->raise();
    QTimer::singleShot(1200, this, [this, effect]() {
        auto anim = new QPropertyAnimation(effect, "opacity", m_clipToast);
        anim->setDuration(800);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        QObject::connect(anim, &QPropertyAnimation::finished, this, [this, effect]() { if (m_clipToast) m_clipToast->setVisible(false); effect->deleteLater(); });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    });
}
void VideoWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_fullscreenBar && m_fullscreenBar->isVisible()) {
        m_fullscreenBar->setGeometry(0, 0, width(), m_fullscreenBar->height());
        m_fullscreenBar->raise();
    }
}

void VideoWindow::attachToolbarToFullscreenBar()
{
    if (!m_toolBar || !m_fullscreenPillLayout) return;
    // 从标题栏中心容器移除工具栏
    if (m_titleCenterLayout) {
        int cidx = m_titleCenterLayout->indexOf(m_toolBar);
        if (cidx >= 0) { QLayoutItem *cit = m_titleCenterLayout->takeAt(cidx); if (cit) delete cit; }
    }
    // 同时移除扬声器和麦克按钮
    if (m_titleBarLayout) {
        int sidx = m_titleBarLayout->indexOf(m_speakerButton);
        if (sidx >= 0) { QLayoutItem *it = m_titleBarLayout->takeAt(sidx); if (it) delete it; }
        int midx = m_titleBarLayout->indexOf(m_micButton);
        if (midx >= 0) { QLayoutItem *it = m_titleBarLayout->takeAt(midx); if (it) delete it; }
    }
    // 归属到全屏 pill
    m_toolBar->setParent(m_fullscreenPill);
    m_fullscreenPillLayout->addWidget(m_toolBar);
    m_fullscreenPillLayout->addSpacing(8);
    m_speakerButton->setParent(m_fullscreenPill);
    m_fullscreenPillLayout->addWidget(m_speakerButton);
    m_fullscreenPillLayout->addSpacing(4);
    m_micButton->setParent(m_fullscreenPill);
    m_fullscreenPillLayout->addWidget(m_micButton);
}

void VideoWindow::detachToolbarToTitleBar()
{
    if (!m_toolBar || !m_titleBarLayout) return;
    if (m_fullscreenPillLayout) {
        int idx = m_fullscreenPillLayout->indexOf(m_toolBar);
        if (idx >= 0) { QLayoutItem *it = m_fullscreenPillLayout->takeAt(idx); if (it) delete it; }
        int sidx = m_fullscreenPillLayout->indexOf(m_speakerButton);
        if (sidx >= 0) { QLayoutItem *it = m_fullscreenPillLayout->takeAt(sidx); if (it) delete it; }
        int midx = m_fullscreenPillLayout->indexOf(m_micButton);
        if (midx >= 0) { QLayoutItem *it = m_fullscreenPillLayout->takeAt(midx); if (it) delete it; }
    }
    m_toolBar->setParent(m_titleCenter);
    if (m_titleCenterLayout) m_titleCenterLayout->addWidget(m_toolBar);
    // 把扬声器/麦克恢复到最小化按钮之前
    int indexMin = m_titleBarLayout->indexOf(m_minimizeButton);
    if (indexMin < 0) indexMin = m_titleBarLayout->count();
    m_micButton->setParent(m_titleBar);
    m_speakerButton->setParent(m_titleBar);
    m_titleBarLayout->insertSpacing(indexMin, 2);
    m_titleBarLayout->insertWidget(indexMin, m_micButton);
    m_titleBarLayout->insertSpacing(indexMin, 2);
    m_titleBarLayout->insertWidget(indexMin, m_speakerButton);
}
#include <QtMultimedia/QMediaDevices>
#include <QtMultimedia/QAudioDevice>
