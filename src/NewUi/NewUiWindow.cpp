#include "NewUiWindow.h"
#include "NewUserGuide.h"
#include "InviteUsersDialog.h"
#include <QMessageBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QScrollArea>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QSaveFile>
#include <QStringList>
#include "../common/AppConfig.h"
#include <QDesktopServices>
#include <QPointer>
#include <QCursor>
#include <QFrame>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QGraphicsDropShadowEffect>
#include <QStylePainter>
#include <QStyleOptionButton>
#include <QMenu>
#include <QAction>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QRubberBand>
#include <QRandomGenerator>
#include "LocalActivityMonitor.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QBuffer>
#include <QImage>
#include <QStackedWidget>
#include <QUrl>
#include <QClipboard>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebChannel>
#include <QSignalBlocker>
#include <QAbstractButton>

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#endif
#include <QDialog>
#include <QLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QUrlQuery>
#include <QtGlobal>
#include <climits>
#include <QAbstractButton>
#include "../video_components/VideoDisplayWidget.h"
#include "../ui/AnnotationToolbar.h"
#include "../ui/SnippetOverlay.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif
#include "../ui/BroadcastNoticeDialog.h"
#include "../common/AutoUpdater.h"
#include <QProgressDialog>

namespace {

QString webOriginForUrl(const QUrl &url)
{
    if (!url.isValid()) return QString();
    const QString scheme = url.scheme().toLower();
    const QString host = url.host().toLower();
    if (scheme.isEmpty() || host.isEmpty()) return QString();
    const int port = url.port();
    if (port <= 0) return scheme + QStringLiteral("://") + host;
    return scheme + QStringLiteral("://") + host + QStringLiteral(":") + QString::number(port);
}

QString webCredConfigKeyForOrigin(const QString &origin)
{
    const QByteArray md5 = QCryptographicHash::hash(origin.toUtf8(), QCryptographicHash::Md5).toHex();
    return QStringLiteral("webcred_") + QString::fromLatin1(md5);
}

bool writeConfigValueToAppDir(const QString &key, const QString &value)
{
    const QString configFilePath = AppConfig::configFilePathInAppDir();
    QFile f(configFilePath);
    QStringList lines;
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        in.setEncoding(QStringConverter::Utf8);
#else
        in.setCodec("UTF-8");
#endif
        while (!in.atEnd()) {
            lines << in.readLine();
        }
        f.close();
    }

    const QString prefix = key + QStringLiteral("=");
    bool replaced = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines[i].trimmed();
        if (trimmed.startsWith(prefix)) {
            lines[i] = prefix + value;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        lines << (prefix + value);
    }

    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec("UTF-8");
#endif
    for (const QString &line : lines) {
        out << line << "\n";
    }
    f.close();
    return true;
}

QByteArray dpapiProtect(const QByteArray &plain)
{
#ifdef _WIN32
    if (plain.isEmpty()) return QByteArray();
    DATA_BLOB inBlob{};
    inBlob.cbData = static_cast<DWORD>(plain.size());
    inBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()));

    DATA_BLOB outBlob{};
    if (!CryptProtectData(&inBlob, L"", nullptr, nullptr, nullptr, 0, &outBlob)) {
        return QByteArray();
    }

    QByteArray protectedBytes(reinterpret_cast<const char *>(outBlob.pbData), static_cast<int>(outBlob.cbData));
    LocalFree(outBlob.pbData);
    return protectedBytes;
#else
    Q_UNUSED(plain);
    return QByteArray();
#endif
}

QByteArray dpapiUnprotect(const QByteArray &protectedBytes)
{
#ifdef _WIN32
    if (protectedBytes.isEmpty()) return QByteArray();
    DATA_BLOB inBlob{};
    inBlob.cbData = static_cast<DWORD>(protectedBytes.size());
    inBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(protectedBytes.constData()));

    DATA_BLOB outBlob{};
    if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, 0, &outBlob)) {
        return QByteArray();
    }

    QByteArray plain(reinterpret_cast<const char *>(outBlob.pbData), static_cast<int>(outBlob.cbData));
    LocalFree(outBlob.pbData);
    return plain;
#else
    Q_UNUSED(protectedBytes);
    return QByteArray();
#endif
}

bool saveWebCredential(const QString &origin, const QString &username, const QString &password)
{
    if (origin.isEmpty()) return false;
    if (password.isEmpty()) return false;
    if (password.size() > 2048) return false;
    if (username.size() > 1024) return false;

    QByteArray payload;
    payload.append(username.toUtf8());
    payload.append('\n');
    payload.append(password.toUtf8());
    const QByteArray protectedBytes = dpapiProtect(payload);
    if (protectedBytes.isEmpty()) return false;

    const QString key = webCredConfigKeyForOrigin(origin);
    const QString value = QString::fromLatin1(protectedBytes.toBase64());
    return writeConfigValueToAppDir(key, value);
}

bool loadWebCredential(const QString &origin, QString &outUsername, QString &outPassword)
{
    outUsername.clear();
    outPassword.clear();
    if (origin.isEmpty()) return false;

    const QString key = webCredConfigKeyForOrigin(origin);
    const QString b64 = AppConfig::readConfigValue(key).trimmed();
    if (b64.isEmpty()) return false;

    const QByteArray protectedBytes = QByteArray::fromBase64(b64.toLatin1());
    const QByteArray plain = dpapiUnprotect(protectedBytes);
    if (plain.isEmpty()) return false;

    const int nl = plain.indexOf('\n');
    if (nl < 0) return false;
    outUsername = QString::fromUtf8(plain.left(nl));
    outPassword = QString::fromUtf8(plain.mid(nl + 1));
    return !outPassword.isEmpty();
}

QString jsQuotedString(const QString &value)
{
    QJsonArray a;
    a.append(value);
    const QString json = QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
    return json.mid(1, json.size() - 2);
}

class IrulerWebBridge final : public QObject
{
    Q_OBJECT
public:
    explicit IrulerWebBridge(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void saveWebCredential(const QString &origin, const QString &username, const QString &password)
    {
        ::saveWebCredential(origin.trimmed(), username, password);
    }

    Q_INVOKABLE QString getWebCredential(const QString &origin)
    {
        QString u;
        QString p;
        const bool ok = loadWebCredential(origin.trimmed(), u, p);
        QJsonObject obj;
        obj.insert(QStringLiteral("ok"), ok);
        if (ok) {
            obj.insert(QStringLiteral("u"), u);
            obj.insert(QStringLiteral("p"), p);
        }
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }
};

void ensureWebChannelBridge(QWebEngineView *view)
{
    if (!view || !view->page()) return;
    QWebEnginePage *page = view->page();
    if (page->property("_iruler_wc_ready").toBool()) return;

    auto *bridge = new IrulerWebBridge(page);
    auto *channel = new QWebChannel(page);
    channel->registerObject(QStringLiteral("irulerBridge"), bridge);
    page->setWebChannel(channel);
    page->setProperty("_iruler_wc_ready", true);
}

void injectWebCredentialAndAutofill(QWebEngineView *view)
{
    if (!view || !view->page()) return;
    ensureWebChannelBridge(view);

    const QString js = QStringLiteral(
        "(function(){"
        "if(window.__irulerWcHooked)return;"
        "window.__irulerWcHooked=true;"
        "function isVisible(el){"
        "try{var r=el.getBoundingClientRect();return r&&r.width>0&&r.height>0;}catch(e){return false;}"
        "}"
        "function setVal(el,val){"
        "if(!el)return;"
        "try{"
        "var proto=Object.getPrototypeOf(el);"
        "var desc=Object.getOwnPropertyDescriptor(proto,'value');"
        "if(!desc||!desc.set)desc=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value');"
        "if(desc&&desc.set){desc.set.call(el,val);}else{el.value=val;}"
        "try{el.setAttribute('value',val);}catch(e){}"
        "el.dispatchEvent(new Event('input',{bubbles:true}));"
        "el.dispatchEvent(new Event('change',{bubbles:true}));"
        "el.dispatchEvent(new Event('keyup',{bubbles:true}));"
        "}catch(e){}"
        "}"
        "function pickPwd(scope){"
        "var pwds=Array.prototype.slice.call((scope||document).querySelectorAll('input[type=password]'));"
        "for(var i=0;i<pwds.length;i++){if(isVisible(pwds[i]))return pwds[i];}"
        "return null;"
        "}"
        "function pickUser(pwd){"
        "var form=pwd&&pwd.form?pwd.form:null;"
        "var scope=form||document;"
        "var inputs=Array.prototype.slice.call(scope.querySelectorAll('input'));"
        "var best=null;"
        "for(var i=0;i<inputs.length;i++){"
        "var el=inputs[i];"
        "if(!el||el===pwd)continue;"
        "var t=(el.type||'').toLowerCase();"
        "if(el.autocomplete==='username'||el.autocomplete==='email')best=el;"
        "if(t==='email')best=el;"
        "if(t==='text'||t==='email'||t==='tel'||t==='number'||t==='search')best=el;"
        "}"
        "if(best&&isVisible(best))return best;"
        "if(pwd){"
        "var idx=inputs.indexOf(pwd);"
        "if(idx>0){"
        "for(var k=idx-1;k>=0;k--){"
        "var e=inputs[k];"
        "if(!e)continue;"
        "var tt=(e.type||'').toLowerCase();"
        "if(tt==='text'||tt==='email'||tt==='tel'||tt==='number'||tt==='search'){if(isVisible(e))return e;break;}"
        "}"
        "}"
        "}"
        "return null;"
        "}"
        "function attach(form){"
        "if(!form||form.__irulerHooked)return;"
        "form.__irulerHooked=true;"
        "form.addEventListener('submit',function(){"
        "try{"
        "var pwd=pickPwd(form);"
        "if(!pwd||!pwd.value)return;"
        "var user=pickUser(pwd);"
        "if(window.irulerBridge&&window.irulerBridge.saveWebCredential){"
        "window.irulerBridge.saveWebCredential(location.origin,(user?user.value:''),pwd.value||'');"
        "}"
        "}catch(e){}"
        "},true);"
        "}"
        "function attachAll(){"
        "try{"
        "var forms=Array.prototype.slice.call(document.forms||[]);"
        "forms.forEach(attach);"
        "}catch(e){}"
        "}"
        "function tryAutofill(){"
        "try{"
        "if(!(window.irulerBridge&&window.irulerBridge.getWebCredential))return;"
        "window.irulerBridge.getWebCredential(location.origin,function(json){"
        "try{"
        "if(!json)return;"
        "var obj=null;try{obj=JSON.parse(json);}catch(e){obj=null;}"
        "if(!obj||!obj.ok)return;"
        "var pwd=pickPwd(document);"
        "if(!pwd)return;"
        "if(pwd.value&&pwd.value.length>0)return;"
        "var user=pickUser(pwd);"
        "setVal(user,obj.u||'');"
        "setVal(pwd,obj.p||'');"
        "}catch(e){}"
        "});"
        "}catch(e){}"
        "}"
        "function setupBridge(){"
        "attachAll();"
        "tryAutofill();"
        "var mo=new MutationObserver(function(){attachAll();});"
        "mo.observe(document.documentElement,{childList:true,subtree:true});"
        "}"
        "function ensureQWebChannel(cb){"
        "if(typeof qt==='undefined'||!qt.webChannelTransport){setTimeout(function(){ensureQWebChannel(cb);},50);return;}"
        "if(typeof QWebChannel!=='undefined'){cb();return;}"
        "var s=document.createElement('script');"
        "s.src='qrc:///qtwebchannel/qwebchannel.js';"
        "s.onload=function(){cb();};"
        "(document.head||document.documentElement).appendChild(s);"
        "}"
        "ensureQWebChannel(function(){"
        "new QWebChannel(qt.webChannelTransport,function(channel){"
        "window.irulerBridge=channel.objects.irulerBridge;"
        "setupBridge();"
        "});"
        "});"
        "})();"
    );

    view->page()->runJavaScript(js);
}

QWebEngineProfile *ensureWebEngineProfileConfigured()
{
    static bool configured = false;
    QWebEngineProfile *profile = QWebEngineProfile::defaultProfile();
    if (configured) return profile;
    configured = true;

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString base = appData.isEmpty()
                             ? (QCoreApplication::applicationDirPath() + QStringLiteral("/config/webengine"))
                             : (appData + QStringLiteral("/webengine"));

    QDir().mkpath(base);
    profile->setPersistentStoragePath(base + QStringLiteral("/storage"));
    profile->setCachePath(base + QStringLiteral("/cache"));
    profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
    return profile;
}

#ifdef _WIN32
struct ACCENT_POLICY {
    int AccentState;
    int AccentFlags;
    int GradientColor;
    int AnimationId;
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    int Attrib;
    PVOID pvData;
    SIZE_T cbData;
};

enum WINDOWCOMPOSITIONATTRIB {
    WCA_ACCENT_POLICY = 19
};

enum ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI *)(HWND, WINDOWCOMPOSITIONATTRIBDATA *);
using RtlGetVersionFn = LONG (WINAPI *)(OSVERSIONINFOW*);

static DWORD getWindowsBuildNumber() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return 0;
    auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (!fn) return 0;

    OSVERSIONINFOW rovi = { 0 };
    rovi.dwOSVersionInfoSize = sizeof(rovi);
    if (fn(&rovi) == 0) { // STATUS_SUCCESS
        return rovi.dwBuildNumber;
    }
    return 0;
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

    DWORD build = getWindowsBuildNumber();

    // Unified logic: Win10 (1803+) and Win11 use Acrylic with the provided color
    // This allows testing Win11 visual style on Win10
    if (build >= 17134) {
        // [User Request] Force Acrylic (4) on Win10 to achieve "more blurry" effect
        // Standard Blur (3) has a fixed small radius. Acrylic provides a stronger blur.
        policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;

        policy.AccentFlags = 0;
        policy.GradientColor = abgrGradientColor;
        if (fn(hwnd, &data)) {
            return true;
        }
    }

    // Fallback (Older Win10 or if Acrylic fails)
    policy.AccentState = ACCENT_ENABLE_BLURBEHIND;
    policy.AccentFlags = 0;
    policy.GradientColor = 0x00FFFFFF; // Transparent tint to avoid black background

    return fn(hwnd, &data) != FALSE;
}

static void disableAcrylic(HWND hwnd)
{
    if (!hwnd) return;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    auto fn = reinterpret_cast<SetWindowCompositionAttributeFn>(GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!fn) return;

    ACCENT_POLICY policy{};
    WINDOWCOMPOSITIONATTRIBDATA data{};
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &policy;
    data.cbData = sizeof(policy);

    // Use standard blur or disabled. Standard blur (ACCENT_ENABLE_BLURBEHIND) is faster.
    policy.AccentState = ACCENT_ENABLE_BLURBEHIND;
    policy.AccentFlags = 0;
    policy.GradientColor = 0xE6000000;

    fn(hwnd, &data);
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
    // Removed +1 to ensure correct clipping inside the window rect
    HRGN rgn = CreateRoundRectRgn(0, 0, w, h, d, d);
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

static bool applyAcrylicForWidget(QWidget *w, int abgrGradientColor)
{
    if (!w) return false;
    w->setAttribute(Qt::WA_NativeWindow, true);
    w->setAttribute(Qt::WA_TranslucentBackground, true);
    w->setAutoFillBackground(false);
    HWND hwnd = reinterpret_cast<HWND>(w->winId());
    if (!hwnd) return false;
    if (tryEnableDwmBackdropSimple(hwnd, 3, true)) {
        return true;
    }
    return tryEnableAcrylicBlur(hwnd, abgrGradientColor);
}

static bool tryEnableDwmBackdrop(HWND hwnd)
{
    if (!hwnd) return false;
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (!dwmapi) return false;
    auto extendFrame = reinterpret_cast<DwmExtendFrameIntoClientAreaFn>(GetProcAddress(dwmapi, "DwmExtendFrameIntoClientArea"));
    auto setAttr = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
    if (!setAttr) {
        FreeLibrary(dwmapi);
        return false;
    }

    const int useDarkMode = 1;
    setAttr(hwnd, 20, &useDarkMode, sizeof(useDarkMode));

    const int cornerPreference = 2;
    setAttr(hwnd, 33, &cornerPreference, sizeof(cornerPreference));

    const int backdropType = 3;
    HRESULT hrBackdrop = setAttr(hwnd, 38, &backdropType, sizeof(backdropType));

    if (extendFrame) {
        const DwmMargins margins{0, 0, 0, 0};
        extendFrame(hwnd, &margins);
    }

    FreeLibrary(dwmapi);
    return SUCCEEDED(hrBackdrop);
}
#endif

} // namespace

// [Standard Approach] Custom Button for High-Performance Visual Feedback
// Overrides paintEvent to scale icon when pressed, ensuring instant response.
class ResponsiveButton : public QPushButton {
public:
    using QPushButton::QPushButton; // Use base constructors

protected:
    void paintEvent(QPaintEvent *event) override {
        QStylePainter p(this);
        QStyleOptionButton option;
        initStyleOption(&option);

        if (isDown()) {
            // Scale down icon size by 15% when pressed
            QSize originalSize = option.iconSize;
            option.iconSize = originalSize * 0.85; 
        }

        p.drawControl(QStyle::CE_PushButton, option);
    }
};

class StoryboardWebPage : public QWebEnginePage {
public:
    explicit StoryboardWebPage(QWebEngineProfile *profile, QObject *parent, QWebEngineView *view)
        : QWebEnginePage(profile, parent), m_view(view)
    {
    }

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override
    {
        Q_UNUSED(type);
        Q_UNUSED(isMainFrame);
        if (url.scheme().compare(QStringLiteral("iruler"), Qt::CaseInsensitive) == 0) {
            if (url.host().compare(QStringLiteral("savewebcred"), Qt::CaseInsensitive) == 0) {
                const QUrlQuery q(url);
                const QString origin = q.queryItemValue(QStringLiteral("origin")).trimmed();
                const QString u = q.queryItemValue(QStringLiteral("u"));
                const QString p = q.queryItemValue(QStringLiteral("p"));
                saveWebCredential(origin, u, p);
            }
            return false;
        }
        return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
    }

private:
    QPointer<QWebEngineView> m_view;
};

NewUiWindow::NewUiWindow(QWidget *parent)
    : QWidget(parent)
{
    // Ensure object name is set for stylesheet selectors
    setObjectName("NewUiWindow");

    // --- GLOBAL SIZE CONTROL (ONE VALUE TO RULE THEM ALL) ---
    // [User Setting] 只要修改这个数值，所有尺寸自动计算
    m_cardBaseWidth = 300; // 卡片可见区域的宽度 (Changed to 300 as requested)
    
    // [Advanced Setting] 底部按钮区域的高度
    m_bottomAreaHeight = 45; 
    
    // --- Automatic Calculations (Do not modify) ---
    m_shadowSize = 5; // Shadow margin
    m_aspectRatio = 16.0 / 9.0;
    
    // 1. Visible Card Height = (Width / 1.77) + Bottom Area
    m_cardBaseHeight = (int)(m_cardBaseWidth / m_aspectRatio) + m_bottomAreaHeight;
    
    // 2. Total Item Size (including shadow)
    m_totalItemWidth = m_cardBaseWidth + (2 * m_shadowSize);
    m_totalItemHeight = m_cardBaseHeight + (2 * m_shadowSize);
    
    // 3. Image Dimensions
    // Width = Card Width * 0.94 (3% margin on each side)
    m_imgWidth = (int)(m_cardBaseWidth * 0.94);
    // Height = Width / 1.77
    m_imgHeight = (int)(m_imgWidth / m_aspectRatio);
    
    // 4. Internal Margins (To center the image and create the border)
    m_marginX = (m_cardBaseWidth - m_imgWidth) / 2;
    // Vertical centering in the top area: (TopAreaHeight - ImageHeight) / 2
    m_topAreaHeight = m_cardBaseHeight - m_bottomAreaHeight;
    m_marginTop = (m_topAreaHeight - m_imgHeight) / 2;

    // Remove Qt::FramelessWindowHint to allow native Windows behaviors (Snap, Maximize animation)
    // We handle WM_NCCALCSIZE to hide the standard frame visually
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);

#ifdef _WIN32
    DWORD build = getWindowsBuildNumber();
    if (build >= 22000) {
        // Windows 11
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
    } else {
        // Windows 10
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAttribute(Qt::WA_NoSystemBackground, false);
        setAutoFillBackground(true);
    }
#else
    // Default/Other OS
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
#endif

    setMouseTracking(true);
    resize(m_totalItemWidth + 20, 770); // Adjust width to fit cards, height arbitrary for now
    
    setupUi();

    // Timer for screenshot
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &NewUiWindow::onTimerTimeout);
    // 恢复为 10s 慢速广播，作为基础心跳和极低频更新（状态 A）
    m_timer->start(10000); 
    QTimer::singleShot(0, this, &NewUiWindow::onTimerTimeout);

    m_selfPreviewFastTimer = new QTimer(this);
    m_selfPreviewFastTimer->setInterval(100);
    connect(m_selfPreviewFastTimer, &QTimer::timeout, this, [this]() {
        if (!m_videoLabel) return;
        if (QApplication::applicationState() != Qt::ApplicationActive) return;
        if (!m_listWidget) return;
        QListWidgetItem *current = m_listWidget->currentItem();
        QString userId;
        if (current) {
            userId = current->data(Qt::UserRole).toString();
            if (userId.isEmpty()) {
                if (QWidget *iw = m_listWidget->itemWidget(current)) {
                    if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                        userId = card->property("userId").toString();
                    } else {
                        userId = iw->property("userId").toString();
                    }
                }
            }
        }
        if (userId.isEmpty() || userId != m_myStreamId) {
            stopSelfPreviewFast();
            return;
        }
        QPixmap preview;
        buildLocalPreviewFrameFast(preview);
        if (!preview.isNull()) {
            m_videoLabel->setPixmap(preview);
        }
    });

    m_localActivityMonitor = new LocalActivityMonitor(this);
    connect(m_localActivityMonitor, &LocalActivityMonitor::activityStateChanged, this, [this](bool active) {
        if (m_localActivityActive == active) {
            return;
        }
        m_localActivityActive = active;
        updateLocalCardActivityStyle(active);
        emit localActivityStateChanged(active);
    });
    m_localActivityActive = true;
    updateLocalCardActivityStyle(true);
    emit localActivityStateChanged(true);
    m_localActivityMonitor->start();

    m_talkSpinnerTimer = new QTimer(this);
    connect(m_talkSpinnerTimer, &QTimer::timeout, this, &NewUiWindow::onTalkSpinnerTimeout);

    // Setup StreamClient
    m_streamClient = new StreamClient(this);
    connect(m_streamClient, &StreamClient::logMessage, this, &NewUiWindow::onStreamLog);
    connect(m_streamClient, &StreamClient::textMessageReceived, this, &NewUiWindow::onTextMessageReceived);
    connect(m_streamClient, &StreamClient::connected, this, [this]() {
        publishLocalScreenFrameTriggered(QStringLiteral("cloud_connected"), true, true);
    });
    connect(m_streamClient, &StreamClient::startStreamingRequested, this, [this]() {
        publishLocalScreenFrameTriggered(QStringLiteral("cloud_start_request"), true, false);
    });

    // Auto Updater
    m_autoUpdater = new AutoUpdater(this);
    connect(m_autoUpdater, &AutoUpdater::updateAvailable, this, &NewUiWindow::onUpdateAvailable);
    connect(m_autoUpdater, &AutoUpdater::downloadProgress, this, &NewUiWindow::onUpdateDownloadProgress);
    connect(m_autoUpdater, &AutoUpdater::errorOccurred, this, &NewUiWindow::onUpdateError);

    // Check for updates after 3 seconds
    QTimer::singleShot(3000, this, &NewUiWindow::checkForUpdates);

    if (AppConfig::lanWsEnabled()) {
        m_streamClientLan = new StreamClient(this);
        connect(m_streamClientLan, &StreamClient::logMessage, this, &NewUiWindow::onStreamLog);
        connect(m_streamClientLan, &StreamClient::textMessageReceived, this, &NewUiWindow::onTextMessageReceived);
        connect(m_streamClientLan, &StreamClient::connected, this, [this]() {
            publishLocalScreenFrameTriggered(QStringLiteral("lan_connected"), true, true);
        });
        connect(m_streamClientLan, &StreamClient::startStreamingRequested, this, [this]() {
            publishLocalScreenFrameTriggered(QStringLiteral("lan_start_request"), true, false);
        });
    }

    auto onHoverStream = [this](const QString &targetId, const QString &channelId, int fps, bool enabled, const QString &senderId) {
        bool accept = true;
        if (!targetId.isEmpty() && !m_myStreamId.isEmpty() && targetId != m_myStreamId) {
            accept = false;
        }
        if (!accept && !m_myStreamId.isEmpty() && !channelId.isEmpty()) {
            const QString prefix = QStringLiteral("hfps_%1").arg(m_myStreamId);
            if (channelId == prefix || channelId.startsWith(prefix + QStringLiteral("_"))) {
                accept = true;
            }
        }
        if (!accept) {
            qInfo().noquote() << "[HiFpsPub] hover_stream ignored"
                              << " my_id=" << m_myStreamId
                              << " target_id=" << targetId
                              << " channel_id=" << channelId
                              << " fps=" << fps
                              << " enabled=" << enabled
                              << " sender_id=" << senderId;
            return;
        }

        qInfo().noquote() << "[HiFpsPub] hover_stream accepted"
                          << " my_id=" << m_myStreamId
                          << " target_id=" << targetId
                          << " channel_id=" << channelId
                          << " fps=" << fps
                          << " enabled=" << enabled
                          << " sender_id=" << senderId;

        // Reference Counting for Shared Channels
        QMap<QString, int> &subs = m_channelSubscribers[channelId];
        if (enabled) {
            const QString sid = senderId.isEmpty() ? QStringLiteral("unknown") : senderId;
            subs.insert(sid, fps);
        } else {
            const QString sid = senderId.isEmpty() ? QStringLiteral("unknown") : senderId;
            subs.remove(sid);
        }

        int maxFps = 0;
        for (auto it = subs.constBegin(); it != subs.constEnd(); ++it) {
            if (it.value() > maxFps) {
                maxFps = it.value();
            }
        }

        qInfo().noquote() << "[HiFpsPub] ref_update channel=" << channelId
                          << " subs_count=" << subs.size()
                          << " max_fps=" << maxFps;

        if (maxFps > 0) {
            startHiFpsPublishing(channelId, maxFps);
        } else {
            stopHiFpsPublishing(channelId);
            // Don't remove from m_channelSubscribers immediately if we want to keep memory? 
            // Actually it's better to remove empty entries to save memory.
            if (subs.isEmpty()) {
                m_channelSubscribers.remove(channelId);
            }
        }
    };
    connect(m_streamClient, &StreamClient::hoverStreamRequested, this, onHoverStream);
    if (m_streamClientLan) {
        connect(m_streamClientLan, &StreamClient::hoverStreamRequested, this, onHoverStream);
    }

    m_avatarPublisher = new StreamClient(this);
    connect(m_avatarPublisher, &StreamClient::connected, this, &NewUiWindow::publishLocalAvatarOnce);
    connect(m_avatarPublisher, &StreamClient::startStreamingRequested, this, &NewUiWindow::publishLocalAvatarOnce);

    if (AppConfig::lanWsEnabled()) {
        m_avatarPublisherLan = new StreamClient(this);
        connect(m_avatarPublisherLan, &StreamClient::connected, this, &NewUiWindow::publishLocalAvatarOnce);
        connect(m_avatarPublisherLan, &StreamClient::startStreamingRequested, this, &NewUiWindow::publishLocalAvatarOnce);
    }

    m_hoverCandidateTimer = new QTimer(this);
    m_hoverCandidateTimer->setSingleShot(true);
    connect(m_hoverCandidateTimer, &QTimer::timeout, this, [this]() {
        if (m_hoverCandidateUserId.isEmpty() || m_hoverCandidateUserId == m_myStreamId) {
            return;
        }
        if (QCursor::pos() != m_hoverCandidatePos) {
            return;
        }
        QWidget *under = QApplication::widgetAt(m_hoverCandidatePos);
        const QString currentId = extractUserId(under);
        if (currentId != m_hoverCandidateUserId) {
            return;
        }
        startHiFpsForUser(m_hoverCandidateUserId);
    });

    m_selectionAutoPauseTimer = new QTimer(this);
    m_selectionAutoPauseTimer->setSingleShot(true);
    connect(m_selectionAutoPauseTimer, &QTimer::timeout, this, [this]() {
        if (m_selectionAutoPauseUserId.isEmpty() || m_selectionAutoPauseUserId == m_myStreamId) {
            return;
        }
        if (QApplication::applicationState() != Qt::ApplicationActive || !m_listWidget) {
            return;
        }
        QListWidgetItem *current = m_listWidget->currentItem();
        if (!current) {
            return;
        }
        QString currentId = current->data(Qt::UserRole).toString();
        if (currentId.isEmpty()) {
            if (QWidget *iw = m_listWidget->itemWidget(current)) {
                if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                    currentId = card->property("userId").toString();
                } else {
                    currentId = iw->property("userId").toString();
                }
            }
        }
        if (currentId != m_selectionAutoPauseUserId) {
            return;
        }
        if (m_autoPausedUserId == currentId) {
            return;
        }
        pauseSelectedStreamForUser(currentId);
    });

    m_avatarPublishTimer = new QTimer(this);
    connect(m_avatarPublishTimer, &QTimer::timeout, this, &NewUiWindow::publishLocalAvatarOnce);
    m_avatarPublishTimer->start(60 * 60 * 1000);
    
    // 2. Connect Login Client (User List & Discovery)
    // DISABLED: Main Window controls the user list now.
    /*
    m_loginClient = new LoginClient(this);
    connect(m_loginClient, &LoginClient::logMessage, this, &NewUiWindow::onStreamLog);
    connect(m_loginClient, &LoginClient::userListUpdated, this, &NewUiWindow::onUserListUpdated);
    connect(m_loginClient, &LoginClient::connected, this, &NewUiWindow::onLoginConnected);
    */

    resize(1160, 720);
    if (QScreen *screen = QGuiApplication::screenAt(QCursor::pos())) {
        const QRect avail = screen->availableGeometry();
        const QSize sz = size();
        const QPoint topLeft(avail.x() + (avail.width() - sz.width()) / 2,
                             avail.y() + (avail.height() - sz.height()) / 2);
        move(topLeft);
    }

    m_resizeGripLeft = new QWidget(this);
    m_resizeGripRight = new QWidget(this);
    m_resizeGripTop = new QWidget(this);
    m_resizeGripBottom = new QWidget(this);
    m_resizeGripTopLeft = new QWidget(this);
    m_resizeGripTopRight = new QWidget(this);
    m_resizeGripBottomLeft = new QWidget(this);
    m_resizeGripBottomRight = new QWidget(this);

    const QList<QWidget*> grips = {
        m_resizeGripLeft, m_resizeGripRight, m_resizeGripTop, m_resizeGripBottom,
        m_resizeGripTopLeft, m_resizeGripTopRight, m_resizeGripBottomLeft, m_resizeGripBottomRight
    };
    for (QWidget *g : grips) {
        g->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        g->setAttribute(Qt::WA_TranslucentBackground, true);
        g->setAttribute(Qt::WA_NoSystemBackground, true);
        g->setAutoFillBackground(false);
        g->setStyleSheet(QStringLiteral("background: transparent;"));
        g->setMouseTracking(true);
        g->installEventFilter(this);
        g->raise();
    }
    m_resizeGripLeft->setCursor(Qt::SizeHorCursor);
    m_resizeGripRight->setCursor(Qt::SizeHorCursor);
    m_resizeGripTop->setCursor(Qt::SizeVerCursor);
    m_resizeGripBottom->setCursor(Qt::SizeVerCursor);
    m_resizeGripTopLeft->setCursor(Qt::SizeFDiagCursor);
    m_resizeGripBottomRight->setCursor(Qt::SizeFDiagCursor);
    m_resizeGripTopRight->setCursor(Qt::SizeBDiagCursor);
    m_resizeGripBottomLeft->setCursor(Qt::SizeBDiagCursor);
    updateResizeGrips();
    setResizeGripsVisible(!(windowState() & Qt::WindowMaximized));
}

void NewUiWindow::setMyStreamId(const QString &id, const QString &name)
{
    const QString oldId = m_myStreamId;
    m_myStreamId = id;
    m_myUserName = name;
    updateLocalWatchedOverlay();

    // Update local user label if it exists
    if (m_localNameLabel) {
        QString displayName = m_myUserName.isEmpty() ? m_myStreamId : m_myUserName;
        QString fullText = displayName; // Only display name
        m_localNameLabel->setText(fullText);
    }
    if (m_videoLabel) {
        m_videoLabel->installEventFilter(this);
        if (auto *pw = m_videoLabel->parentWidget()) {
            pw->installEventFilter(this);
        }
    }

    // [Interaction Fix] Update local card userId property for event filter
    if (m_localCard) {
        m_localCard->setProperty("userId", m_myStreamId);
    }

    if (m_localAvatarLabel) {
        if (!oldId.isEmpty()) {
            m_userAvatarLabels.remove(oldId);
        }
        if (!m_myStreamId.isEmpty()) {
            m_userAvatarLabels.insert(m_myStreamId, m_localAvatarLabel);
        }
    }

    if (!oldId.isEmpty() && oldId == m_myStreamId) {
        return;
    }

    // 1. Connect Stream Client (Push)
    const QString previewChannelId = QStringLiteral("preview_%1").arg(m_myStreamId);
    QString serverUrl = QString("%1/publish/%2").arg(AppConfig::wsBaseUrl(), previewChannelId);
    
    if (m_streamClient) {
        m_streamClient->connectToServer(QUrl(serverUrl));
    }

    if (m_streamClientLan) {
        QUrl u(QStringLiteral("ws://127.0.0.1:%1").arg(AppConfig::lanWsPort()));
        u.setPath(QStringLiteral("/publish/%1").arg(previewChannelId));
        m_streamClientLan->connectToServer(u);
    }

    if (!m_myStreamId.isEmpty()) {
        ensureAvatarCacheDir();
        const QString cacheFile = avatarCacheFilePath(m_myStreamId);
        if (!QFileInfo::exists(cacheFile)) {
            QPixmap savePix = buildTestAvatarPixmap(128);
            if (savePix.isNull()) {
                savePix = QPixmap(128, 128);
                savePix.fill(QColor(90, 90, 90));
            }
            QSaveFile f(cacheFile);
            if (f.open(QIODevice::WriteOnly)) {
                savePix.save(&f, "PNG");
                f.commit();
            }
        }

        QPixmap cached(cacheFile);
        if (!cached.isNull()) {
            m_localAvatarPublishPixmap = cached.scaled(128, 128, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        }

        if (m_avatarPublisher) {
            const QString channelId = QString("avatar_%1").arg(m_myStreamId);
            const QString pubUrl = QString("%1/publish/%2").arg(AppConfig::wsBaseUrl(), channelId);
            m_avatarPublisher->connectToServer(QUrl(pubUrl));
        }

        if (m_avatarPublisherLan) {
            const QString channelId = QString("avatar_%1").arg(m_myStreamId);
            QUrl u(QStringLiteral("ws://127.0.0.1:%1").arg(AppConfig::lanWsPort()));
            u.setPath(QStringLiteral("/publish/%1").arg(channelId));
            m_avatarPublisherLan->connectToServer(u);
        }

        ensureAvatarSubscription(m_myStreamId);
        refreshLocalAvatarFromCache();
    }

    if (!m_myStreamId.isEmpty()) {
        m_janusDesiredRoomOwnerId = m_myStreamId;
        ensureJanusAudioLoaded();
        applyJanusAudioState();
    }

    // 2. Connect Login Client
    // DISABLED: Main Window controls login.
    /*
    QString loginUrl = QString("%1/login").arg(AppConfig::wsBaseUrl());
    if (m_loginClient) {
        m_loginClient->disconnectFromServer();
        m_loginClient->connectToServer(QUrl(loginUrl));
    }
    */
}

void NewUiWindow::setCaptureScreenIndex(int index)
{
    m_captureScreenIndex = index;
    if (m_videoLabel) {
        onTimerTimeout();
    }
}

void NewUiWindow::setRemoteActivityState(const QString &userId, bool active)
{
    if (userId.isEmpty() || userId == m_myStreamId) {
        return;
    }
    const bool prev = m_remoteActivityStates.value(userId, true);
    if (prev == active && m_remoteActivityStates.contains(userId)) {
        return;
    }
    m_remoteActivityStates.insert(userId, active);
    updateRemoteCardActivityStyle(userId);
}

bool NewUiWindow::localActivityActive() const
{
    return m_localActivityActive;
}

NewUiWindow::~NewUiWindow()
{

    if (m_timer && m_timer->isActive()) {
        m_timer->stop();
    }
    if (m_talkSpinnerTimer && m_talkSpinnerTimer->isActive()) {
        m_talkSpinnerTimer->stop();
    }
    if (m_avatarPublishTimer && m_avatarPublishTimer->isActive()) {
        m_avatarPublishTimer->stop();
    }
    
    // Cleanup Auto Updater
    if (m_updateProgressDialog) {
        m_updateProgressDialog->close();
        delete m_updateProgressDialog;
    }
    // m_autoUpdater is a child of this, so it will be deleted automatically,
    // but good to stop any pending operations
    if (m_autoUpdater) {
        m_autoUpdater->cancel();
    }

    stopHiFpsForUser();
    const QStringList chs = m_hiFpsPublishers.keys();
    for (const QString &ch : chs) {
        stopHiFpsPublishing(ch);
    }

    if (m_streamClient) {
        m_streamClient->disconnectFromServer();
    }
    if (m_avatarPublisher) {
        m_avatarPublisher->disconnectFromServer();
    }
    const QStringList avatarKeys = m_avatarSubscribers.keys();
    for (const QString &k : avatarKeys) {
        if (StreamClient *c = m_avatarSubscribers.value(k, nullptr)) {
            c->disconnectFromServer();
        }
    }
    const QStringList remoteKeys = m_remoteStreams.keys();
    for (const QString &k : remoteKeys) {
        if (StreamClient *c = m_remoteStreams.value(k, nullptr)) {
            c->disconnectFromServer();
        }
    }
}

void NewUiWindow::onLoginConnected()
{
    // Auto-login after connection
    // DISABLED: Main Window handles login
    // m_loginClient->login(m_myStreamId, m_myUserName);
}

QIcon NewUiWindow::buildSpinnerIcon(int size, int angleDeg) const
{
    const int s = qMax(8, size);
    QPixmap pix(s, s);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(QColor(230, 230, 230, 230));
    pen.setWidthF(qMax(1.5, s / 10.0));
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);

    const qreal pad = pen.widthF() + 1.0;
    QRectF r(pad, pad, s - 2 * pad, s - 2 * pad);
    const int start = (90 - angleDeg) * 16;
    const int span = 120 * 16;
    p.drawArc(r, start, span);

    return QIcon(pix);
}

QPixmap NewUiWindow::buildTestAvatarPixmap(int size) const
{
    const int s = qMax(8, size);
    const QString weChatDirPath = QStringLiteral("C:/Users/Administrator/Documents/WeChat Files/All Users");
    QString avatarPath;
    {
        QDir dir(weChatDirPath);
        if (dir.exists()) {
            QStringList filters;
            filters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.webp";
            const QFileInfoList files = dir.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::NoSort);
            QFileInfo best;
            for (const QFileInfo &fi : files) {
                if (!best.exists() || fi.lastModified() > best.lastModified()) {
                    best = fi;
                }
            }
            if (best.exists()) {
                avatarPath = best.absoluteFilePath();
            }
        }
    }

    if (avatarPath.isEmpty()) {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString candidate1 = QDir(appDir).filePath("maps/logo/head.png");
        const QString candidate2 = QDir::current().filePath("src/maps/logo/head.png");
        avatarPath = QFileInfo::exists(candidate1) ? candidate1 : candidate2;
    }

    QPixmap src(avatarPath);
    if (src.isNull()) {
        return QPixmap();
    }
    return makeCircularPixmap(src, s);
}

QPixmap NewUiWindow::buildHeadAvatarPixmap(int size) const
{
    const int s = qMax(8, size);
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString candidate1 = QDir(appDir).filePath("maps/logo/head.png");
    const QString candidate2 = QDir::current().filePath("src/maps/logo/head.png");
    const QString avatarPath = QFileInfo::exists(candidate1) ? candidate1 : candidate2;

    QPixmap src(avatarPath);
    if (src.isNull()) {
        return QPixmap();
    }
    return makeCircularPixmap(src, s);
}

void NewUiWindow::pickAndApplyLocalAvatar()
{
    if (m_myStreamId.isEmpty()) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择头像"),
        QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All Files (*)")
    );
    if (path.isEmpty()) {
        return;
    }

    QPixmap src(path);
    if (src.isNull()) {
        return;
    }

    ensureAvatarCacheDir();
    const QPixmap savePix = makeCircularPixmap(src, 256);
    if (savePix.isNull()) {
        return;
    }

    QSaveFile f(avatarCacheFilePath(m_myStreamId));
    if (f.open(QIODevice::WriteOnly)) {
        savePix.save(&f, "PNG");
        f.commit();
    }

    m_localAvatarPublishPixmap = makeCircularPixmap(savePix, 128);
    refreshLocalAvatarFromCache();
    publishLocalAvatarOnce();
}

QString NewUiWindow::avatarCacheDirPath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return QDir(appDir).filePath("avatars");
}

QString NewUiWindow::avatarCacheFilePath(const QString &userId) const
{
    return QDir(avatarCacheDirPath()).filePath(userId + ".png");
}

void NewUiWindow::ensureAvatarCacheDir()
{
    QDir dir(avatarCacheDirPath());
    if (!dir.exists()) {
        QDir().mkpath(dir.path());
    }
}

QPixmap NewUiWindow::makeCircularPixmap(const QPixmap &src, int size) const
{
    const int s = qMax(8, size);
    if (src.isNull()) {
        return QPixmap();
    }

    QPixmap scaled = src.scaled(s, s, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (scaled.width() == s && scaled.height() == s) {
        return scaled;
    }
    const int x = qMax(0, (scaled.width() - s) / 2);
    const int y = qMax(0, (scaled.height() - s) / 2);
    return scaled.copy(x, y, s, s);
}

void NewUiWindow::setAvatarLabelPixmap(QLabel *label, const QPixmap &src)
{
    if (!label) {
        return;
    }
    const int s = qMin(label->width(), label->height());
    QPixmap out = makeCircularPixmap(src, s);
    if (!out.isNull()) {
        label->setPixmap(out);
    }
}

void NewUiWindow::publishLocalAvatarOnce()
{
    if (!m_avatarPublisher || !m_avatarPublisher->isConnected()) {
    } else if (!m_localAvatarPublishPixmap.isNull()) {
        m_avatarPublisher->sendFrame(m_localAvatarPublishPixmap, true);
    }
    if (m_localAvatarPublishPixmap.isNull()) {
    } else if (m_avatarPublisherLan && m_avatarPublisherLan->isConnected()) {
        m_avatarPublisherLan->sendFrame(m_localAvatarPublishPixmap, true);
    }
}

void NewUiWindow::publishLocalAvatarHint()
{
    if (!m_avatarPublisher || !m_avatarPublisher->isConnected()) {
    } else if (!m_localAvatarPublishPixmap.isNull()) {
        m_avatarPublisher->sendFrame(m_localAvatarPublishPixmap, false);
    }
    if (m_localAvatarPublishPixmap.isNull()) {
    } else if (m_avatarPublisherLan && m_avatarPublisherLan->isConnected()) {
        m_avatarPublisherLan->sendFrame(m_localAvatarPublishPixmap, false);
    }
}

void NewUiWindow::refreshLocalAvatarFromCache()
{
    if (m_myStreamId.isEmpty()) {
        return;
    }

    ensureAvatarCacheDir();
    QPixmap cached(avatarCacheFilePath(m_myStreamId));
    if (!cached.isNull()) {
        setAvatarLabelPixmap(m_localAvatarLabel, cached);
        setAvatarLabelPixmap(m_toolbarAvatarLabel, cached);
        emit avatarPixmapUpdated(m_myStreamId, cached);
    }
}

void NewUiWindow::ensureAvatarSubscription(const QString &userId)
{
    if (userId.isEmpty()) {
        return;
    }
    if (m_avatarSubscribers.contains(userId)) {
        return;
    }

    ensureAvatarCacheDir();
    const QString cachedPath = avatarCacheFilePath(userId);
    QPixmap cached(cachedPath);
    QLabel *label = m_userAvatarLabels.value(userId, nullptr);
    if (!cached.isNull()) {
        setAvatarLabelPixmap(label, cached);
        if (userId == m_myStreamId) {
            setAvatarLabelPixmap(m_localAvatarLabel, cached);
            setAvatarLabelPixmap(m_toolbarAvatarLabel, cached);
        }
        emit avatarPixmapUpdated(userId, cached);
    }

    StreamClient *client = new StreamClient(this);
    m_avatarSubscribers.insert(userId, client);
    connect(client, &StreamClient::frameReceived, this, [this, userId](const QPixmap &frame) {
        if (frame.isNull()) {
            return;
        }

        QLabel *avatarLabel = m_userAvatarLabels.value(userId, nullptr);
        setAvatarLabelPixmap(avatarLabel, frame);
        if (userId == m_myStreamId) {
            setAvatarLabelPixmap(m_localAvatarLabel, frame);
            setAvatarLabelPixmap(m_toolbarAvatarLabel, frame);
        }

        ensureAvatarCacheDir();
        QPixmap savePix = frame.scaled(128, 128, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        QSaveFile f(avatarCacheFilePath(userId));
        if (f.open(QIODevice::WriteOnly)) {
            savePix.save(&f, "PNG");
            f.commit();
        }

        emit avatarPixmapUpdated(userId, frame);
    });

    const QString channelId = QString("avatar_%1").arg(userId);
    const QString subscribeUrl = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), channelId);
    client->connectToServer(QUrl(subscribeUrl));
}

void NewUiWindow::setTalkPending(const QString &userId, bool pending)
{
    QPushButton *btn = m_talkButtons.value(userId, nullptr);
    if (!btn) {
        return;
    }

    btn->setProperty("isPending", pending);

    if (pending) {
        if (!m_talkSpinnerAngles.contains(userId)) {
            m_talkSpinnerAngles.insert(userId, 0);
        }
        if (m_talkSpinnerTimer && !m_talkSpinnerTimer->isActive()) {
            m_talkSpinnerTimer->start(60);
        }
        onTalkSpinnerTimeout();
        updateTalkOverlay(userId);
        return;
    }

    m_talkSpinnerAngles.remove(userId);
    if (m_talkSpinnerAngles.isEmpty() && m_talkSpinnerTimer && m_talkSpinnerTimer->isActive()) {
        m_talkSpinnerTimer->stop();
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const bool isOn = btn->property("isOn").toBool();
    const QString iconName = "get.png";
    btn->setIcon(QIcon(appDir + "/maps/logo/" + iconName));
    updateTalkOverlay(userId);
}

void NewUiWindow::setTalkConnected(const QString &userId, bool connected)
{
    QPushButton *btn = m_talkButtons.value(userId, nullptr);
    if (!btn) {
        return;
    }

    btn->setProperty("isOn", connected);
    setTalkPending(userId, false);
    updateTalkOverlay(userId);
    if (connected) {
        showAudioCallUi(userId);
    } else if (m_audioCallPeerId == userId) {
        hideAudioCallUi();
    }
}

void NewUiWindow::setTalkRemoteActive(const QString &userId, bool active)
{
    QPushButton *btn = m_talkButtons.value(userId, nullptr);
    if (!btn) {
        return;
    }

    btn->setProperty("remoteActive", active);
    btn->setProperty("isOn", active);
    setTalkPending(userId, false);
    updateTalkOverlay(userId);
    if (active) {
        showAudioCallUi(userId);
    } else if (m_audioCallPeerId == userId) {
        hideAudioCallUi();
    }
}

void NewUiWindow::updateTalkOverlay(const QString &userId)
{
    QLabel *overlay = m_talkOverlays.value(userId, nullptr);
    if (!overlay) {
        return;
    }
    overlay->setWordWrap(true);
    overlay->setAlignment(Qt::AlignCenter);

    auto applyOverlayTextAutoFit = [](QLabel *lbl, const QString &text) {
        if (!lbl) {
            return;
        }
        lbl->setText(text);
        const int w = qMax(1, lbl->width() - 12);
        const int h = qMax(1, lbl->height() - 12);

        QFont f = lbl->font();
        f.setBold(true);

        const int maxPx = qBound(12, lbl->height() / 4, 22);
        const int minPx = 10;

        bool ok = false;
        for (int px = maxPx; px >= minPx; --px) {
            f.setPixelSize(px);
            QFontMetrics fm(f);
            const QRect br = fm.boundingRect(QRect(0, 0, w, h), Qt::TextWordWrap | Qt::AlignCenter, text);
            if (br.width() <= w && br.height() <= h) {
                lbl->setFont(f);
                ok = true;
                break;
            }
        }
        if (!ok) {
            f.setPixelSize(minPx);
            lbl->setFont(f);
            QFontMetrics fm(f);
            lbl->setWordWrap(false);
            lbl->setText(fm.elidedText(text, Qt::ElideRight, w));
            lbl->setWordWrap(true);
        }
    };

    const bool watching = (!m_watchingTargetId.isEmpty() && userId == m_watchingTargetId);
    const bool beingWatchedBy = isInMyRoomViewerList(userId);
    QString myName = m_myUserName.isEmpty() ? m_myStreamId : m_myUserName;
    if (myName.isEmpty()) {
        myName = QStringLiteral("我");
    }
    if (watching) {
        // [Fix] Disable "Watching" overlay and text as requested
        overlay->setVisible(false);
    } else if (beingWatchedBy) {
        // [Fix] Disable "Being Watched" overlay and text as requested
        overlay->setVisible(false);
    } else {
        overlay->setVisible(false);
    }

    updateRemoteCardActivityStyle(userId);
}

void NewUiWindow::setWatchingTarget(const QString &targetId)
{
    if (m_watchingTargetId == targetId) {
        return;
    }
    m_watchingTargetId = targetId;
    const QStringList userIds = m_userItems.keys();
    for (const QString &userId : userIds) {
        updateTalkOverlay(userId);
    }
}

void NewUiWindow::onTalkSpinnerTimeout()
{
    if (m_talkSpinnerAngles.isEmpty()) {
        if (m_talkSpinnerTimer && m_talkSpinnerTimer->isActive()) {
            m_talkSpinnerTimer->stop();
        }
        return;
    }

    const QStringList ids = m_talkSpinnerAngles.keys();
    for (const QString &id : ids) {
        QPushButton *btn = m_talkButtons.value(id, nullptr);
        if (!btn || !btn->property("isPending").toBool()) {
            m_talkSpinnerAngles.remove(id);
            continue;
        }

        int angle = m_talkSpinnerAngles.value(id, 0);
        angle = (angle + 30) % 360;
        m_talkSpinnerAngles[id] = angle;

        const int size = qMin(btn->width(), btn->height());
        btn->setIcon(buildSpinnerIcon(size, angle));
        btn->setIconSize(QSize(size, size));
    }

    if (m_talkSpinnerAngles.isEmpty() && m_talkSpinnerTimer && m_talkSpinnerTimer->isActive()) {
        m_talkSpinnerTimer->stop();
    }
}

void NewUiWindow::onUserListUpdated(const QJsonArray &users)
{
    // DISABLED: Main Window handles user list updates
    // updateListWidget(users);
}

void NewUiWindow::updateListWidget(const QJsonArray &users)
{
    if (!m_listWidget) return;

    // Collect current remote users from the list
    QSet<QString> currentRemoteUsers;
    for (const QJsonValue &val : users) {
        QJsonObject user = val.toObject();
        QString id = user["id"].toString();
        if (id != m_myStreamId) {
            currentRemoteUsers.insert(id);
        }
    }

    // 1. Identify users to REMOVE
    // Iterate over our tracking map
    QList<QString> usersToRemove;
    for (auto it = m_remoteStreams.begin(); it != m_remoteStreams.end(); ++it) {
        if (!currentRemoteUsers.contains(it.key())) {
            usersToRemove.append(it.key());
        }
    }

    for (const QString &id : usersToRemove) {
        // Stop stream
        StreamClient *client = m_remoteStreams.take(id);
        if (client) {
            client->disconnectFromServer();
            client->deleteLater();
        }

        // Remove label reference
        m_userLabels.remove(id);
        m_talkButtons.remove(id);
        m_talkOverlays.remove(id);
        m_talkSpinnerAngles.remove(id);

        // Remove list item
        QListWidgetItem *item = m_userItems.take(id);
        if (item) {
            int row = m_listWidget->row(item);
            if (row >= 0) {
                delete m_listWidget->takeItem(row);
            }
            // item is deleted by takeItem if we manage it correctly, or we delete it manually
            // QListWidget::takeItem returns the item, ownership is transferred to caller.
        }
    }

    // 2. Identify users to ADD or UPDATE
    QString appDir = QCoreApplication::applicationDirPath();

    for (const QJsonValue &val : users) {
        QJsonObject user = val.toObject();
        QString id = user["id"].toString();
        QString name = user["name"].toString();

        if (id == m_myStreamId) continue;

        if (m_remoteStreams.contains(id)) {
            QLabel *imgLabel = m_userLabels.value(id, nullptr);
            if (imgLabel) {
            }
            QListWidgetItem *existingItem = m_userItems.value(id, nullptr);
            if (existingItem && m_listWidget) {
                QWidget *existingWidget = m_listWidget->itemWidget(existingItem);
                if (existingWidget) {
                    QLabel *existingNameLabel = existingWidget->findChild<QLabel*>("UserNameLabel");
                    if (existingNameLabel) {
                        existingNameLabel->setText(name.isEmpty() ? id : name);
                    }
                }
            }
            continue;
        }

        // NEW USER -> Add to List & Subscribe
        QListWidgetItem *item = new QListWidgetItem(m_listWidget);
        item->setSizeHint(QSize(m_totalItemWidth, m_totalItemHeight));

        // Create the Item Widget (Container for the card)
        QWidget *itemWidget = new QWidget();
        itemWidget->setAttribute(Qt::WA_TranslucentBackground);
        QVBoxLayout *itemLayout = new QVBoxLayout(itemWidget);
        // Margins for shadow
        itemLayout->setContentsMargins(m_shadowSize, m_shadowSize, m_shadowSize, m_shadowSize);
        itemLayout->setSpacing(0);

        // The Card Frame (Visible Part)
        QFrame *card = new QFrame();
        card->setObjectName("CardFrame");
        // [Interaction Fix] Install event filter on local card to allow double-click testing
        card->installEventFilter(this);
        card->setProperty("userId", id);
        card->setProperty("userName", name.isEmpty() ? id : name);
        // m_localCard = card; // REMOVED: Incorrectly assigning remote card to local pointer

        card->setStyleSheet(
            "#CardFrame {"
            "   background-color: rgba(32, 32, 36, 175);"
            "   border: 1px solid rgba(255, 255, 255, 22);"
            "   border-radius: 15px;"
            "}"
            "#CardFrame:hover {"
            "   background-color: rgba(40, 40, 45, 190);"
            "}"
        );
        
        // Shadow Effect
        QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect();
        shadow->setBlurRadius(18); 
        shadow->setColor(QColor(0, 0, 0, 140));
        shadow->setOffset(0, 6);
        card->setGraphicsEffect(shadow);

        QVBoxLayout *cardLayout = new QVBoxLayout(card);
        // Padding creates the visible border around the image
        // Top: Centering margin, Sides: Centering margin, Bottom: 0 (Controls area handles its own padding)
        cardLayout->setContentsMargins(m_marginX, m_marginTop, m_marginX, 0);
        cardLayout->setSpacing(0); 

        // Image Label
        QLabel *imgLabel = new QLabel();
        imgLabel->setObjectName("StreamImageLabel");
        imgLabel->setProperty("userId", id);
        imgLabel->setFixedSize(m_imgWidth, m_imgHeight); 
        imgLabel->setAlignment(Qt::AlignCenter);
        imgLabel->setText("Loading Stream...");
        imgLabel->setStyleSheet("color: #888; font-size: 10px;");
        imgLabel->installEventFilter(this);

    QLabel *talkOverlay = new QLabel(imgLabel);
        talkOverlay->setText(QString());
        talkOverlay->setAlignment(Qt::AlignCenter);
        talkOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
        talkOverlay->setGeometry(0, 0, m_imgWidth, m_imgHeight);
        talkOverlay->setStyleSheet("color: rgba(255, 255, 255, 235); font-size: 34px; font-weight: bold; background-color: rgba(0, 200, 83, 90); border-radius: 8px;");
        talkOverlay->setVisible(false);
        if (id != m_myStreamId) {
            m_talkOverlays.insert(id, talkOverlay);
        }

        // Bottom Controls Layout
        QHBoxLayout *bottomLayout = new QHBoxLayout();
        // Zero side margins because parent cardLayout already provides MARGIN_X
        // But we might want buttons to extend a bit wider? No, keep alignment.
        // Add a bit of bottom padding
        bottomLayout->setContentsMargins(0, 0, 0, 5); 
        bottomLayout->setSpacing(5);

        // Left Button (tab1.png)
        QPushButton *tabBtn = nullptr;
        if (id != m_myStreamId) {
            tabBtn = new QPushButton();
            tabBtn->setFixedSize(14, 14);
            tabBtn->setCursor(Qt::PointingHandCursor);
            tabBtn->setFlat(true);
            tabBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
            tabBtn->setIcon(QIcon(appDir + "/maps/logo/in.png"));
            tabBtn->setIconSize(QSize(14, 14));

            connect(tabBtn, &QPushButton::clicked, this, [this, id, name]() {
                emit startWatchingRequested(id, name);
            });
        }
        
        // Text Label (Middle)
        QLabel *txtLabel = new QLabel(name.isEmpty() ? id : name);
        txtLabel->setObjectName("UserNameLabel");
        txtLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        txtLabel->setStyleSheet("color: #e0e0e0; font-size: 12px; border: none; background: transparent;");
        txtLabel->setAlignment(Qt::AlignCenter);

        // Mic Toggle
        QPushButton *micBtn = nullptr;
        if (id != m_myStreamId) {
            micBtn = new QPushButton();
            micBtn->setFixedSize(14, 14);
            micBtn->setCursor(Qt::PointingHandCursor);
            micBtn->setProperty("isOn", false);
            micBtn->setProperty("remoteActive", false);
            micBtn->setFlat(true);
            micBtn->setStyleSheet("QPushButton { border: none; background: transparent; }");
            micBtn->setIcon(QIcon(appDir + "/maps/logo/get.png"));
            micBtn->setIconSize(QSize(14, 14));

            m_talkButtons.insert(id, micBtn);
            updateTalkButtonsAvailability();
            connect(micBtn, &QPushButton::clicked, [this, micBtn, appDir, id]() {
                if (!m_audioCallPeerId.isEmpty() && id != m_audioCallPeerId) {
                    return;
                }
                const bool remoteActive = micBtn->property("remoteActive").toBool();
                bool isOn = micBtn->property("isOn").toBool();
                if (remoteActive) {
                    setTalkRemoteActive(id, false);
                    if (id != m_myStreamId) {
                        setTalkConnected(id, false);
                        emit talkToggleRequested(id, false);
                        return;
                    }
                }
                if (isOn) return; // Only dial, no hangup
                isOn = true;
                micBtn->setProperty("isOn", isOn);
                if (isOn) {
                    const QStringList keys = m_talkButtons.keys();
                    for (const QString &otherId : keys) {
                        if (otherId == id) continue;
                        setTalkConnected(otherId, false);
                        emit talkToggleRequested(otherId, false);
                    }
                }
                if (isOn) {
                    setTalkPending(id, true);
                } else {
                    setTalkConnected(id, false);
                }
                emit talkToggleRequested(id, isOn);
            });
        }

        if (tabBtn) {
            bottomLayout->addWidget(tabBtn);
        } else {
            bottomLayout->addStretch();
        }
        bottomLayout->addWidget(txtLabel);
        if (micBtn) {
            bottomLayout->addWidget(micBtn);
        } else {
            bottomLayout->addStretch();
        }

        cardLayout->addWidget(imgLabel);
        cardLayout->addLayout(bottomLayout);

        itemLayout->addWidget(card);

        m_listWidget->setItemWidget(item, itemWidget);

        // --- Track & Subscribe ---
        m_userItems.insert(id, item);
        m_userLabels.insert(id, imgLabel);

        // Create Client
        StreamClient *client = new StreamClient(this);
        m_remoteStreams.insert(id, client);

        // Connect signals
        connect(client, &StreamClient::frameReceived, this, [this, id](const QPixmap &frame) {
            if (m_userLabels.contains(id)) {
                QLabel *lbl = m_userLabels[id];
                if (lbl) {
                    // Apply rounded corners and scaling exactly like local user
                    QPixmap pixmap(m_imgWidth, m_imgHeight);
                    pixmap.setDevicePixelRatio(1.0);
                    pixmap.fill(Qt::transparent);
                    QPainter p(&pixmap);
                    p.setRenderHint(QPainter::Antialiasing);
                    p.setRenderHint(QPainter::SmoothPixmapTransform);
                    
                    QPixmap scaledPix = frame.scaled(m_imgWidth, m_imgHeight, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                    int x = (m_imgWidth - scaledPix.width()) / 2;
                    int y = (m_imgHeight - scaledPix.height()) / 2;
                    
                    QPainterPath path;
                    path.addRoundedRect(0, 0, m_imgWidth, m_imgHeight, 8, 8);
                    p.setClipPath(path);
                    
                    p.drawPixmap(x, y, scaledPix);
                    p.end();
                    
                    lbl->setPixmap(pixmap);
                }
            }
        });
        
        // Connect to Subscribe URL
        QString subUrl = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), id);
        client->connectToServer(QUrl(subUrl));
    }
}

void NewUiWindow::onStreamLog(const QString &msg)
{
    qInfo().noquote() << msg;
}

VideoDisplayWidget* NewUiWindow::embeddedVideoWidget() const
{
    return m_embeddedVideoWidget;
}

void NewUiWindow::updateEmbeddedFullscreenOverlayGeometry()
{
    if (!m_embeddedFullscreenOverlay || !m_embeddedVideoWidget) {
        return;
    }

    const int overlayHeight = 56;
    m_embeddedFullscreenOverlay->setGeometry(0, 0, m_embeddedVideoWidget->width(), overlayHeight);
    m_embeddedFullscreenOverlay->raise();
}

void NewUiWindow::toggleEmbeddedVideoFullscreen(bool maximized)
{
    if (!m_embeddedVideoWidget) return;
    
    // Update local toolbar state
    if (m_annotationToolbar) {
        m_annotationToolbar->setMaximizedState(maximized);
    }

    if (maximized) {
        m_embeddedFullscreenActive = true;
        m_embeddedVideoWidget->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        m_embeddedVideoWidget->showFullScreen();

        if (!m_embeddedFullscreenOverlay) {
            m_embeddedFullscreenOverlay = new QWidget(m_embeddedVideoWidget);
            m_embeddedFullscreenOverlay->setAttribute(Qt::WA_TranslucentBackground);
            m_embeddedFullscreenOverlay->setStyleSheet(QStringLiteral("background: transparent;"));
            m_embeddedFullscreenOverlay->setFixedHeight(56);

            QHBoxLayout *overlayLayout = new QHBoxLayout(m_embeddedFullscreenOverlay);
            overlayLayout->setContentsMargins(0, 8, 0, 0);
            overlayLayout->setSpacing(0);
            overlayLayout->addStretch();
            if (m_annotationContainer) {
                m_annotationContainer->setParent(m_embeddedFullscreenOverlay);
                overlayLayout->addWidget(m_annotationContainer);
            }
            overlayLayout->addStretch();
        } else {
            m_embeddedFullscreenOverlay->setParent(m_embeddedVideoWidget);
            m_embeddedFullscreenOverlay->show();
            if (m_annotationContainer && m_annotationContainer->parent() != m_embeddedFullscreenOverlay) {
                m_annotationContainer->setParent(m_embeddedFullscreenOverlay);
            }
        }

        m_embeddedFullscreenOverlay->show();
        updateEmbeddedFullscreenOverlayGeometry();
        QTimer::singleShot(0, this, [this]() { updateEmbeddedFullscreenOverlayGeometry(); });
    } else {
        m_embeddedFullscreenActive = false;
        m_embeddedVideoWidget->setWindowFlags(Qt::Widget);
        if (m_videoContentPage) {
            QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(m_videoContentPage->layout());
            if (layout) {
                layout->addWidget(m_embeddedVideoWidget);
            }
        }
        m_embeddedVideoWidget->show();

        if (m_embeddedFullscreenOverlay) {
            m_embeddedFullscreenOverlay->hide();
        }

        if (m_annotationContainer && m_videoTopBar) {
            QHBoxLayout *videoTopLayout = qobject_cast<QHBoxLayout*>(m_videoTopBar->layout());
            if (videoTopLayout) {
                m_annotationContainer->setParent(m_videoTopBar);
                if (videoTopLayout->indexOf(m_annotationContainer) == -1) {
                    while (QLayoutItem *it = videoTopLayout->takeAt(0)) {
                        delete it;
                    }
                    videoTopLayout->addSpacing(8);
                    if (m_titleBackBtn) {
                        videoTopLayout->addWidget(m_titleBackBtn);
                    }
                    videoTopLayout->addStretch();
                    videoTopLayout->addWidget(m_annotationContainer, 0, Qt::AlignHCenter);
                    videoTopLayout->addStretch();
                    if (m_videoTopRightPlaceholder) {
                        videoTopLayout->addWidget(m_videoTopRightPlaceholder);
                    }
                    videoTopLayout->addSpacing(8);
                }
            }
        }
    }
    
    emit videoFullscreenToggled(maximized);
}

bool NewUiWindow::isEmbeddedWatching() const
{
    return !m_embeddedTargetId.isEmpty();
}

bool NewUiWindow::isEmbeddedWatchingTarget(const QString &targetId) const
{
    if (targetId.isEmpty()) {
        return false;
    }
    return m_embeddedTargetId == targetId;
}

void NewUiWindow::enterEmbeddedWatchingUi(const QString &targetId, const QString &targetName)
{
    Q_UNUSED(targetName);
    m_embeddedTargetId = targetId;
    if (!targetId.isEmpty()) {
        janusSwitchToUserRoom(targetId);
        showAudioCallUi(targetId);
    }
    if (m_rightContentStack && m_videoContentPage) {
        m_rightContentStack->setCurrentWidget(m_videoContentPage);
    }
    if (m_embeddedVideoWidget) {
        m_embeddedVideoWidget->showSwitchingIndicator(QStringLiteral("切换中..."));
    }
}

void NewUiWindow::startEmbeddedReceiving(const QString &viewerId,
                                        const QString &targetId,
                                        const QString &viewerName,
                                        const QString &serverUrl,
                                        int initialColorId)
{
    m_embeddedTargetId = targetId;
    setWatchingTarget(targetId);
    if (!targetId.isEmpty()) {
        janusSwitchToUserRoom(targetId);
        showAudioCallUi(targetId);
    }
    if (m_rightContentStack && m_videoContentPage) {
        m_rightContentStack->setCurrentWidget(m_videoContentPage);
    }
    if (!m_embeddedVideoWidget) {
        return;
    }
    m_embeddedVideoWidget->setAnnotationColorId(initialColorId);
    m_embeddedVideoWidget->setViewerName(viewerName);
    m_embeddedVideoWidget->setAudioOnlySession(false);
    m_embeddedVideoWidget->setSessionInfo(viewerId, targetId);
    m_embeddedVideoWidget->startReceiving(serverUrl);
    m_embeddedVideoWidget->setSpeakerEnabled(false);
    m_embeddedVideoWidget->setMicSendEnabled(false);
    m_embeddedVideoWidget->setTalkEnabled(false);
}

void NewUiWindow::stopEmbeddedWatching()
{
    const QString targetId = m_embeddedTargetId;
    m_embeddedTargetId.clear();
    setWatchingTarget(QString());
    if (!m_audioCallPeerId.isEmpty()) {
        hangupAudioCallUi();
    } else if (!m_myStreamId.isEmpty()) {
        janusSwitchToMyRoom();
    } else {
        janusStop();
    }
    if (m_embeddedVideoWidget && m_embeddedVideoWidget->isReceiving()) {
        m_embeddedVideoWidget->stopReceiving(false);
    }
    showHomeContent();
    if (!targetId.isEmpty()) {
        emit stopWatchingRequested(targetId);
    }
}

void NewUiWindow::setupUi()
{
    QString appDir = QCoreApplication::applicationDirPath();

    setObjectName("NewUiWindowRoot");
#ifdef _WIN32
    // Always enable TranslucentBackground for Windows to support Acrylic/Blur
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
#else
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NoSystemBackground, true);
#endif
#ifndef _WIN32
    setStyleSheet(
        "QWidget#NewUiWindowRoot {"
        "   background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0b0c10, stop:1 #141724);"
        "}"
    );
#endif


    bool isWin11 = false;
#ifdef _WIN32
    RTL_OSVERSIONINFOEXW osInfo = { sizeof(osInfo) };
    typedef LONG (WINAPI *RtlGetVersionPtr)(RTL_OSVERSIONINFOEXW*);
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod) {
        RtlGetVersionPtr pRtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
        if (pRtlGetVersion) {
            pRtlGetVersion(&osInfo);
            if (osInfo.dwMajorVersion > 10 || (osInfo.dwMajorVersion == 10 && osInfo.dwBuildNumber >= 22000)) {
                isWin11 = true;
            } else if (osInfo.dwMajorVersion == 10) {
                m_isWin10 = true;
            }
        }
    }
#endif

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(1, 1, 1, 1);
    mainLayout->setSpacing(20); // The "hollow" gap

    auto applyShadow = [](QWidget *w, int blurRadius, int yOffset, int alpha) {
        if (!w) return;
        auto *fx = new QGraphicsDropShadowEffect(w);
        fx->setBlurRadius(blurRadius);
        fx->setOffset(0, yOffset);
        fx->setColor(QColor(0, 0, 0, alpha));
        w->setGraphicsEffect(fx);
    };

    // --- Left Panel ---
    QWidget *leftPanel = new QWidget(this);
    leftPanel->setObjectName("LeftPanel");
    leftPanel->setFixedWidth(80);
    leftPanel->installEventFilter(this);
    // Use QSS for styling
    // Unified values for both Win10 and Win11 to ensure consistent look and shadow visibility

    const QString leftBgColor = isWin11 ? "rgba(45, 45, 48, 150)" : "rgba(45, 45, 48, 150)";
    leftPanel->setStyleSheet(
        "QWidget#LeftPanel {"
        "   background-color: " + leftBgColor + ";"
        "   border: 1px solid " + leftBgColor + ";"
        "   border-top-left-radius: 10px;"
        "   border-bottom-left-radius: 10px;"
        "   border-top-right-radius: 0px;"
        "   border-bottom-right-radius: 0px;"
        "}"
        "QWidget#LeftPanel QPushButton {"
        "   background-color: transparent;"
        "   border: 1px solid transparent;"
        "   border-radius: 20px;"
        "   margin: 5px;"
        "}"
        "QWidget#LeftPanel QPushButton:hover { background-color: rgba(255, 255, 255, 26); }"
        "QWidget#LeftPanel QPushButton:pressed { background-color: rgba(255, 255, 255, 38); }"
    );
    applyShadow(leftPanel, 30, 10, 135);

    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 20, 0, 20);
    leftLayout->setSpacing(10);
    leftLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    // App Logo
    m_logoLabel = new QLabel();
    m_logoLabel->setFixedSize(40, 40);
    m_logoLabel->setPixmap(QPixmap(appDir + "/maps/logo/iruler.ico").scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_logoLabel->setAlignment(Qt::AlignCenter);
    m_logoLabel->setCursor(Qt::PointingHandCursor);
    m_logoLabel->setToolTip("打开官网：http://www.iruler.cn");
    m_logoLabel->installEventFilter(this);
    leftLayout->addWidget(m_logoLabel);
    
    // Spacing between logo and buttons
    leftLayout->addSpacing(20);

    auto playIconBling = [](QPushButton *btn) {
        const QSize normal = btn->iconSize().isValid() ? btn->iconSize() : QSize(28, 28);
        const QSize down(qRound(normal.width() * 0.7857142857), qRound(normal.height() * 0.7857142857));
        const QSize up(qRound(normal.width() * 1.2142857143), qRound(normal.height() * 1.2142857143));

        QSequentialAnimationGroup *group = new QSequentialAnimationGroup(btn);

        QPropertyAnimation *anim1 = new QPropertyAnimation(btn, "iconSize");
        anim1->setDuration(100);
        anim1->setStartValue(normal);
        anim1->setEndValue(down);
        anim1->setEasingCurve(QEasingCurve::OutQuad);

        QPropertyAnimation *anim2 = new QPropertyAnimation(btn, "iconSize");
        anim2->setDuration(100);
        anim2->setStartValue(down);
        anim2->setEndValue(up);
        anim2->setEasingCurve(QEasingCurve::OutQuad);

        QPropertyAnimation *anim3 = new QPropertyAnimation(btn, "iconSize");
        anim3->setDuration(100);
        anim3->setStartValue(up);
        anim3->setEndValue(normal);
        anim3->setEasingCurve(QEasingCurve::OutElastic);

        group->addAnimation(anim1);
        group->addAnimation(anim2);
        group->addAnimation(anim3);

        connect(group, &QAbstractAnimation::finished, group, &QObject::deleteLater);
        group->start();
    };

    // Add vertical buttons to left panel
    for (int i = 0; i < 4; ++i) {
        QPushButton *btn = new QPushButton();
        btn->setFixedSize(40, 40);
        btn->setCursor(Qt::PointingHandCursor);
        if (i == 0) btn->setToolTip("主页");
        else if (i == 1) btn->setToolTip("故事板");
        else if (i == 2) btn->setToolTip("功能 2");
        else if (i == 3) btn->setToolTip("功能 3");
        btn->installEventFilter(this);
        
        if (i == 0) {
            btn->setObjectName("HomeButton");
            btn->setIcon(QIcon(appDir + "/maps/logo/tab1.png"));
            btn->setIconSize(QSize(28, 28));
            // Transparent background for the first button, no hover background
            btn->setStyleSheet(
                "QPushButton#HomeButton {"
                "   background-color: transparent;"
                "   border: none;"
                "   border-radius: 20px;"
                "}"
                "QPushButton#HomeButton:hover { background-color: rgba(255, 255, 255, 26); }"
                "QPushButton#HomeButton:pressed { background-color: rgba(255, 255, 255, 38); }"
            );
            
            // Add click animation (Bling effect: Scale down -> Scale up -> Restore)
            connect(btn, &QPushButton::clicked, [this, btn, playIconBling]() {
                playIconBling(btn);
                showHomeContent();
            });
        }
        else if (i == 1) {
            btn->setObjectName("Function1Button");
            btn->setIcon(QIcon(appDir + "/maps/logo/Storyboard.png"));
            btn->setIconSize(QSize(28, 28));
            btn->setStyleSheet(
                "QPushButton#Function1Button {"
                "   background-color: transparent;"
                "   border: none;"
                "   border-radius: 20px;"
                "}"
                "QPushButton#Function1Button:hover { background-color: rgba(255, 255, 255, 26); }"
                "QPushButton#Function1Button:pressed { background-color: rgba(255, 255, 255, 38); }"
            );

            connect(btn, &QPushButton::clicked, [this, btn, playIconBling]() {
                playIconBling(btn);
                showFunction1Browser();
            });
        }
        else if (i == 2) {
            btn->setObjectName("Function2Button");
            btn->setIcon(QIcon(appDir + "/maps/logo/wangye.png"));
            btn->setIconSize(QSize(28, 28));
            btn->setStyleSheet(
                "QPushButton#Function2Button {"
                "   background-color: transparent;"
                "   border: none;"
                "   border-radius: 20px;"
                "}"
                "QPushButton#Function2Button:hover { background-color: rgba(255, 255, 255, 26); }"
                "QPushButton#Function2Button:pressed { background-color: rgba(255, 255, 255, 38); }"
            );

            connect(btn, &QPushButton::clicked, [this, btn, playIconBling]() {
                playIconBling(btn);
                if (m_function2WebView) {
                    QString v = AppConfig::readConfigValue(QStringLiteral("function2_url")).trimmed();
                    if (v.isEmpty()) {
                        m_function2WebView->setHtml(
                            QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\" /></head>"
                                           "<body style=\"background:#404040;color:#e0e0e0;font-family:sans-serif;padding:18px;\">"
                                           "请在系统设置-配置中填写“功能2网址”"
                                           "</body></html>"));
                    } else {
                        m_function2WebView->load(QUrl::fromUserInput(v));
                    }
                }
                if (m_rightContentStack && m_function2BrowserPage) {
                    m_rightContentStack->setCurrentWidget(m_function2BrowserPage);
                }
            });
        }
        else if (i == 3) {
            btn->setObjectName("Function3Button");
            btn->setIcon(QIcon(appDir + "/maps/logo/wangye.png"));
            btn->setIconSize(QSize(28, 28));
            btn->setStyleSheet(
                "QPushButton#Function3Button {"
                "   background-color: transparent;"
                "   border: none;"
                "   border-radius: 20px;"
                "}"
                "QPushButton#Function3Button:hover { background-color: rgba(255, 255, 255, 26); }"
                "QPushButton#Function3Button:pressed { background-color: rgba(255, 255, 255, 38); }"
            );

            connect(btn, &QPushButton::clicked, [this, btn, playIconBling]() {
                playIconBling(btn);

                // [Fix] Reset maintenance state
                if (m_function3BrowserPage) {
                    if (QLabel *l = m_function3BrowserPage->findChild<QLabel*>("MaintenanceLabel")) l->setVisible(false);
                }

                if (m_function3WebView) {
                    m_function3WebView->setVisible(true);
                    QString v = AppConfig::readConfigValue(QStringLiteral("function3_url")).trimmed();
                    if (v.isEmpty()) {
                        m_function3WebView->setHtml(
                            QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\" /></head>"
                                           "<body style=\"background:#404040;color:#e0e0e0;font-family:sans-serif;padding:18px;\">"
                                           "请在系统设置-配置中填写“功能3网址”"
                                           "</body></html>"));
                    } else {
                        m_function3WebView->load(QUrl::fromUserInput(v));
                    }
                }
                if (m_rightContentStack && m_function3BrowserPage) {
                    m_rightContentStack->setCurrentWidget(m_function3BrowserPage);
                }
            });
        }
        
        leftLayout->addWidget(btn);
    }
    
    leftLayout->addStretch();
    
    // Bottom setting button
    QPushButton *settingBtn = new QPushButton();
    settingBtn->setObjectName("SettingButton");
    settingBtn->setFixedSize(40, 40);
    settingBtn->setCursor(Qt::PointingHandCursor);
    settingBtn->setToolTip("设置");
    settingBtn->setIcon(QIcon(appDir + "/maps/logo/menu.png"));
    settingBtn->setIconSize(QSize(28, 28));
    settingBtn->setStyleSheet(
        "QPushButton#SettingButton {"
        "   background-color: transparent;"
        "   border: none;"
        "   border-radius: 20px;"
        "}"
        "QPushButton#SettingButton:hover { background-color: rgba(255, 255, 255, 26); }"
        "QPushButton#SettingButton:pressed { background-color: rgba(255, 255, 255, 38); }"
    );
    settingBtn->installEventFilter(this);
    connect(settingBtn, &QPushButton::clicked, this, &NewUiWindow::systemSettingsRequested);
    leftLayout->addWidget(settingBtn);

    // --- Right Panel ---
    QWidget *rightPanel = new QWidget(this);
    rightPanel->setObjectName("RightPanel");
    
    // Win10 uses white background, Win11 uses translucent dark background (original style)
    // isWin11 is already calculated at the beginning of setupUi()
    const QString rightBgColor = isWin11 ? "rgba(45, 45, 48, 150)" : "rgba(45, 45, 48, 150)";

    rightPanel->setStyleSheet(
        "QWidget#RightPanel {"
        "   background-color: " + rightBgColor + ";"
        "   border: 1px solid rgba(255, 255, 255, 16);"
        "   border-top-left-radius: 0px;"
        "   border-bottom-left-radius: 0px;"
        "   border-top-right-radius: 10px;"
        "   border-bottom-right-radius: 10px;"
        "}"
    );
    applyShadow(rightPanel, 38, 12, 150);

    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(10, 10, 10, 10); // Reduced margins (was 40, 10, 40, 40) to expand content
    rightLayout->setSpacing(10);

    // Title Bar Area
    QWidget *titleBar = new QWidget(rightPanel);
    titleBar->setFixedHeight(50); // Increase height to accommodate larger buttons
    titleBar->setStyleSheet("background-color: transparent;");
    m_titleBar = titleBar;
    titleBar->installEventFilter(this);
    
    QHBoxLayout *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(0, 0, 0, 0);

    QFrame *toolsContainer = new QFrame(titleBar);
    toolsContainer->setObjectName("ToolsContainer");
    toolsContainer->setFixedSize(160, 30);
    toolsContainer->setFrameShape(QFrame::NoFrame);
    toolsContainer->installEventFilter(this);
    toolsContainer->setStyleSheet(
        "#ToolsContainer {"
        "   background-color: rgba(90, 90, 96, 160);"
        "   border: 1px solid rgba(255, 255, 255, 18);"
        "   border-radius: 15px;"
        "}"
        "QPushButton {"
        "   background-color: transparent;"
        "   border: none;"
        "   margin: 3px;"
        "}"
        "QPushButton:hover {"
        "   background-color: rgba(255, 255, 255, 28);"
        "   border-radius: 12px;"
        "}"
        "QPushButton:pressed {"
        "   background-color: rgba(255, 255, 255, 40);"
        "}"
    );

    QHBoxLayout *toolsLayout = new QHBoxLayout(toolsContainer);
    toolsLayout->setContentsMargins(10, 0, 10, 0);
    toolsLayout->setSpacing(5);
    toolsLayout->setAlignment(Qt::AlignCenter);

    ResponsiveButton *toolBtn1 = new ResponsiveButton();
    toolBtn1->setObjectName("ToolLocalDraw");
    toolBtn1->setFixedSize(30, 30);
    toolBtn1->setIcon(QIcon(appDir + "/maps/logo/d.png"));
    toolBtn1->setIconSize(QSize(24, 24));
    toolBtn1->setCursor(Qt::PointingHandCursor);
    toolBtn1->setToolTip("本地绘制");
    toolBtn1->installEventFilter(this);
    connect(toolBtn1, &QPushButton::clicked, this, &NewUiWindow::toggleStreamingIslandRequested);

    ResponsiveButton *toolBtn2 = new ResponsiveButton();
    toolBtn2->setObjectName("ToolTask");
    toolBtn2->setFixedSize(30, 30);
    toolBtn2->setIcon(QIcon(appDir + "/maps/logo/log.png"));
    toolBtn2->setIconSize(QSize(24, 24));
    toolBtn2->setCursor(Qt::PointingHandCursor);
    toolBtn2->setToolTip("任务");
    toolBtn2->installEventFilter(this);
    connect(toolBtn2, &QPushButton::clicked, this, &NewUiWindow::onBroadcastBtnClicked);

    ResponsiveButton *toolBtn3 = new ResponsiveButton();
    toolBtn3->setObjectName("ToolClear");
    toolBtn3->setFixedSize(30, 30);
    toolBtn3->setIcon(QIcon(appDir + "/maps/logo/clearn.png"));
    toolBtn3->setIconSize(QSize(24, 24));
    toolBtn3->setCursor(Qt::PointingHandCursor);
    toolBtn3->setToolTip("清空标注");
    toolBtn3->installEventFilter(this);
    connect(toolBtn3, &QPushButton::clicked, this, &NewUiWindow::clearMarksRequested);

    toolsLayout->addWidget(toolBtn1);
    toolsLayout->addWidget(toolBtn2);
    toolsLayout->addWidget(toolBtn3);

    titleLayout->addSpacing(8);
    titleLayout->addWidget(toolsContainer);

    // Meeting Button Container
    QFrame *meetingContainer = new QFrame(titleBar);
    meetingContainer->setObjectName("MeetingContainer");
    meetingContainer->setFixedSize(60, 40); 
    meetingContainer->setFrameShape(QFrame::NoFrame);
    meetingContainer->installEventFilter(this);
    meetingContainer->setStyleSheet(
        "#MeetingContainer {"
        "   background-color: rgba(90, 90, 96, 160);"
        "   border: 1px solid rgba(255, 255, 255, 18);"
        "   border-radius: 20px;"
        "}"
        "QPushButton {"
        "   background-color: transparent;"
        "   border: none;"
        "   margin: 3px;"
        "}"
        "QPushButton:hover {"
        "   background-color: rgba(255, 255, 255, 28);"
        "   border-radius: 17px;"
        "}"
        "QPushButton:pressed {"
        "   background-color: rgba(255, 255, 255, 40);"
        "}"
    );

    QHBoxLayout *meetingLayout = new QHBoxLayout(meetingContainer);
    meetingLayout->setContentsMargins(5, 0, 5, 0);
    meetingLayout->setAlignment(Qt::AlignCenter);

    ResponsiveButton *meetingBtn = new ResponsiveButton();
    meetingBtn->setFixedSize(40, 40);
    meetingBtn->setIcon(QIcon(appDir + "/maps/logo/Meeting.png"));
    meetingBtn->setIconSize(QSize(24, 24));
    meetingBtn->setCursor(Qt::PointingHandCursor);
    meetingBtn->setToolTip("发起会议邀请");
    meetingBtn->installEventFilter(this);
    connect(meetingBtn, &QPushButton::clicked, this, &NewUiWindow::onMeetingBtnClicked);

    meetingLayout->addWidget(meetingBtn);
    
    titleLayout->addSpacing(8);
    titleLayout->addWidget(meetingContainer);
    titleLayout->addStretch();

    titleLayout->addStretch();

    QPushButton *callRestoreBtn = new QPushButton(titleBar);
    callRestoreBtn->setText(QStringLiteral("通话"));
    callRestoreBtn->setFixedHeight(26);
    callRestoreBtn->setCursor(Qt::PointingHandCursor);
    callRestoreBtn->setVisible(false);
    callRestoreBtn->setStyleSheet(QStringLiteral(
        "QPushButton{"
        " background: rgba(0, 92, 54, 180);"
        " color: rgba(240,240,240,230);"
        " border: 1px solid rgba(0,0,0,60);"
        " border-radius: 10px;"
        " padding: 0 12px;"
        "}"
        "QPushButton:hover{ background: rgba(0, 92, 54, 210); }"
        "QPushButton:pressed{ background: rgba(0, 92, 54, 235); }"));
    connect(callRestoreBtn, &QPushButton::clicked, this, [this]() {
        setAudioCallMiniHidden(false);
        hideAudioCallMiniBar();
        if (m_audioCallDialog && !m_audioCallPeerId.isEmpty()) {
            m_audioCallDialog->show();
            m_audioCallDialog->raise();
        }
    });
    m_audioCallTitleRestoreBtn = callRestoreBtn;
    titleLayout->addWidget(callRestoreBtn, 0, Qt::AlignCenter);
    titleLayout->addStretch();

    QWidget *controlContainer = new QWidget(titleBar);
    // Size adjustment:
    // Buttons: 48x48 (Double size)
    // Container width: 48*5 = 240. Height: 48.
    controlContainer->setFixedSize(144, 48); 
    // Important: Ensure the widget itself doesn't paint a background, only the stylesheet image
    controlContainer->setAttribute(Qt::WA_TranslucentBackground);
    controlContainer->setObjectName("TitleControlContainer");
    controlContainer->installEventFilter(this);
    controlContainer->setStyleSheet(
        "QPushButton {"
        "   background-color: transparent;"
        "   border: none;"
        "}"
        "QPushButton:hover {"
        "   background-color: transparent;"
        "}"
        "QPushButton:pressed {"
        "   background-color: transparent;"
        "}"
    );
    
    // appDir is already defined at top of function

    QHBoxLayout *controlLayout = new QHBoxLayout(controlContainer);
    controlLayout->setContentsMargins(0, 0, 0, 0); // No margins
    controlLayout->setSpacing(0); // No spacing
    controlLayout->setAlignment(Qt::AlignCenter);

    // Minimize Button
    ResponsiveButton *minBtn = new ResponsiveButton();
    minBtn->setFixedSize(48, 48); 
    minBtn->setIcon(QIcon(appDir + "/maps/logo/mini.png"));
    minBtn->setIconSize(QSize(32, 32)); 
    minBtn->setCursor(Qt::PointingHandCursor);
    minBtn->setToolTip("最小化");
    minBtn->installEventFilter(this);
    connect(minBtn, &QPushButton::clicked, this, &QWidget::showMinimized);

    ResponsiveButton *maxBtn = new ResponsiveButton();
    maxBtn->setFixedSize(48, 48);
    maxBtn->setIconSize(QSize(32, 32));
    maxBtn->setCursor(Qt::PointingHandCursor);
    maxBtn->installEventFilter(this);
    connect(maxBtn, &QPushButton::clicked, this, &NewUiWindow::toggleFunction1Maximize);
    m_titleMaximizeBtn = maxBtn;
    updateTitleMaximizeButton();

    // Close Button
    ResponsiveButton *closeBtn = new ResponsiveButton();
    closeBtn->setFixedSize(48, 48); 
    closeBtn->setIcon(QIcon(appDir + "/maps/logo/close.png"));
    closeBtn->setIconSize(QSize(32, 32)); 
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setToolTip("关闭");
    closeBtn->installEventFilter(this);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

    controlLayout->addWidget(minBtn);
    controlLayout->addWidget(maxBtn);
    controlLayout->addWidget(closeBtn);

    titleLayout->addWidget(controlContainer);
    titleLayout->addSpacing(20);

    m_rightContentStack = new QStackedWidget(rightPanel);
    m_rightContentStack->setObjectName("RightContentStack");
    m_rightContentStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_rightContentStack->setAttribute(Qt::WA_TranslucentBackground);
    m_rightContentStack->setAutoFillBackground(false);
    m_rightContentStack->setStyleSheet(
        "QStackedWidget#RightContentStack {"
        "   background: transparent;"
        "   border: none;"
        "}"
    );

    // Content Area (Image Matrix)
    // Wrap QListWidget in a container to handle rounded corners + scrollbar issue
    QFrame *listContainer = new QFrame(rightPanel);
    listContainer->setObjectName("ListContainer");
    listContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    listContainer->setStyleSheet(
        "#ListContainer {"
        "   background-color: rgba(26, 26, 30, 110);"
        "   border: 1px solid rgba(255, 255, 255, 14);"
        "   border-radius: 20px;"
        "}"
    );
    QVBoxLayout *listContainerLayout = new QVBoxLayout(listContainer);
    listContainerLayout->setContentsMargins(15, 15, 5, 15); // Right margin smaller for scrollbar, others for spacing
    
    // Constants moved to member variables initialized in constructor

    m_listWidget = new QListWidget(listContainer);
    m_listWidget->setViewMode(QListWidget::IconMode);
    // [Fix] Disable dragging to prevent process freeze
    m_listWidget->setMovement(QListView::Static);
    m_listWidget->setDragEnabled(false);
    // Adjust icon size to fit the card widget (roughly card size)
    // Use the global TOTAL size calculated above
    m_listWidget->setIconSize(QSize(m_totalItemWidth, m_totalItemHeight)); 
    m_listWidget->setSpacing(15); // Expanded spacing (was 3)
    m_listWidget->setResizeMode(QListWidget::Adjust);
    // [Scroll Settings] Smooth scrolling settings
    m_listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listWidget->verticalScrollBar()->setSingleStep(10); // Scroll 10 pixels at a time
    // Remove default border and background to blend in
    m_listWidget->setFrameShape(QFrame::NoFrame);
    m_listWidget->viewport()->installEventFilter(this);

    // [Context Menu] Right-click menu with rounded corners
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listWidget, &QListWidget::customContextMenuRequested, [this](const QPoint &pos) {
        QListWidgetItem *item = m_listWidget->itemAt(pos);
        if (!item) return; // Only show menu on items
        const int row = m_listWidget->row(item);
        QString itemUserId = item->data(Qt::UserRole).toString();
        if (itemUserId.isEmpty() && row == 0) {
            itemUserId = m_myStreamId;
        }

        QMenu contextMenu(m_listWidget);
        // Enable transparency for rounded corners
        contextMenu.setAttribute(Qt::WA_TranslucentBackground);
        contextMenu.setWindowFlags(contextMenu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        
        contextMenu.setStyleSheet(
            "QMenu {"
        "    background-color: rgba(29, 29, 35, 210);" /* Brightened from 24,24,29 */
        "    border: 1px solid rgba(255, 255, 255, 22);"
        "    border-radius: 12px;"
            "    padding: 6px;"
            "    color: #e0e0e0;"
            "    font-size: 13px;"
            "}"
            "QMenu::item {"
            "    background-color: transparent;"
            "    padding: 8px 24px;"
            "    margin: 2px 4px;"
            "    border-radius: 6px;"
            "}"
            "QMenu::item:selected {"
            "    background-color: #0078d4;" // Windows blue style
            "    color: white;"
            "}"
            "QMenu::separator {"
            "    height: 1px;"
            "    background: rgba(255, 255, 255, 18);"
            "    margin: 4px 10px;"
            "}"
        );

        /*
        // [Fix] Kick menu removed as requested
        if (!itemUserId.isEmpty() && itemUserId == m_myStreamId && row == 0) {
            const QStringList viewerIds = getViewerIds();
            QMenu *kickMenu = contextMenu.addMenu(QStringLiteral("踢出观看者"));
            kickMenu->setStyleSheet(contextMenu.styleSheet());
            if (viewerIds.isEmpty()) {
                QAction *none = kickMenu->addAction(QStringLiteral("暂无观看者"));
                none->setEnabled(false);
            } else {
                QAction *kickAll = contextMenu.addAction(QStringLiteral("踢出全部观看者"));
                connect(kickAll, &QAction::triggered, this, [this, viewerIds]() {
                    for (const QString &viewerId : viewerIds) {
                        emit kickViewerRequested(viewerId);
                    }
                });
                contextMenu.addSeparator();
                for (const QString &viewerId : viewerIds) {
                    QString display = viewerId;
                    if (QListWidgetItem *vit = m_viewerItems.value(viewerId, nullptr)) {
                        if (QWidget *vw = m_viewerList ? m_viewerList->itemWidget(vit) : nullptr) {
                            const QList<QLabel*> labels = vw->findChildren<QLabel*>();
                            if (!labels.isEmpty() && labels.first() && !labels.first()->text().isEmpty()) {
                                display = labels.first()->text();
                            }
                        }
                    }
                    QAction *a = kickMenu->addAction(display);
                    connect(a, &QAction::triggered, this, [this, viewerId]() {
                        emit kickViewerRequested(viewerId);
                    });
                }
            }
        } else {
            if (!itemUserId.isEmpty() && isInMyRoomViewerList(itemUserId)) {
                QAction *kickOne = contextMenu.addAction(QStringLiteral("踢出"));
                connect(kickOne, &QAction::triggered, this, [this, itemUserId]() {
                    emit kickViewerRequested(itemUserId);
                });
            } else {
                return;
            }
        }
        */
        if (itemUserId.isEmpty() && row != 0) {
            return;
        }

        contextMenu.exec(m_listWidget->mapToGlobal(pos));
    });
    m_listWidget->setStyleSheet(
        "QListWidget {"
        "   background-color: transparent;"
        "   outline: none;"
        "   border: none;"
        "}"
        "QListWidget::item {"
        "   background-color: transparent;" // Items handle their own background
        "   padding: 0px;" 
        "}"
        "QListWidget::item:selected {"
        "   background-color: transparent;" // Disable default selection rect
        "}"
        "QListWidget::item:hover {"
        "   background-color: transparent;"
        "}"
    );
    
    // Vertical ScrollBar Styling
    m_listWidget->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical {"
        "    border: none;"
        "    background: transparent;" // Transparent track
        "    width: 8px;"
        "    margin: 0px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: #666;"
        "    min-height: 20px;"
        "    border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "    background: #888;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "    border: none;"
        "    background: none;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "    background: none;"
        "}"
    );

    // ScrollBar styling ends here

    /*
    // Add dummy items
    QString imgPath = appDir + "/maps/t.png";
    QPixmap srcPix(imgPath);
    // If loading fails, create a fallback
    if (srcPix.isNull()) {
        srcPix = QPixmap(m_imgWidth, m_imgHeight); // Use calculated size
        srcPix.fill(Qt::darkGray);
    }
    */

    // Connect double click to watch request
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        QString userId = item->data(Qt::UserRole).toString();
        // Ignore if it's local user (empty or self ID) or invalid
        if (!userId.isEmpty() && userId != m_myStreamId) {
             QString name = item->data(Qt::UserRole + 1).toString();
             // Fallback to widget property if data not set
             if (name.isEmpty()) {
                 if (QWidget *iw = m_listWidget->itemWidget(item)) {
                     if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                         name = card->property("userName").toString();
                     }
                 }
             }
             if (name.isEmpty()) {
                 name = userId; // Fallback to ID if name is missing
             }
             emit startWatchingRequested(userId, name);
        }
    });

    m_hiFpsWatchdogTimer = new QTimer(this);
    m_hiFpsWatchdogTimer->setInterval(1500);
    connect(m_hiFpsWatchdogTimer, &QTimer::timeout, this, [this]() {
        if (m_hiFpsActiveUserId.isEmpty() || m_hiFpsActiveChannelId.isEmpty()) {
            return;
        }
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const bool connected = (m_hiFpsSubscriber && m_hiFpsSubscriber->isConnected());
        const bool shouldNudge = (m_hiFpsLastFrameAtMs > 0 && (nowMs - m_hiFpsLastFrameAtMs) >= 2500);
        if (connected && shouldNudge) {
            QJsonObject start;
            start["type"] = "start_streaming";
            m_hiFpsSubscriber->sendTextMessage(QJsonDocument(start).toJson(QJsonDocument::Compact));
            sendHiFpsControl(m_hiFpsActiveUserId, m_hiFpsActiveChannelId, 10, true);
        } else if (!connected && shouldNudge) {
            sendHiFpsControl(m_hiFpsActiveUserId, m_hiFpsActiveChannelId, 10, true);
        }

        if (m_hiFpsLastFrameAtMs > 0 && (nowMs - m_hiFpsLastFrameAtMs) >= 8000) {
            if (m_hiFpsLastRecoveryAtMs == 0 || (nowMs - m_hiFpsLastRecoveryAtMs) >= 8000) {
                m_hiFpsLastRecoveryAtMs = nowMs;
                const QString userId = m_hiFpsActiveUserId;
                stopHiFpsForUser();
                restartUserStreamSubscription(userId);
                startHiFpsForUser(userId);
            }
        }
    });

    connect(m_listWidget, &QListWidget::itemPressed, this, [this](QListWidgetItem *item) {
        if (!item || QGuiApplication::mouseButtons() != Qt::RightButton) return;
        
        QString userId = item->data(Qt::UserRole).toString();
        if (userId.isEmpty()) {
            if (QWidget *iw = m_listWidget->itemWidget(item)) {
                if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                    userId = card->property("userId").toString();
                } else {
                    userId = iw->property("userId").toString();
                }
            }
        }
        
        if (userId == m_myStreamId) {
            QMenu menu(this);
            menu.setStyleSheet(
                "QMenu { background-color: rgb(45, 45, 48); border: 1px solid rgb(60, 60, 60); color: white; padding: 5px; }"
                "QMenu::item { padding: 5px 20px; border-radius: 4px; }"
                "QMenu::item:selected { background-color: rgba(255, 255, 255, 30); }"
            );
            QAction *privacyAction = menu.addAction(m_isPrivacyMode ? QStringLiteral("关闭隐私时间") : QStringLiteral("开启隐私时间"));
            connect(privacyAction, &QAction::triggered, this, [this]() {
                togglePrivacyMode(!m_isPrivacyMode);
            });

            QAction *cameraAction = menu.addAction(m_isCameraMode ? QStringLiteral("切换到屏幕") : QStringLiteral("切换到摄像头"));
            connect(cameraAction, &QAction::triggered, this, [this]() {
                if (m_isCameraMode) {
                    m_isCameraMode = false;
                    stopCamera();
                } else {
                    // 尝试切换到摄像头模式
                    m_isCameraMode = true;
                    ensureCameraStarted();
                    
                    // 如果 ensureCameraStarted 检测不到设备，会弹窗并将 m_isCameraMode 设为 false
                    if (!m_isCameraMode) {
                    }
                }
                m_lastPreviewFramePixmap = QPixmap();
                m_lastPreviewSendPixmap = QPixmap();
                m_lastPreviewCaptureAtMs = 0;
                publishLocalScreenFrameTriggered("camera_switch", true, true);
            });
            menu.exec(QCursor::pos());
        }
    });

    connect(m_listWidget, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current, QListWidgetItem *previous) {
        Q_UNUSED(previous);
        QString userId;
        if (current) {
            userId = current->data(Qt::UserRole).toString();
            if (userId.isEmpty()) {
                if (QWidget *iw = m_listWidget->itemWidget(current)) {
                    if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                        userId = card->property("userId").toString();
                    } else {
                        userId = iw->property("userId").toString();
                    }
                }
            }
        }
        if (userId.isEmpty()) {
            stopSelfPreviewFast();
            cancelHoverHiFps();
            resetSelectionAutoPause(QString());
            return;
        }
        if (userId == m_myStreamId) {
            cancelHoverHiFps();
            resetSelectionAutoPause(QString());
            startSelfPreviewFast();
            return;
        }
        stopSelfPreviewFast();
        if (QApplication::applicationState() != Qt::ApplicationActive) {
            cancelHoverHiFps();
            resetSelectionAutoPause(QString());
            return;
        }
        if (userId == m_autoPausedUserId) {
            resumeSelectedStreamForUser(userId);
        }
        
        startHiFpsForUser(userId);
        resetSelectionAutoPause(userId);
    });

    connect(m_listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        QString userId = item->data(Qt::UserRole).toString();
        if (userId.isEmpty()) {
            if (QWidget *iw = m_listWidget->itemWidget(item)) {
                if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                    userId = card->property("userId").toString();
                } else {
                    userId = iw->property("userId").toString();
                }
            }
        }
        if (userId.isEmpty() || userId == m_myStreamId) {
            return;
        }
        if (QApplication::applicationState() != Qt::ApplicationActive) {
            return;
        }
        if (userId == m_autoPausedUserId) {
            resumeSelectedStreamForUser(userId);
        }
        startHiFpsForUser(userId);
        resetSelectionAutoPause(userId);
    });

    connect(m_listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!m_farRightPanel || !m_farRightPanel->isVisible() || !item) {
            return;
        }

        QString userId = item->data(Qt::UserRole).toString();
        if (userId.isEmpty()) {
            if (QWidget *iw = m_listWidget->itemWidget(item)) {
                if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                    userId = card->property("userId").toString();
                } else {
                    userId = iw->property("userId").toString();
                }
            }
        }

        if (userId.isEmpty() || userId == m_myStreamId) {
            return;
        }
        m_farRightPanel->setVisible(false);
    });

    // Create Local User Item (Index 0)
    {
        QListWidgetItem *item = new QListWidgetItem(m_listWidget);
        // Size hint must cover the widget size + shadow margins
        
        // Use the global TOTAL size
        item->setSizeHint(QSize(m_totalItemWidth, m_totalItemHeight)); 
        
        // Create the Item Widget (Container for the card)
        QWidget *itemWidget = new QWidget();
        itemWidget->setAttribute(Qt::WA_TranslucentBackground);
        QVBoxLayout *itemLayout = new QVBoxLayout(itemWidget);
        // Margins for shadow
        itemLayout->setContentsMargins(m_shadowSize, m_shadowSize, m_shadowSize, m_shadowSize);
        itemLayout->setSpacing(0);

        // The Card Frame (Visible Part)
        QFrame *card = new QFrame();
        card->setObjectName("CardFrame");
        
        // [Interaction Fix] Store local card pointer and install event filter
        m_localCard = card;
        card->installEventFilter(this);
        card->setProperty("userId", m_myStreamId); // Might be empty initially, updated in setMyStreamId

        card->setStyleSheet(
            "#CardFrame {"
            "   background-color: rgba(32, 32, 36, 175);"
            "   border: 1px solid rgba(255, 255, 255, 22);"
            "   border-radius: 15px;"
            "}"
            "#CardFrame:hover {"
            "   background-color: rgba(40, 40, 45, 190);"
            "}"
        );
        
        // Shadow Effect
        QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect();
        shadow->setBlurRadius(18); 
        shadow->setColor(QColor(0, 0, 0, 140));
        shadow->setOffset(0, 6);
        card->setGraphicsEffect(shadow);

        QVBoxLayout *cardLayout = new QVBoxLayout(card);
        // Padding creates the visible border around the image
        // Top: Centering margin, Sides: Centering margin, Bottom: 0 (Controls area handles its own padding)
        cardLayout->setContentsMargins(m_marginX, m_marginTop, m_marginX, 0);
        cardLayout->setSpacing(0); 

        // Image Label
        QLabel *imgLabel = new QLabel();
        // Width matches the calculated image width
        imgLabel->setFixedSize(m_imgWidth, m_imgHeight); 
        imgLabel->setAlignment(Qt::AlignCenter);

        // Capture for video updates
        m_videoLabel = imgLabel;
        
        // Initial placeholder capture
        QScreen *screen = QGuiApplication::primaryScreen();
        QPixmap srcPix;
        if (screen) {
             QPixmap original = screen->grabWindow(0,
                 screen->geometry().x(), screen->geometry().y(),
                 screen->size().width(), screen->size().height());
             original.setDevicePixelRatio(1.0);
             srcPix = original.scaledToWidth(m_cardBaseWidth, Qt::SmoothTransformation);
        } else {
             srcPix = QPixmap(m_cardBaseWidth, (int)(m_cardBaseWidth/1.77));
             srcPix.fill(Qt::black);
        }

        // Process Image (Rounded Corners)
        QPixmap pixmap(m_imgWidth, m_imgHeight); 
        pixmap.setDevicePixelRatio(1.0);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        
        QPixmap scaledPix = srcPix.scaled(m_imgWidth, m_imgHeight, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        // Center crop
        int x = (m_imgWidth - scaledPix.width()) / 2;
        int y = (m_imgHeight - scaledPix.height()) / 2;
        
        QPainterPath path;
        // All corners rounded to match the inner look
        path.addRoundedRect(0, 0, m_imgWidth, m_imgHeight, 8, 8);
        p.setClipPath(path);
        
        p.drawPixmap(x, y, scaledPix);
        p.end();
        imgLabel->setPixmap(pixmap);

        QWidget *imageContainer = new QWidget();
        imageContainer->setFixedSize(m_imgWidth, m_imgHeight);
        imgLabel->setParent(imageContainer);
        imgLabel->move(0, 0);

        QLabel *avatarLabel = new QLabel(imageContainer);
        avatarLabel->setFixedSize(30, 30);
        avatarLabel->move(6, 6);
        avatarLabel->setAlignment(Qt::AlignCenter);
        avatarLabel->setCursor(Qt::PointingHandCursor);
        avatarLabel->setToolTip(QStringLiteral("更换头像"));
        avatarLabel->installEventFilter(this);
        avatarLabel->setStyleSheet(
            "QLabel {"
            "   background: transparent;"
            "   border: none;"
            "}"
        );
        QPixmap avatarPix = buildTestAvatarPixmap(30);
        if (!avatarPix.isNull()) {
            avatarLabel->setPixmap(avatarPix);
        }
        m_localAvatarLabel = avatarLabel;
        if (!m_myStreamId.isEmpty()) {
            m_userAvatarLabels.insert(m_myStreamId, m_localAvatarLabel);
        }

    QLabel *watchedOverlay = new QLabel(imageContainer);
        watchedOverlay->setText(QString());
        watchedOverlay->setAlignment(Qt::AlignCenter);
        watchedOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
        watchedOverlay->setGeometry(0, 0, m_imgWidth, m_imgHeight);
        watchedOverlay->setStyleSheet("color: rgba(255, 255, 255, 235); font-size: 34px; font-weight: bold; background-color: rgba(0, 120, 212, 95); border-radius: 8px;");
        watchedOverlay->setVisible(false);
        m_localWatchedOverlay = watchedOverlay;

        // Bottom Controls Layout
        QHBoxLayout *bottomLayout = new QHBoxLayout();
        // Zero side margins because parent cardLayout already provides MARGIN_X
        // But we might want buttons to extend a bit wider? No, keep alignment.
        // Add a bit of bottom padding
        bottomLayout->setContentsMargins(0, 0, 0, 5); 
        bottomLayout->setSpacing(5);

        // Text Label (Middle)
    QString displayName = m_myUserName.isEmpty() ? m_myStreamId : m_myUserName;
    // Format: Name only (ID removed as requested)
    QString fullText = displayName;
    QLabel *txtLabel = new QLabel(fullText);
    txtLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_localNameLabel = txtLabel; // Store pointer for updates
    txtLabel->setStyleSheet("color: #e0e0e0; font-size: 12px; border: none; background: transparent;");
    txtLabel->setAlignment(Qt::AlignCenter);
        bottomLayout->addStretch();
        bottomLayout->addWidget(txtLabel);
        bottomLayout->addStretch();

        cardLayout->addWidget(imageContainer);
        cardLayout->addLayout(bottomLayout);

        itemLayout->addWidget(card);
        
        m_listWidget->setItemWidget(item, itemWidget);
    }

    listContainerLayout->addWidget(m_listWidget);
    m_homeContentPage = listContainer;
    m_rightContentStack->addWidget(m_homeContentPage);

    QFrame *browserContainer = new QFrame(rightPanel);
    browserContainer->setObjectName("Function1BrowserContainer");
    browserContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    browserContainer->setStyleSheet(
        "#Function1BrowserContainer {"
        "   background-color: rgba(26, 26, 30, 110);"
        "   border: 1px solid rgba(255, 255, 255, 14);"
        "   border-radius: 20px;"
        "}"
    );
    QVBoxLayout *browserLayout = new QVBoxLayout(browserContainer);
    browserLayout->setContentsMargins(0, 0, 0, 0);
    browserLayout->setSpacing(0);

    QWebEngineProfile *profile = ensureWebEngineProfileConfigured();
    m_function1WebView = new QWebEngineView(browserContainer);
    auto *storyboardPage = new StoryboardWebPage(profile, this, m_function1WebView);
    m_function1WebView->setPage(storyboardPage);
    connect(m_function1WebView, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok) return;
        injectWebCredentialAndAutofill(m_function1WebView);
    });

    ensureJanusAudioLoaded();
    browserLayout->addWidget(m_function1WebView);

    m_function1BrowserPage = browserContainer;
    m_rightContentStack->addWidget(m_function1BrowserPage);

    // --- Function 2 Browser ---
    QFrame *browserContainer2 = new QFrame(rightPanel);
    browserContainer2->setObjectName("Function2BrowserContainer");
    browserContainer2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    browserContainer2->setStyleSheet(
        "#Function2BrowserContainer {"
        "   background-color: rgba(26, 26, 30, 110);"
        "   border: 1px solid rgba(255, 255, 255, 14);"
        "   border-radius: 20px;"
        "}"
    );
    QVBoxLayout *browserLayout2 = new QVBoxLayout(browserContainer2);
    browserLayout2->setContentsMargins(0, 0, 0, 0);
    browserLayout2->setSpacing(0);

    m_function2WebView = new QWebEngineView(browserContainer2);
    auto *page2 = new StoryboardWebPage(profile, this, m_function2WebView);
    m_function2WebView->setPage(page2);
    connect(m_function2WebView, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok) return;
        injectWebCredentialAndAutofill(m_function2WebView);
    });
    browserLayout2->addWidget(m_function2WebView);
    m_function2BrowserPage = browserContainer2;
    m_rightContentStack->addWidget(m_function2BrowserPage);

    // --- Function 3 Browser ---
    QFrame *browserContainer3 = new QFrame(rightPanel);
    browserContainer3->setObjectName("Function3BrowserContainer");
    browserContainer3->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    browserContainer3->setStyleSheet(
        "#Function3BrowserContainer {"
        "   background-color: rgba(26, 26, 30, 110);"
        "   border: 1px solid rgba(255, 255, 255, 14);"
        "   border-radius: 20px;"
        "}"
    );
    QVBoxLayout *browserLayout3 = new QVBoxLayout(browserContainer3);
    browserLayout3->setContentsMargins(0, 0, 0, 0);
    browserLayout3->setSpacing(0);

    m_function3WebView = new QWebEngineView(browserContainer3);
    auto *page3 = new StoryboardWebPage(profile, this, m_function3WebView);
    m_function3WebView->setPage(page3);
    connect(m_function3WebView, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok) return;
        injectWebCredentialAndAutofill(m_function3WebView);
    });
    browserLayout3->addWidget(m_function3WebView);
    m_function3BrowserPage = browserContainer3;
    m_rightContentStack->addWidget(m_function3BrowserPage);

    QFrame *videoContainer = new QFrame(rightPanel);
    videoContainer->setObjectName("VideoContainer");
    videoContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoContainer->setStyleSheet(
        "#VideoContainer {"
        "   background-color: #404040;"
        "   border-radius: 20px;"
        "}"
    );
    QVBoxLayout *videoLayout = new QVBoxLayout(videoContainer);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->setSpacing(0);

    m_videoTopBar = new QWidget(videoContainer);
    m_videoTopBar->setFixedHeight(50);
    m_videoTopBar->setStyleSheet("background-color: transparent;");
    QHBoxLayout *videoTopLayout = new QHBoxLayout(m_videoTopBar);
    videoTopLayout->setContentsMargins(0, 0, 0, 0); // No bottom margin to bring video closer
    videoTopLayout->setSpacing(8);

    ResponsiveButton *backBtn = new ResponsiveButton(m_videoTopBar);
    backBtn->setFixedSize(40, 40);
    backBtn->setText(QStringLiteral("←"));
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setToolTip(QStringLiteral("返回"));
    backBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: transparent;"
        "  border: none;"
        "  color: rgba(240,240,240,230);"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background-color: rgba(255,255,255,30); border-radius: 18px; }"
        "QPushButton:pressed { background-color: rgba(255,255,255,50); border-radius: 18px; }"
    ));
    connect(backBtn, &QPushButton::clicked, this, [this]() { stopEmbeddedWatching(); });
    m_titleBackBtn = backBtn;

    m_videoTopRightPlaceholder = new QWidget(m_videoTopBar);
    m_videoTopRightPlaceholder->setObjectName("VideoTopRightPlaceholder");
    m_videoTopRightPlaceholder->setFixedSize(backBtn->size());
    m_videoTopRightPlaceholder->setStyleSheet(QStringLiteral("background: transparent;"));

    m_annotationContainer = new QFrame(m_videoTopBar);
    m_annotationContainer->setObjectName("VideoAnnotationContainer");
    m_annotationContainer->setFixedHeight(30);
    m_annotationContainer->setFrameShape(QFrame::NoFrame);
    m_annotationContainer->setStyleSheet(
        "#VideoAnnotationContainer {"
        "   background-color: rgba(35, 35, 38, 160);"
        "   border: 1px solid rgba(255, 255, 255, 18);"
        "   border-radius: 15px;"
        "}"
    );
    QHBoxLayout *annotationLayout = new QHBoxLayout(m_annotationContainer);
    annotationLayout->setContentsMargins(10, 0, 10, 0);
    annotationLayout->setSpacing(0);
    m_annotationToolbar = new AnnotationToolbar(m_annotationContainer);
    annotationLayout->addWidget(m_annotationToolbar);

    if (m_annotationToolbar) {
        connect(m_annotationToolbar, &AnnotationToolbar::toolSelected, this, [this](int mode) {
            if (!m_embeddedVideoWidget) return;
            if (mode == 0) {
                m_embeddedVideoWidget->setAnnotationEnabled(false);
                m_embeddedVideoWidget->setToolMode(0);
                return;
            }
            m_embeddedVideoWidget->setAnnotationEnabled(true);
            if (mode == 1) m_embeddedVideoWidget->setToolMode(0);
            else if (mode == 2) m_embeddedVideoWidget->setToolMode(2);
            else if (mode == 3) m_embeddedVideoWidget->setToolMode(3);
            else if (mode == 4) m_embeddedVideoWidget->setToolMode(5);
            else if (mode == 5) m_embeddedVideoWidget->setToolMode(4);
            else if (mode == 6) m_embeddedVideoWidget->setToolMode(1);
        });
        connect(m_annotationToolbar, &AnnotationToolbar::colorChanged, this, [this](int colorId) {
            if (m_embeddedVideoWidget) m_embeddedVideoWidget->setAnnotationColorId(colorId);
        });
        connect(m_annotationToolbar, &AnnotationToolbar::undoRequested, this, [this]() {
            if (m_embeddedVideoWidget) m_embeddedVideoWidget->sendUndo();
        });
        connect(m_annotationToolbar, &AnnotationToolbar::cameraRequested, this, [this]() {
            if (!m_embeddedVideoWidget) return;
            QImage img = m_embeddedVideoWidget->captureToImage();
            if (img.isNull()) return;
            QClipboard *cb = QGuiApplication::clipboard();
            if (cb) cb->setImage(img);
        });
        connect(m_annotationToolbar, &AnnotationToolbar::snippetRequested, this, [this]() {
            QScreen *screen = this->screen();
            if (!screen) return;
            QPixmap fullPix = screen->grabWindow(0);
            SnippetOverlay *overlay = new SnippetOverlay(fullPix);
            overlay->setGeometry(screen->geometry());
            overlay->show();
        });
        connect(m_annotationToolbar, &AnnotationToolbar::clearRequested, this, [this]() {
            if (m_embeddedVideoWidget) m_embeddedVideoWidget->sendClear();
        });
        connect(m_annotationToolbar, &AnnotationToolbar::remoteControlToggled, this, [this](bool checked) {
            if (m_embeddedVideoWidget) m_embeddedVideoWidget->setRemoteControlEnabled(checked);
        });
        connect(m_annotationToolbar, &AnnotationToolbar::maximizeRequested, this, [this](bool maximized) {
            toggleEmbeddedVideoFullscreen(maximized);
        });
    }

    videoTopLayout->addSpacing(8);
    videoTopLayout->addWidget(backBtn);
    videoTopLayout->addStretch();
    videoTopLayout->addWidget(m_annotationContainer, 0, Qt::AlignHCenter);
    videoTopLayout->addStretch();
    videoTopLayout->addWidget(m_videoTopRightPlaceholder);
    videoTopLayout->addSpacing(8);

    videoLayout->addWidget(m_videoTopBar);
    m_embeddedVideoWidget = new VideoDisplayWidget(videoContainer);
    m_embeddedVideoWidget->setShowControls(false);
    m_embeddedVideoWidget->setAutoResize(true);
    m_embeddedVideoWidget->installEventFilter(this);
    m_embeddedVideoWidget->setStyleSheet(
        "VideoDisplayWidget {"
        "    background-color: #000000;"
        "    border: none;"
        "    margin-top: -10px;" /* Negative margin to pull video up closer to the back button row */
        "}"
    );
    connect(m_embeddedVideoWidget, &VideoDisplayWidget::receivingStopped, this, [this](const QString &, const QString &targetId) {
        if (!targetId.isEmpty()) {
            onVideoReceivingStopped(targetId);
            emit videoReceivingStopped(targetId);
        }
    });
    videoLayout->addWidget(m_embeddedVideoWidget);
    m_videoContentPage = videoContainer;
    m_rightContentStack->addWidget(m_videoContentPage);
    m_rightContentStack->setCurrentWidget(m_homeContentPage);

    rightLayout->addWidget(titleBar);
    rightLayout->addWidget(m_rightContentStack);

    // Connect selection change to update styles
    connect(m_listWidget, &QListWidget::itemSelectionChanged, [this]() {
        for(int i = 0; i < m_listWidget->count(); ++i) {
            QListWidgetItem *item = m_listWidget->item(i);
            QWidget *w = m_listWidget->itemWidget(item);
            if (w) {
                QFrame *card = w->findChild<QFrame*>("CardFrame");
                if (card) {
                    QString userId = card->property("userId").toString();
                    if (userId.isEmpty()) {
                        userId = item->data(Qt::UserRole).toString();
                    }
                    if (card == m_localCard) {
                        continue;
                    }
                    updateRemoteCardActivityStyle(userId);
                }
            }
        }
        if (m_localCard) {
            updateLocalCardActivityStyle(m_localActivityActive);
        }
    });

    // Assemble Main Layout
    mainLayout->addWidget(leftPanel);
    mainLayout->addWidget(rightPanel);

    if (AppConfig::lastTutorialVersion() != AppConfig::applicationVersion()) {
        QTimer::singleShot(1000, this, &NewUiWindow::showUserGuide);
    }
}

void NewUiWindow::showFunction1Browser()
{
    if (!m_rightContentStack || !m_function1BrowserPage) {
        return;
    }

    // [Maintenance Mode] Hide WebView and show maintenance message
    if (m_function1WebView) {
        m_function1WebView->setVisible(false);
    }

    QLabel *maintenanceLabel = m_function1BrowserPage->findChild<QLabel *>("MaintenanceLabel");
    if (!maintenanceLabel) {
        maintenanceLabel = new QLabel(QStringLiteral("李哥故事白板尚在维护中\n敬请期待"), m_function1BrowserPage);
        maintenanceLabel->setObjectName("MaintenanceLabel");
        maintenanceLabel->setAlignment(Qt::AlignCenter);
        maintenanceLabel->setStyleSheet("QLabel { color: rgba(255, 255, 255, 150); font-size: 24px; font-weight: bold; }");
        if (m_function1BrowserPage->layout()) {
            m_function1BrowserPage->layout()->addWidget(maintenanceLabel);
        }
    }
    maintenanceLabel->setVisible(true);

    /*
    if (m_function1WebView) {
        QString v = AppConfig::readConfigValue(QStringLiteral("storyboard_url")).trimmed();
        if (v.isEmpty()) {
            v = QStringLiteral("http://124.221.247.99:9001/");
        }
        m_function1WebView->load(QUrl::fromUserInput(v));
    }
    */
    m_rightContentStack->setCurrentWidget(m_function1BrowserPage);
}

void NewUiWindow::showHomeContent()
{
    if (!m_rightContentStack || !m_homeContentPage) {
        return;
    }
    m_rightContentStack->setCurrentWidget(m_homeContentPage);
}

void NewUiWindow::toggleFunction1Maximize()
{
    activateWindow();
    raise();

    Qt::WindowStates state = windowState();
    const bool currentlyMaximized = (state & Qt::WindowMaximized);
    if (currentlyMaximized) {
        setWindowState(state & ~Qt::WindowMaximized);
        showNormal();
    } else {
        setWindowState(state | Qt::WindowMaximized);
        showMaximized();
    }
    updateTitleMaximizeButton();
    setResizeGripsVisible(!(windowState() & Qt::WindowMaximized));
}

void NewUiWindow::updateTitleMaximizeButton()
{
    if (!m_titleMaximizeBtn) {
        return;
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    const bool currentlyMaximized = (windowState() & Qt::WindowMaximized);
    if (currentlyMaximized) {
        m_titleMaximizeBtn->setIcon(QIcon(appDir + "/maps/logo/Restore.png"));
        m_titleMaximizeBtn->setToolTip(QStringLiteral("还原"));
    } else {
        m_titleMaximizeBtn->setIcon(QIcon(appDir + "/maps/logo/maximize.png"));
        m_titleMaximizeBtn->setToolTip(QStringLiteral("最大化"));
    }
}

void NewUiWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event && event->type() == QEvent::WindowStateChange) {
        updateTitleMaximizeButton();
        setResizeGripsVisible(!(windowState() & Qt::WindowMaximized));
#ifdef _WIN32
        QTimer::singleShot(0, this, [this]() {
            applyHwndCornerStyle(reinterpret_cast<HWND>(winId()), !(windowState() & Qt::WindowMaximized));
        });
#endif
    }
}

bool NewUiWindow::event(QEvent *event)
{
#ifdef _WIN32
    if (event && (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange)) {
        QTimer::singleShot(0, this, [this]() {
            HWND hwnd = reinterpret_cast<HWND>(winId());
            DWORD build = getWindowsBuildNumber();
            
            // Windows 11 (Build 22000+)
            if (build >= 22000) {
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
            } 
            // Windows 10 or older
            else {
                // Extended frame logic already handled by initial attributes
                
                // Extend the frame into the client area to ensure WM_NCCALCSIZE works correctly
                tryExtendGlassFrame(hwnd);
                
                // Try to enable blur/acrylic
                tryEnableAcrylicBlur(hwnd, kAcrylicTintAbgr);

                // Use a semi-transparent gradient stylesheet to allow blur to show through
                // Converting #0859f0 (8, 89, 240) and #d400ff (212, 0, 255) to rgba with alpha ~245
                setStyleSheet("QWidget#NewUiWindowRoot {"
                              "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, "
                              "    stop:0 rgba(8, 89, 240, 245), stop:1 rgba(212, 0, 255, 245));"
                              "}");
                
                // Still try to apply rounded corners if possible (software fallback in applyHwndCornerStyle)
                applyHwndCornerStyle(hwnd, !(windowState() & Qt::WindowMaximized));
            }
        });
    }
#endif
    if (event->type() == QEvent::ApplicationStateChange) {
        if (QApplication::applicationState() == Qt::ApplicationActive) {
            if (m_timer) m_timer->start(60000);
            
            // Resume HiFps if a user is selected
            if (m_listWidget) {
                QListWidgetItem *current = m_listWidget->currentItem();
                if (current) {
                    QString userId = current->data(Qt::UserRole).toString();
                    if (userId.isEmpty()) {
                        if (QWidget *iw = m_listWidget->itemWidget(current)) {
                            if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                                userId = card->property("userId").toString();
                            } else {
                                userId = iw->property("userId").toString();
                            }
                        }
                    }
                    if (!userId.isEmpty() && userId != m_myStreamId) {
                         startHiFpsForUser(userId);
                         resetSelectionAutoPause(userId);
                    }
                }
            }
        } else {
            if (m_timer) m_timer->stop();
            
            // Stop HiFps
            stopHiFpsForUser();
            if (m_selectionAutoPauseTimer) {
                m_selectionAutoPauseTimer->stop();
            }
        }
    }
    if (event && (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::WindowActivate)) {
        const bool active = (event->type() == QEvent::WindowActivate);
        if (!active) {
            stopSelfPreviewFast();
            if (m_selectionAutoPauseTimer) {
                m_selectionAutoPauseTimer->stop();
            }
            if (QApplication::applicationState() != Qt::ApplicationActive) {
                cancelHoverHiFps();
                const QStringList chs = m_hiFpsPublishers.keys();
                for (const QString &ch : chs) {
                    stopHiFpsPublishing(ch);
                }
            }
        } else {
            if (QApplication::applicationState() == Qt::ApplicationActive && m_listWidget) {
                QListWidgetItem *current = m_listWidget->currentItem();
                QString userId;
                if (current) {
                    userId = current->data(Qt::UserRole).toString();
                    if (userId.isEmpty()) {
                        if (QWidget *iw = m_listWidget->itemWidget(current)) {
                            if (QFrame *card = iw->findChild<QFrame*>("CardFrame")) {
                                userId = card->property("userId").toString();
                            } else {
                                userId = iw->property("userId").toString();
                            }
                        }
                    }
                }
                if (!userId.isEmpty() && userId == m_myStreamId) {
                    startSelfPreviewFast();
                } else {
                    stopSelfPreviewFast();
                }
                if (!userId.isEmpty() && userId != m_myStreamId) {
                    if (userId != m_autoPausedUserId) {
                        startHiFpsForUser(userId);
                        resetSelectionAutoPause(userId);
                    }
                }
            }
        }
    }
    return QWidget::event(event);
}

bool NewUiWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef _WIN32
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_NCHITTEST) {
            // Use QCursor::pos() to get logical coordinates correctly handling HighDPI
            QPoint globalPos = QCursor::pos();
            QPoint localPos = mapFromGlobal(globalPos);

            // Title bar area: Use actual titlebar height or default 80
            int titleHeight = m_titleBar ? m_titleBar->height() : 80;
            
            // Allow a bit more space for resize handles at top if needed, but usually title bar is enough
            if (localPos.y() <= titleHeight) {
                QWidget *child = childAt(localPos);
                bool isInteractive = false;
                if (child) {
                    // Check if the widget or its parent (up to title bar) is a button/interactive
                    QWidget *curr = child;
                    while (curr && curr != this && curr != m_titleBar) {
                        if (qobject_cast<QAbstractButton*>(curr)) {
                            isInteractive = true;
                            break;
                        }
                        curr = curr->parentWidget();
                    }
                }
                
                if (!isInteractive) {
                    *result = HTCAPTION;
                    return true;
                }
            }
        } else if (msg->message == WM_NCLBUTTONDBLCLK) {
            // [Fix] Handle double-click on title bar manually to ensure state sync
            if (msg->wParam == HTCAPTION) {
                toggleFunction1Maximize();
                *result = 0;
                return true;
            }
        } else if (msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
            // Remove standard window frame
            *result = 0;
            return true;
        } else if (msg->message == WM_ENTERSIZEMOVE) {
            // [Fix] Disable Acrylic during drag/resize to prevent lag on Win10, and for visual effect on Win11
            if (m_isWin10 || m_isWin11) {
                disableAcrylic(reinterpret_cast<HWND>(winId()));
            }
        } else if (msg->message == WM_EXITSIZEMOVE) {
            // [Fix] Restore Acrylic after drag/resize
            if (m_isWin10 || m_isWin11) {
                // Use a timer to allow the drag loop to finish before resetting composition
                QTimer::singleShot(50, this, [this]() {
                    HWND hwnd = reinterpret_cast<HWND>(winId());
                    // 1. Reset to disabled first to ensure state transition
                    ACCENT_POLICY policy{};
                    policy.AccentState = ACCENT_DISABLED;
                    WINDOWCOMPOSITIONATTRIBDATA data{};
                    data.Attrib = WCA_ACCENT_POLICY;
                    data.pvData = &policy;
                    data.cbData = sizeof(policy);

                    HMODULE user32 = GetModuleHandleW(L"user32.dll");
                    if (user32) {
                        auto fn = reinterpret_cast<SetWindowCompositionAttributeFn>(GetProcAddress(user32, "SetWindowCompositionAttribute"));
                        if (fn) {
                            fn(hwnd, &data);
                        }
                    }

                    // 2. Re-apply Acrylic (with fallback for Win11)
                    if (!tryEnableAcrylicBlur(hwnd, kAcrylicTintAbgr)) {
                        if (m_isWin11) {
                            tryEnableDwmBackdropSimple(hwnd, 3, true);
                        }
                    }
                    update();
                });
            }
        }
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void NewUiWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Manual dragging logic for non-titlebar areas (if desired)
        // Note: Title bar dragging is handled by WM_NCHITTEST in nativeEvent
        m_dragging = true;
        // Use globalPosition() for Qt6
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void NewUiWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton && m_dragging) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void NewUiWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    }
}

void NewUiWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    QWidget::mouseDoubleClickEvent(event);
}

void NewUiWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateResizeGrips();
#ifdef _WIN32
    applyHwndCornerStyle(reinterpret_cast<HWND>(winId()), !(windowState() & Qt::WindowMaximized));
#endif
    if (m_farRightPanel) {
        const int outerMargin = 10;
        const int panelW = m_farRightPanel->width();
        int yTop = outerMargin;
        if (m_titleBar) {
            const QPoint p = m_titleBar->mapTo(this, QPoint(0, 0));
            yTop = p.y() + m_titleBar->height() + outerMargin;
        }
        const int panelH = qMax(0, height() - yTop - outerMargin);
        m_farRightPanel->setGeometry(width() - outerMargin - panelW, yTop, panelW, panelH);
        m_farRightPanel->raise();
    }
    updateNotificationPositions();
}

bool NewUiWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_audioCallDialog) {
        bool insideCallDialog = false;
        QObject *cur = watched;
        while (cur) {
            if (cur == m_audioCallDialog) {
                insideCallDialog = true;
                break;
            }
            cur = cur->parent();
        }

        if (insideCallDialog) {
            bool insideButton = false;
            cur = watched;
            while (cur && cur != m_audioCallDialog) {
                if (qobject_cast<QAbstractButton*>(cur)) {
                    insideButton = true;
                    break;
                }
                cur = cur->parent();
            }

            if (!insideButton) {
                if (event->type() == QEvent::MouseButtonPress) {
                    auto *me = static_cast<QMouseEvent*>(event);
                    if (me->button() == Qt::LeftButton) {
                        m_audioCallDialogDragging = true;
                        m_audioCallDialogDragOffset = me->globalPosition().toPoint() - m_audioCallDialog->frameGeometry().topLeft();
                        event->accept();
                        return true;
                    }
                } else if (event->type() == QEvent::MouseMove) {
                    if (m_audioCallDialogDragging) {
                        auto *me = static_cast<QMouseEvent*>(event);
                        m_audioCallDialog->move(me->globalPosition().toPoint() - m_audioCallDialogDragOffset);
                        event->accept();
                        return true;
                    }
                } else if (event->type() == QEvent::MouseButtonRelease) {
                    auto *me = static_cast<QMouseEvent*>(event);
                    if (me->button() == Qt::LeftButton) {
                        m_audioCallDialogDragging = false;
                        event->accept();
                        return true;
                    }
                }
            }
        }
    }

    if (m_audioCallMiniBar) {
        bool insideMini = false;
        QObject *cur = watched;
        while (cur) {
            if (cur == m_audioCallMiniBar) {
                insideMini = true;
                break;
            }
            cur = cur->parent();
        }
        if (insideMini) {
            if (event->type() == QEvent::MouseButtonDblClick) {
                auto *me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton) {
                    m_audioCallMiniBarDragging = false;
                    setAudioCallMiniHidden(false);
                    // hideAudioCallMiniBar();
                    if (m_audioCallDialog && !m_audioCallPeerId.isEmpty()) {
                        m_audioCallDialog->show();
                        m_audioCallDialog->raise();
                    }
                    return true;
                }
            } else if (event->type() == QEvent::MouseButtonPress) {
                auto *me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton) {
                    m_audioCallMiniBarDragging = true;
                    m_audioCallMiniBarDragOffset = me->globalPosition().toPoint() - m_audioCallMiniBar->frameGeometry().topLeft();
                    event->accept();
                    return true;
                }
            } else if (event->type() == QEvent::MouseMove) {
                if (m_audioCallMiniBarDragging) {
                    auto *me = static_cast<QMouseEvent*>(event);
                    m_audioCallMiniBar->move(me->globalPosition().toPoint() - m_audioCallMiniBarDragOffset);
                    event->accept();
                    return true;
                }
            } else if (event->type() == QEvent::MouseButtonRelease) {
                auto *me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton) {
                    m_audioCallMiniBarDragging = false;
                    event->accept();
                    return true;
                }
            }
        }
    }

    if (watched == m_resizeGripLeft || watched == m_resizeGripRight || watched == m_resizeGripTop || watched == m_resizeGripBottom ||
        watched == m_resizeGripTopLeft || watched == m_resizeGripTopRight || watched == m_resizeGripBottomLeft || watched == m_resizeGripBottomRight) {
        if (windowState() & Qt::WindowMaximized) {
            return true;
        }
        auto edgesForGrip = [this](QObject *o) -> Qt::Edges {
            if (o == m_resizeGripLeft) return Qt::LeftEdge;
            if (o == m_resizeGripRight) return Qt::RightEdge;
            if (o == m_resizeGripTop) return Qt::TopEdge;
            if (o == m_resizeGripBottom) return Qt::BottomEdge;
            if (o == m_resizeGripTopLeft) return Qt::LeftEdge | Qt::TopEdge;
            if (o == m_resizeGripTopRight) return Qt::RightEdge | Qt::TopEdge;
            if (o == m_resizeGripBottomLeft) return Qt::LeftEdge | Qt::BottomEdge;
            if (o == m_resizeGripBottomRight) return Qt::RightEdge | Qt::BottomEdge;
            return Qt::Edges();
        };

        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_resizeDragging = true;
                m_resizeEdges = edgesForGrip(watched);
                m_resizePressGlobal = me->globalPosition().toPoint();
                m_resizeStartGeometry = frameGeometry();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            if (m_resizeDragging) {
                auto *me = static_cast<QMouseEvent*>(event);
                const QPoint gp = me->globalPosition().toPoint();
                const int dx = gp.x() - m_resizePressGlobal.x();
                const int dy = gp.y() - m_resizePressGlobal.y();
                QRect r = m_resizeStartGeometry;

                const int minW = qMax(500, minimumWidth());
                const int minH = qMax(450, minimumHeight());

                if (m_resizeEdges.testFlag(Qt::LeftEdge)) r.setLeft(r.left() + dx);
                if (m_resizeEdges.testFlag(Qt::RightEdge)) r.setRight(r.right() + dx);
                if (m_resizeEdges.testFlag(Qt::TopEdge)) r.setTop(r.top() + dy);
                if (m_resizeEdges.testFlag(Qt::BottomEdge)) r.setBottom(r.bottom() + dy);

                if (r.width() < minW) {
                    if (m_resizeEdges.testFlag(Qt::LeftEdge)) r.setLeft(r.right() - minW + 1);
                    else r.setRight(r.left() + minW - 1);
                }
                if (r.height() < minH) {
                    if (m_resizeEdges.testFlag(Qt::TopEdge)) r.setTop(r.bottom() - minH + 1);
                    else r.setBottom(r.top() + minH - 1);
                }
                setGeometry(r);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_resizeDragging = false;
                m_resizeEdges = Qt::Edges();
                return true;
            }
        }
        return true;
    }

    if (m_farRightPanel && m_farRightPanel->isVisible() && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            bool insidePanel = false;
            QObject *cur = watched;
            while (cur) {
                if (cur == m_farRightPanel) {
                    insidePanel = true;
                    break;
                }
                cur = cur->parent();
            }

            if (!insidePanel && watched != m_localCard) {
                bool insideTitleBar = false;
                if (m_titleBar) {
                    QObject *t = watched;
                    while (t) {
                        if (t == m_titleBar) {
                            insideTitleBar = true;
                            break;
                        }
                        t = t->parent();
                    }
                }
                bool insideList = (m_listWidget && watched == m_listWidget->viewport());
                if (insideList) {
                    QListWidgetItem *pressedItem = m_listWidget->itemAt(me->pos());
                    if (pressedItem && m_listWidget->row(pressedItem) == 0) {
                        insideList = false;
                    }
                }
            }
        }
    }

    if (m_titleBar && (watched == m_titleBar || watched->objectName() == QStringLiteral("ToolsContainer") || watched->objectName() == QStringLiteral("TitleControlContainer"))) {
        if (qobject_cast<QAbstractButton*>(watched)) {
            return QWidget::eventFilter(watched, event);
        }
        if (watched == m_toolbarAvatarLabel || watched == m_localAvatarLabel) {
            return QWidget::eventFilter(watched, event);
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                toggleFunction1Maximize();
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_titleBarDragging = true;
                m_titleBarPendingRestore = (windowState() & Qt::WindowMaximized);
                m_titleBarSnapMaximize = false;
                m_titleBarPressGlobal = me->globalPosition().toPoint();
                m_titleBarPressLocalInWindow = mapFromGlobal(m_titleBarPressGlobal);
                m_titleBarDragOffset = m_titleBarPressGlobal - frameGeometry().topLeft();
                return true;
            }
        }
        if (event->type() == QEvent::MouseMove) {
            if (m_titleBarDragging) {
                auto *me = static_cast<QMouseEvent*>(event);
                if (!(me->buttons() & Qt::LeftButton)) {
                    m_titleBarDragging = false;
                    m_titleBarPendingRestore = false;
                    m_titleBarSnapMaximize = false;
                    return true;
                }
                const QPoint globalPos = me->globalPosition().toPoint();

#ifdef _WIN32
                if ((globalPos - m_titleBarPressGlobal).manhattanLength() > QApplication::startDragDistance()) {
                    ReleaseCapture();
                    HWND hwnd = reinterpret_cast<HWND>(winId());
                    
                    // Handle "drag from maximized" logic before handing off to OS
                    if (windowState() & Qt::WindowMaximized) {
                        QRect restore = normalGeometry();
                        // [Fix] If normalGeometry is invalid or erroneously set to maximized size, use default
                        if (!restore.isValid() || restore.size() == size()) {
                            if (QScreen *screen = QGuiApplication::screenAt(globalPos)) {
                                QRect avail = screen->availableGeometry();
                                restore = QRect(0, 0, avail.width() * 0.8, avail.height() * 0.8);
                                restore.moveCenter(avail.center());
                            } else {
                                restore = QRect(100, 100, 1280, 720);
                            }
                        }
                        const int restoreW = qMax(200, restore.width());
                        const int restoreH = qMax(200, restore.height());
                        const qreal xRatio = width() > 0 ? (qreal)m_titleBarPressLocalInWindow.x() / (qreal)width() : 0.5;
                        const int newX = globalPos.x() - qRound(xRatio * restoreW);
                        const int newY = globalPos.y() - m_titleBarPressLocalInWindow.y();
                        showNormal();
                        setGeometry(QRect(QPoint(newX, newY), QSize(restoreW, restoreH)));
                    }
                    
                    SendMessage(hwnd, WM_SYSCOMMAND, SC_MOVE + HTCAPTION, 0);
                    
                    // After SendMessage returns, drag is complete
                    m_titleBarDragging = false;
                    m_titleBarPendingRestore = false;
                    m_titleBarSnapMaximize = false;
                }
#else
                if (m_titleBarPendingRestore) {
                    m_titleBarPendingRestore = false;
                    const QRect restore = normalGeometry().isValid() ? normalGeometry() : geometry();
                    const int restoreW = qMax(200, restore.width());
                    const int restoreH = qMax(200, restore.height());
                    const qreal xRatio = width() > 0 ? (qreal)m_titleBarPressLocalInWindow.x() / (qreal)width() : 0.5;
                    const int newX = globalPos.x() - qRound(xRatio * restoreW);
                    const int newY = globalPos.y() - m_titleBarPressLocalInWindow.y();
                    showNormal();
                    setGeometry(QRect(QPoint(newX, newY), QSize(restoreW, restoreH)));
                    m_titleBarDragOffset = globalPos - frameGeometry().topLeft();
                } else {
                    move(globalPos - m_titleBarDragOffset);
                }

                if (QScreen *screen = QGuiApplication::screenAt(globalPos)) {
                    const QRect avail = screen->availableGeometry();
                    m_titleBarSnapMaximize = (globalPos.y() <= avail.top() + 24);
                } else {
                    m_titleBarSnapMaximize = false;
                }
#endif
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                const bool doMaximize = m_titleBarDragging && m_titleBarSnapMaximize && !(windowState() & Qt::WindowMaximized);
                m_titleBarDragging = false;
                m_titleBarPendingRestore = false;
                m_titleBarSnapMaximize = false;
                if (doMaximize) {
                    toggleFunction1Maximize();
                }
                return true;
            }
        }
    }

    if (m_listWidget && watched == m_listWidget->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            QListWidgetItem *item = m_listWidget->itemAt(me->pos());
            if (!item) {
                m_listWidget->clearSelection();
                m_listWidget->setCurrentItem(nullptr);
                cancelHoverHiFps();
                resetSelectionAutoPause(QString());
                return true;
            }
        }
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            const QVariant v = watched->property("userId");
            if (v.isValid()) {
                const QString userId = v.toString();
                if (!userId.isEmpty() && userId != m_myStreamId) {
                    if (m_listWidget) {
                        const QListWidgetItem *it = m_userItems.value(userId, nullptr);
                        if (it) {
                            m_listWidget->setCurrentItem(const_cast<QListWidgetItem*>(it));
                        }
                    }
                    if (QApplication::applicationState() == Qt::ApplicationActive) {
                        if (userId == m_autoPausedUserId) {
                            resumeSelectedStreamForUser(userId);
                        }
                        startHiFpsForUser(userId);
                        resetSelectionAutoPause(userId);
                    } else {
                        cancelHoverHiFps();
                        resetSelectionAutoPause(QString());
                    }
                }
            }
        }
    }

    if (watched == m_logoLabel && event->type() == QEvent::MouseButtonRelease) {
        QDesktopServices::openUrl(QUrl("http://www.iruler.cn"));
        return true;
    }

    if ((watched == m_toolbarAvatarLabel || watched == m_localAvatarLabel) &&
        event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            pickAndApplyLocalAvatar();
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonDblClick) {
        QString userId = watched->property("userId").toString();
        if (!userId.isEmpty()) {
            if (watched == m_localCard) {
                return true;
            }
            QString name = watched->property("userName").toString();
            emit startWatchingRequested(userId, name);
            return true; // Event handled
        }
    }



    if (watched == m_embeddedVideoWidget && m_embeddedFullscreenActive) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::WindowStateChange) {
            updateEmbeddedFullscreenOverlayGeometry();
        }
    }
    return QWidget::eventFilter(watched, event);
}



void NewUiWindow::setGlobalMicCheckedSilently(bool enabled)
{
    m_globalMicEnabled = enabled;
    if (!m_titleMicBtn) return;
    if (!m_titleMicBtn->isCheckable()) return;
    if (m_titleMicBtn->isChecked() == enabled) return;

    QSignalBlocker blocker(m_titleMicBtn);
    m_titleMicBtn->setChecked(enabled);
    if (!m_titleMicIconOn.isNull() && !m_titleMicIconOff.isNull()) {
        m_titleMicBtn->setIcon(enabled ? m_titleMicIconOn : m_titleMicIconOff);
    }
    m_titleMicBtn->setToolTip(enabled ? QStringLiteral("麦克风：开") : QStringLiteral("麦克风：关"));
    janusSetMuted(!enabled);
}

void NewUiWindow::onBroadcastBtnClicked()
{
    QMap<QString, QString> users;
    QMap<QString, QPixmap> avatars;

    // Iterate over m_userItems to get available users
    for (auto it = m_userItems.begin(); it != m_userItems.end(); ++it) {
        QString userId = it.key();
        QListWidgetItem *item = it.value();
        if (!item) continue;
        
        QString userName = item->data(Qt::UserRole + 1).toString();
        if (userName.isEmpty()) {
            userName = userId;
        }
        users.insert(userId, userName);

        // Get avatar
        if (m_userAvatarLabels.contains(userId)) {
            QLabel *label = m_userAvatarLabels.value(userId);
            if (label && !label->pixmap().isNull()) {
                avatars.insert(userId, label->pixmap());
            }
        }
    }

    BroadcastNoticeDialog dlg(users, avatars, this);
    connect(&dlg, &BroadcastNoticeDialog::publishRequested, this, &NewUiWindow::broadcastRequested);
    dlg.exec();
}

void NewUiWindow::onMeetingBtnClicked()
{
    QMap<QString, QString> users;
    QMap<QString, QPixmap> avatars;

    // Iterate over m_userItems to get available users
    for (auto it = m_userItems.begin(); it != m_userItems.end(); ++it) {
        QString userId = it.key();
        QListWidgetItem *item = it.value();
        if (!item) continue;
        
        QString userName = item->data(Qt::UserRole + 1).toString();
        if (userName.isEmpty()) {
            userName = userId;
        }
        users.insert(userId, userName);

        // Get avatar
        if (m_userAvatarLabels.contains(userId)) {
            QLabel *label = m_userAvatarLabels.value(userId);
            if (label && !label->pixmap().isNull()) {
                avatars.insert(userId, label->pixmap());
            }
        }
    }
    
    InviteUsersDialog dialog(users, avatars, this);
    connect(&dialog, &InviteUsersDialog::inviteRequested, this, &NewUiWindow::onInviteRequested);
    dialog.exec();
}

void NewUiWindow::onInviteRequested(const QStringList &userIds)
{
    if (userIds.isEmpty()) return;

    // [Fix] Add to pending list for cancellation tracking
    for (const QString &uid : userIds) {
        if (!uid.isEmpty() && !m_pendingInvitees.contains(uid)) {
            m_pendingInvitees.append(uid);
        }
    }

    // [Request] Auto open local drawing tool when starting a meeting
    emit setStreamingIslandVisibleRequested(true);

    // [Fix] 预先初始化 Janus 房间，避免多人同时加入时的竞态条件
    janusSwitchToMyRoom();

    // 如果当前已经在等待或者没有观众，显示等待弹窗
    if (m_inviteWaitDialog) {
        m_inviteWaitDialog->raise();
    } else {
        // 检查当前是否有观众，如果没有则显示等待
        // 注意：refreshAudioCallParticipants 还没跑，所以可能不知道具体人数，但 m_viewerList 可能有
        // 不过最稳妥的是：如果我们是发起者，且没人，就显示等待
        // 简单起见，只要发起邀请，就认为需要等待（除非已经有很多人）
        // 这里我们启用 "ignoreAlone" 防止没人进时自动关闭
        m_isWaitingForAttendees = true;
        janusSetIgnoreAlone(true);

        m_inviteWaitDialog = new QMessageBox(this);
        m_inviteWaitDialog->setAttribute(Qt::WA_DeleteOnClose);
        m_inviteWaitDialog->setWindowTitle(QStringLiteral("等待加入"));
        m_inviteWaitDialog->setText(QStringLiteral("正在等待观众加入..."));
        m_inviteWaitDialog->setStandardButtons(QMessageBox::Cancel);
        m_inviteWaitDialog->button(QMessageBox::Cancel)->setText(QStringLiteral("取消邀请"));
        
        // 30s Timeout for inviter wait dialog
        QTimer *waitTimer = new QTimer(m_inviteWaitDialog);
        waitTimer->setSingleShot(true);
        waitTimer->setInterval(30000);
        connect(waitTimer, &QTimer::timeout, m_inviteWaitDialog, [this]() {
            if (m_inviteWaitDialog) {
                m_inviteWaitDialog->reject(); // Close dialog
            }
        });
        waitTimer->start();

        connect(m_inviteWaitDialog, &QMessageBox::finished, this, [this](int result) {
            if (result != QDialog::Accepted) {
                // [Fix] Send cancellation to all invitees
                for (const QString &targetId : m_pendingInvitees) {
                     QJsonObject cancelMsg;
                     cancelMsg["type"] = "invite_cancelled";
                     cancelMsg["target_id"] = m_myStreamId; 
                     cancelMsg["viewer_id"] = targetId; 
                     cancelMsg["inviter_name"] = m_myUserName;
                     
                     QJsonDocument doc(cancelMsg);
                     QByteArray data = doc.toJson(QJsonDocument::Compact);
                     if (m_streamClient && m_streamClient->isConnected()) {
                         m_streamClient->sendTextMessage(QString::fromUtf8(data));
                     }
                     if (m_streamClientLan && m_streamClientLan->isConnected()) {
                         m_streamClientLan->sendTextMessage(QString::fromUtf8(data));
                     }
                }

                // 用户取消，恢复自动关闭逻辑，并可能挂断
                m_isWaitingForAttendees = false;
                janusSetIgnoreAlone(false);
                // 如果此时房间里还是没人，janusStop? 或者仅仅恢复检查
                // 用户说"挂断弹窗"，意味着取消就是不再等待，可能也就是不玩了
                
                // [Request] Cancel invite press should also close local drawing
                emit setStreamingIslandVisibleRequested(false);
                
                janusStop();
            }
            m_pendingInvitees.clear();
            // Dialog closes itself
        });
        m_inviteWaitDialog->show();
    }

    for (const QString &targetId : userIds) {
        if (targetId.isEmpty()) continue;

        // Use watch_request_accepted type because the server forwards it to the viewer_id.
        // We add "is_invite" to distinguish it from a normal acceptance.
        QJsonObject invite;
        invite["type"] = "watch_request_accepted";
        invite["viewer_id"] = targetId; // The user we are inviting
        invite["target_id"] = m_myStreamId; // Me (The inviter)
        invite["is_invite"] = true;
        invite["inviter_name"] = m_myUserName;
        
        QJsonDocument doc(invite);
        QByteArray data = doc.toJson(QJsonDocument::Compact);

        bool sent = false;
        // Send via Cloud StreamClient
        if (m_streamClient && m_streamClient->isConnected()) {
            m_streamClient->sendTextMessage(QString::fromUtf8(data));
            sent = true;
            qInfo() << "[NewUiWindow] Sent invite to" << targetId << "via Cloud StreamClient";
        } else {
            qWarning() << "[NewUiWindow] Cloud StreamClient not connected, cannot send invite to" << targetId;
        }
        
        // Optionally send via LAN if applicable
        if (m_streamClientLan && m_streamClientLan->isConnected()) {
             m_streamClientLan->sendTextMessage(QString::fromUtf8(data));
             if (!sent) sent = true;
             qInfo() << "[NewUiWindow] Sent invite to" << targetId << "via LAN StreamClient";
        }
        
        if (!sent) {
            QMessageBox::warning(this, QStringLiteral("发送失败"), 
                QStringLiteral("无法发送邀请给 %1，未连接到服务器").arg(targetId));
        }
    }
}

void NewUiWindow::showInviteNotification(const QString &inviterId, const QString &inviterName, const QString &type)
{
    if (m_activeInviteNotification) {
        m_activeInviteNotification->deleteLater();
        m_activeInviteNotification = nullptr;
    }

    m_activeInviteNotification = new QWidget(this);
    m_activeInviteNotification->setObjectName("InviteNotification");
    m_activeInviteNotification->setStyleSheet(
        "QWidget#InviteNotification {"
        "   background-color: rgba(40, 40, 45, 230);"
        "   border: 1px solid rgba(0, 200, 83, 100);"
        "   border-radius: 8px;"
        "}"
        "QLabel {"
        "   color: #e0e0e0;"
        "   font-size: 13px;"
        "}"
        "QPushButton {"
        "   background-color: rgba(255, 255, 255, 15);"
        "   border: 1px solid rgba(255, 255, 255, 30);"
        "   border-radius: 4px;"
        "   color: #e0e0e0;"
        "   padding: 4px 12px;"
        "}"
        "QPushButton:hover {"
        "   background-color: rgba(255, 255, 255, 25);"
        "   color: #fff;"
        "}"
        "QPushButton#AcceptBtn {"
        "   background-color: rgba(0, 200, 83, 60);"
        "   border: 1px solid rgba(0, 200, 83, 100);"
        "}"
        "QPushButton#AcceptBtn:hover {"
        "   background-color: rgba(0, 200, 83, 100);"
        "}"
        "QPushButton#RejectBtn {"
        "   background-color: rgba(255, 80, 80, 60);"
        "   border: 1px solid rgba(255, 80, 80, 100);"
        "}"
        "QPushButton#RejectBtn:hover {"
        "   background-color: rgba(255, 80, 80, 100);"
        "}"
    );

    QVBoxLayout *mainLayout = new QVBoxLayout(m_activeInviteNotification);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    QLabel *titleLabel = new QLabel(QStringLiteral("会议邀请"), m_activeInviteNotification);
    titleLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #fff;");
    
    QLabel *textLabel = new QLabel(QStringLiteral("%1 邀请您加入视频通话").arg(inviterName), m_activeInviteNotification);
    textLabel->setWordWrap(true);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);

    QPushButton *acceptBtn = new QPushButton(QStringLiteral("接受"), m_activeInviteNotification);
    acceptBtn->setObjectName("AcceptBtn");
    acceptBtn->setCursor(Qt::PointingHandCursor);

    QPushButton *rejectBtn = new QPushButton(QStringLiteral("拒绝"), m_activeInviteNotification);
    rejectBtn->setObjectName("RejectBtn");
    rejectBtn->setCursor(Qt::PointingHandCursor);

    btnLayout->addStretch();
    btnLayout->addWidget(rejectBtn);
    btnLayout->addWidget(acceptBtn);

    mainLayout->addWidget(titleLabel);
    mainLayout->addWidget(textLabel);
    mainLayout->addLayout(btnLayout);

    // Timer for auto-expire
    QTimer *timer = new QTimer(m_activeInviteNotification);
    timer->setSingleShot(true);
    timer->setInterval(30000);
    
    connect(timer, &QTimer::timeout, this, [this, inviterName, inviterId]() {
        if (m_activeInviteNotification) {
            m_activeInviteNotification->deleteLater();
            m_activeInviteNotification = nullptr;
            showExpiredInviteNotification(inviterName);
            
            // Auto-reject: Send rejection to server
            QJsonObject rejectMsg;
            rejectMsg["type"] = "watch_request_rejected";
            rejectMsg["viewer_id"] = m_myStreamId;
            rejectMsg["target_id"] = inviterId;
            
            QJsonDocument rejectDoc(rejectMsg);
            QByteArray data = rejectDoc.toJson(QJsonDocument::Compact);
            
            if (m_streamClient && m_streamClient->isConnected()) {
                m_streamClient->sendTextMessage(QString::fromUtf8(data));
            }
            if (m_streamClientLan && m_streamClientLan->isConnected()) {
                m_streamClientLan->sendTextMessage(QString::fromUtf8(data));
            }
        }
    });
    timer->start();

    // Connect Buttons
    connect(rejectBtn, &QPushButton::clicked, this, [this, inviterId]() {
        if (m_activeInviteNotification) {
            m_activeInviteNotification->deleteLater();
            m_activeInviteNotification = nullptr;
            
            // Send rejection to server
            QJsonObject rejectMsg;
            rejectMsg["type"] = "watch_request_rejected";
            rejectMsg["viewer_id"] = m_myStreamId;
            rejectMsg["target_id"] = inviterId;
            
            QJsonDocument rejectDoc(rejectMsg);
            QByteArray data = rejectDoc.toJson(QJsonDocument::Compact);
            
            if (m_streamClient && m_streamClient->isConnected()) {
                m_streamClient->sendTextMessage(QString::fromUtf8(data));
            }
            if (m_streamClientLan && m_streamClientLan->isConnected()) {
                m_streamClientLan->sendTextMessage(QString::fromUtf8(data));
            }
        }
    });

    connect(acceptBtn, &QPushButton::clicked, this, [this, inviterId, type]() {
        if (m_activeInviteNotification) {
            m_activeInviteNotification->deleteLater();
            m_activeInviteNotification = nullptr;
        }

        if (type == QStringLiteral("invite_to_room")) {
             if (!inviterId.isEmpty()) {
                 if (m_userItems.contains(inviterId)) {
                     QListWidgetItem *item = m_userItems.value(inviterId);
                     if (item && m_listWidget) {
                         m_listWidget->setCurrentItem(item);
                     }
                 } else {
                     qWarning() << "Inviter ID not found in user list:" << inviterId;
                 }
             }
        } else if (type == QStringLiteral("watch_request_accepted")) {
            QJsonObject req;
            req["type"] = "watch_request";
            req["viewer_id"] = m_myStreamId;
            req["target_id"] = inviterId;
            req["action"] = "invite_response";
            
            QByteArray data = QJsonDocument(req).toJson(QJsonDocument::Compact);
            if (m_streamClient && m_streamClient->isConnected()) {
                m_streamClient->sendTextMessage(QString::fromUtf8(data));
            }
            
            if (m_userItems.contains(inviterId)) {
                 QListWidgetItem *item = m_userItems.value(inviterId);
                 if (item && m_listWidget) {
                     m_listWidget->setCurrentItem(item);
                 }
            }
        }
    });

    m_activeInviteNotification->adjustSize();
    updateNotificationPositions();
    m_activeInviteNotification->show();
    m_activeInviteNotification->raise();
}

void NewUiWindow::updateNotificationPositions()
{
    const int margin = 20;
    int bottomY = height() - margin;
    
    if (m_expiredInviteNotification) {
        m_expiredInviteNotification->adjustSize();
        bottomY -= m_expiredInviteNotification->height();
        m_expiredInviteNotification->move(width() - m_expiredInviteNotification->width() - margin, bottomY);
        bottomY -= 10; // Spacing
    }
    
    if (m_activeInviteNotification) {
        m_activeInviteNotification->adjustSize();
        bottomY -= m_activeInviteNotification->height();
        m_activeInviteNotification->move(width() - m_activeInviteNotification->width() - margin, bottomY);
    }
}

void NewUiWindow::updateLocalCardActivityStyle(bool active)
{
    if (!m_localCard) {
        return;
    }
    bool selected = false;
    if (m_listWidget) {
        for (int i = 0; i < m_listWidget->count(); ++i) {
            QListWidgetItem *item = m_listWidget->item(i);
            QWidget *w = item ? m_listWidget->itemWidget(item) : nullptr;
            QFrame *card = w ? w->findChild<QFrame*>("CardFrame") : nullptr;
            if (card == m_localCard) {
                selected = item && item->isSelected();
                break;
            }
        }
    }
    const QString base = QStringLiteral("rgba(32, 32, 36, 175)");
    const QString baseHover = QStringLiteral("rgba(40, 40, 45, 190)");
    const QString top = active ? QStringLiteral("rgba(0, 200, 83, 200)") : QStringLiteral("rgba(160, 90, 210, 200)");
    const QString topHover = active ? QStringLiteral("rgba(0, 220, 95, 230)") : QStringLiteral("rgba(180, 110, 230, 230)");
    const QString gradient = QStringLiteral("qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 %1, stop:0.38 %2, stop:0.62 %2, stop:1 %2)")
                                 .arg(top, base);
    const QString gradientHover = QStringLiteral("qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 %1, stop:0.38 %2, stop:0.62 %2, stop:1 %2)")
                                      .arg(topHover, baseHover);
    const QString bottomLineColor = selected ? QStringLiteral("rgba(255, 102, 0, 220)") : QStringLiteral("rgba(255, 255, 255, 22)");
    const QString bottomLineWidth = selected ? QStringLiteral("3px") : QStringLiteral("1px");
    m_localCard->setStyleSheet(
        QStringLiteral(
            "#CardFrame {"
            "   background: %1;"
            "   border: 1px solid rgba(255, 255, 255, 22);"
            "   border-bottom: %3 solid %4;"
            "   border-radius: 15px;"
            "}"
            "#CardFrame:hover {"
            "   background: %2;"
            "}"
        ).arg(gradient, gradientHover, bottomLineWidth, bottomLineColor)
    );
}

void NewUiWindow::updateRemoteCardActivityStyle(const QString &userId)
{
    if (userId.isEmpty() || !m_listWidget) {
        return;
    }
    QListWidgetItem *item = m_userItems.value(userId, nullptr);
    QWidget *w = item ? m_listWidget->itemWidget(item) : nullptr;
    QFrame *card = w ? w->findChild<QFrame*>("CardFrame") : nullptr;
    if (!card || card == m_localCard) {
        return;
    }

    const bool active = m_remoteActivityStates.value(userId, true);
    const bool selected = item && item->isSelected();
    const bool watching = (!m_watchingTargetId.isEmpty() && userId == m_watchingTargetId);
    const bool beingWatchedBy = isInMyRoomViewerList(userId);
    const QString base = QStringLiteral("rgba(32, 32, 36, 175)");
    const QString baseHover = QStringLiteral("rgba(40, 40, 45, 190)");
    const QString top = active ? QStringLiteral("rgba(0, 200, 83, 200)") : QStringLiteral("rgba(160, 90, 210, 200)");
    const QString topHover = active ? QStringLiteral("rgba(0, 220, 95, 230)") : QStringLiteral("rgba(180, 110, 230, 230)");
    const QString gradient = QStringLiteral("qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 %1, stop:0.38 %2, stop:0.62 %2, stop:1 %2)")
                                 .arg(top, base);
    const QString gradientHover = QStringLiteral("qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 %1, stop:0.38 %2, stop:0.62 %2, stop:1 %2)")
                                      .arg(topHover, baseHover);
    const QString topLineColor = watching ? QStringLiteral("rgba(0, 200, 83, 200)")
                                          : (beingWatchedBy ? QStringLiteral("rgba(0, 120, 212, 200)") : QString());
    const QString topLine = topLineColor.isEmpty()
                                ? QString()
                                : QStringLiteral("   border-top: 3px solid %1;").arg(topLineColor);
    const QString bottomLineColor = selected ? QStringLiteral("rgba(255, 102, 0, 220)") : QStringLiteral("rgba(255, 255, 255, 22)");
    const QString bottomLineWidth = selected ? QStringLiteral("3px") : QStringLiteral("1px");
    card->setStyleSheet(
        QStringLiteral(
            "#CardFrame {"
            "   background: %1;"
            "   border: 1px solid rgba(255, 255, 255, 22);"
            "   border-bottom: %3 solid %4;"
            "   border-radius: 15px;"
            "%5"
            "}"
            "#CardFrame:hover {"
            "   background: %2;"
            "}"
        ).arg(gradient, gradientHover, bottomLineWidth, bottomLineColor, topLine)
    );
}

void NewUiWindow::showExpiredInviteNotification(const QString &inviterName)
{
    if (m_expiredInviteNotification) {
        m_expiredInviteNotification->deleteLater();
        m_expiredInviteNotification = nullptr;
    }

    m_expiredInviteNotification = new QWidget(this);
    m_expiredInviteNotification->setObjectName("ExpiredInviteNotification");
    m_expiredInviteNotification->setStyleSheet(
        "QWidget#ExpiredInviteNotification {"
        "   background-color: rgba(40, 40, 45, 230);"
        "   border: 1px solid rgba(255, 80, 80, 100);"
        "   border-radius: 8px;"
        "}"
        "QLabel {"
        "   color: #e0e0e0;"
        "   font-size: 13px;"
        "}"
        "QPushButton {"
        "   background: transparent;"
        "   border: none;"
        "   color: #aaa;"
        "   font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "   color: #fff;"
        "}"
    );

    QHBoxLayout *layout = new QHBoxLayout(m_expiredInviteNotification);
    layout->setContentsMargins(12, 8, 8, 8);
    layout->setSpacing(10);

    QLabel *textLabel = new QLabel(m_expiredInviteNotification);
    
    QPushButton *closeBtn = new QPushButton("X", m_expiredInviteNotification);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFixedSize(20, 20);
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        if (m_expiredInviteNotification) {
            m_expiredInviteNotification->deleteLater();
            m_expiredInviteNotification = nullptr;
        }
    });

    layout->addWidget(textLabel);
    layout->addWidget(closeBtn);

    // Timer logic
    QDateTime startTime = QDateTime::currentDateTime();
    QTimer *updateTimer = new QTimer(m_expiredInviteNotification);
    auto updateFunc = [textLabel, inviterName, startTime]() {
        if (!textLabel) return;
        qint64 diff = startTime.secsTo(QDateTime::currentDateTime());
        int h = diff / 3600;
        int m = (diff % 3600) / 60;
        int s = diff % 60;
        textLabel->setText(QStringLiteral("用户%1的视频邀请已过时%2小时%3分钟%4秒")
                       .arg(inviterName).arg(h).arg(m).arg(s));
    };
    
    updateFunc();
    connect(updateTimer, &QTimer::timeout, m_expiredInviteNotification, updateFunc);
    updateTimer->start(1000);

    m_expiredInviteNotification->adjustSize();
    
    updateNotificationPositions();
    m_expiredInviteNotification->show();
    m_expiredInviteNotification->raise();
}

void NewUiWindow::showCancelledInviteNotification(const QString &inviterName, const QString &timeStr)
{
    if (m_expiredInviteNotification) {
        m_expiredInviteNotification->deleteLater();
        m_expiredInviteNotification = nullptr;
    }

    m_expiredInviteNotification = new QWidget(this);
    m_expiredInviteNotification->setObjectName("ExpiredInviteNotification");
    m_expiredInviteNotification->setStyleSheet(
        "QWidget#ExpiredInviteNotification {"
        "   background-color: rgba(40, 40, 45, 230);"
        "   border: 1px solid rgba(255, 80, 80, 100);"
        "   border-radius: 8px;"
        "}"
        "QLabel {"
        "   color: #e0e0e0;"
        "   font-size: 13px;"
        "}"
        "QPushButton {"
        "   background-color: transparent;"
        "   border: none;"
        "   color: #aaa;"
        "   font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "   color: #fff;"
        "}"
    );

    QHBoxLayout *layout = new QHBoxLayout(m_expiredInviteNotification);
    layout->setContentsMargins(12, 8, 8, 8);
    layout->setSpacing(10);

    QLabel *textLabel = new QLabel(m_expiredInviteNotification);
    textLabel->setText(QStringLiteral("%1在%2发送会议邀请，请及时联系")
                       .arg(inviterName).arg(timeStr));
    textLabel->setWordWrap(true);
    
    QPushButton *closeBtn = new QPushButton("X", m_expiredInviteNotification);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFixedSize(20, 20);
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        if (m_expiredInviteNotification) {
            m_expiredInviteNotification->deleteLater();
            m_expiredInviteNotification = nullptr;
        }
    });

    layout->addWidget(textLabel);
    layout->addWidget(closeBtn);

    m_expiredInviteNotification->adjustSize();
    updateNotificationPositions();
    m_expiredInviteNotification->show();
    m_expiredInviteNotification->raise();
}

void NewUiWindow::onTextMessageReceived(const QString &message)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    const QJsonObject obj = doc.object();
    const QString type = obj.value("type").toString();
    // Some messages might not have target_user_id but rely on viewer_id/target_id pair
    const QString targetUserId = obj.value("target_user_id").toString();
    
    // Check if the message is for me (if target_user_id is specified)
    if (!targetUserId.isEmpty() && targetUserId != m_myStreamId) {
        return; 
    }

    if (type == QStringLiteral("invite_to_room")) {
        // Fallback for legacy or direct messages
        const QString fromUserId = obj.value("from_user_id").toString();
        const QString fromUserName = obj.value("from_user_name").toString();
        const QString displayName = fromUserName.isEmpty() ? fromUserId : fromUserName;
        
        showInviteNotification(fromUserId, displayName, type);
    } else if (type == QStringLiteral("invite_cancelled")) {
        const QString viewerId = obj.value("viewer_id").toString();
        if (viewerId == m_myStreamId) {
            const QString targetId = obj.value("target_id").toString(); // Inviter
            const QString inviterName = obj.value("inviter_name").toString();
            const QString displayName = inviterName.isEmpty() ? targetId : inviterName;
            
            // Close active invite popup
            if (m_activeInviteNotification) {
                m_activeInviteNotification->deleteLater();
                m_activeInviteNotification = nullptr;
            }

            // Show cancelled notification
            showCancelledInviteNotification(displayName, QDateTime::currentDateTime().toString("HH:mm"));
        }
    } else if (type == QStringLiteral("watch_request_accepted")) {
        if (obj.value("is_invite").toBool()) {
            const QString viewerId = obj.value("viewer_id").toString();
            // Ensure this invite is meant for ME (as the viewer/invitee)
            if (viewerId != m_myStreamId) {
                return;
            }

            const QString targetId = obj.value("target_id").toString(); // Inviter
            const QString inviterName = obj.value("inviter_name").toString();
            const QString displayName = inviterName.isEmpty() ? targetId : inviterName;

            showInviteNotification(targetId, displayName, type);
        }
    } else if (type == QStringLiteral("start_streaming_request")) {
        const QString action = obj.value("action").toString();
        if (action == QStringLiteral("invite_response")) {
            // This is a response to our invite. Auto-approve it.
            const QString viewerId = obj.value("viewer_id").toString();
            
            // Close the wait dialog if it exists, as someone is joining
            if (m_inviteWaitDialog) {
                // Note: We don't disable ignoreAlone here immediately to ensure the room stays open 
                // until the connection is fully established. It will be disabled if the user manually cancels
                // or we can leave it enabled until the session ends.
                // However, to keep UI clean, we close the dialog.
                m_inviteWaitDialog->accept(); 
                m_inviteWaitDialog = nullptr;
                // We keep m_janusIgnoreAlone = true for a bit? 
                // Actually, if we close the dialog, the 'finished' signal fires.
                // In onInviteRequested, 'finished' calls janusSetIgnoreAlone(false).
                // So we are relying on the timeouts (now 30s/45s) to bridge the gap.
            }

            // 1. Send Accepted
            QJsonObject accepted;
            accepted["type"] = "watch_request_accepted";
            accepted["viewer_id"] = viewerId;
            accepted["target_id"] = m_myStreamId;
            QByteArray accData = QJsonDocument(accepted).toJson(QJsonDocument::Compact);
            if (m_streamClient && m_streamClient->isConnected()) {
                m_streamClient->sendTextMessage(QString::fromUtf8(accData));
            }
            
            // 2. Start Streaming if not already
            // Assuming startStreaming() or equivalent exists. 
            // In NewUiWindow, streaming might be managed via Janus or direct.
            // If we are using StreamClient for signaling, we might need to tell Janus to publish.
            // Let's assume onBroadcastBtnClicked logic or similar.
            // But wait, NewUiWindow usually handles publishing when "start_streaming" is received?
            // No, "start_streaming" is sent BY server to tell Publisher to publish.
            // Actually, in `websocket_server_with_routing.cpp`:
            // Server sends `start_streaming` to Publisher when `streaming_ok` is received from Publisher?
            // No, when `streaming_ok` is received from Publisher (forwarded to Viewer), server sends `start_streaming` to Publisher?
            
            // Let's look at server logic again (lines 416+):
            // if type == "streaming_ok":
            // ... forwards to viewer ...
            // ... sends "start_streaming" to Publisher (Target) ...
            
            // So, as Publisher, I must send "streaming_ok" to Viewer.
            // Then Server will send "start_streaming" to Me.
            // Then I start publishing.
            
            // 3. Send Streaming OK
            QJsonObject okMsg;
            okMsg["type"] = "streaming_ok";
            okMsg["viewer_id"] = viewerId;
            okMsg["target_id"] = m_myStreamId;
            // The URL is usually constructed by client or server. 
            // In MainWindow it sends "stream_url".
            // Here we can send it too.
            okMsg["stream_url"] = QString("%1/subscribe/%2").arg(AppConfig::wsBaseUrl(), m_myStreamId);
            
            QByteArray okData = QJsonDocument(okMsg).toJson(QJsonDocument::Compact);
            if (m_streamClient && m_streamClient->isConnected()) {
                m_streamClient->sendTextMessage(QString::fromUtf8(okData));
            }
        }
    } else if (type == QStringLiteral("start_streaming")) {
        // Handle server telling us to start streaming
        if (m_timer) {
            m_timer->start(100); // Start streaming at 10 FPS
        }
    }
}

void NewUiWindow::updateAcrylicState(bool enable)
{
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (enable) {
        tryEnableAcrylicBlur(hwnd, kAcrylicTintAbgr);
    } else {
        // Completely disable blur for maximum performance during drag on Win10
        // Using ACCENT_DISABLED instead of standard blur (ACCENT_ENABLE_BLURBEHIND)
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            auto fn = reinterpret_cast<SetWindowCompositionAttributeFn>(GetProcAddress(user32, "SetWindowCompositionAttribute"));
            if (fn) {
                ACCENT_POLICY policy{};
                policy.AccentState = ACCENT_DISABLED;
                WINDOWCOMPOSITIONATTRIBDATA data{};
                data.Attrib = WCA_ACCENT_POLICY;
                data.pvData = &policy;
                data.cbData = sizeof(policy);
                fn(hwnd, &data);
            }
        }
    }
#else
    Q_UNUSED(enable);
#endif
}

void NewUiWindow::checkForUpdates()
{
    // 使用 GitHub Releases 的 latest/download 链接获取 version.json
    // 这样可以确保获取到的是最新发布版附带的版本信息
    const QString updateUrl = "https://github.com/lixiaotaowx/IrulerDesk2.0/releases/latest/download/version.json";
    qInfo() << "[Update] Checking for updates from:" << updateUrl;
    if (m_autoUpdater) {
        m_autoUpdater->checkUpdate(updateUrl);
    }
}

void NewUiWindow::onUpdateAvailable(const QString &version, const QString &downloadUrl, const QString &description, bool force)
{
    QString msg = QStringLiteral("发现新版本: %1\n\n%2\n\n是否立即更新？").arg(version, description);
    
    if (force) {
        QMessageBox::warning(this, QStringLiteral("强制更新"), QStringLiteral("发现重要版本 %1，必须更新后才能继续使用。\n\n%2").arg(version, description));
        m_autoUpdater->downloadAndInstall();
        
        m_updateProgressDialog = new QProgressDialog(QStringLiteral("正在下载更新..."), QStringLiteral("取消"), 0, 100, this);
        m_updateProgressDialog->setWindowModality(Qt::WindowModal);
        m_updateProgressDialog->setAutoClose(false); // 下载完不要自动关闭，等待安装
        m_updateProgressDialog->setAutoReset(false);
        m_updateProgressDialog->setMinimumDuration(0);
        // 强制更新不允许取消
        m_updateProgressDialog->setCancelButton(nullptr); 
        m_updateProgressDialog->show();
    } else {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, QStringLiteral("发现新版本"), msg, QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            m_autoUpdater->downloadAndInstall();

            // 创建进度对话框
            m_updateProgressDialog = new QProgressDialog(QStringLiteral("正在下载更新..."), QStringLiteral("取消"), 0, 100, this);
            m_updateProgressDialog->setWindowModality(Qt::WindowModal);
            m_updateProgressDialog->setAutoClose(false);
            m_updateProgressDialog->setAutoReset(false);
            m_updateProgressDialog->setMinimumDuration(0);
            connect(m_updateProgressDialog, &QProgressDialog::canceled, m_autoUpdater, &AutoUpdater::cancel);
            m_updateProgressDialog->show();
        }
    }
}

void NewUiWindow::onUpdateDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (m_updateProgressDialog && bytesTotal > 0) {
        m_updateProgressDialog->setMaximum(100);
        m_updateProgressDialog->setValue(static_cast<int>(bytesReceived * 100 / bytesTotal));
        
        double receivedMB = bytesReceived / 1024.0 / 1024.0;
        double totalMB = bytesTotal / 1024.0 / 1024.0;
        m_updateProgressDialog->setLabelText(QStringLiteral("正在下载更新... %1 MB / %2 MB").arg(QString::number(receivedMB, 'f', 2), QString::number(totalMB, 'f', 2)));
    }
}

void NewUiWindow::onUpdateError(const QString &error)
{
    qWarning() << "[Update] Error:" << error;
    
    // 只有在显示了进度条（意味着用户同意更新或强制更新中）时才弹窗报错
    if (m_updateProgressDialog) {
        m_updateProgressDialog->close();
        m_updateProgressDialog->deleteLater();
        m_updateProgressDialog = nullptr;
        
        QMessageBox::warning(this, QStringLiteral("更新失败"), QStringLiteral("更新过程中发生错误：\n%1").arg(error));
    }
}

void NewUiWindow::showUserGuide()
{
    if (!m_userGuide) {
        m_userGuide = new NewUserGuide(this);
    }
    m_userGuide->show();
    // Mark as seen immediately so it doesn't pop up again automatically
    AppConfig::setLastTutorialVersion(AppConfig::applicationVersion());
}

void NewUiWindow::ensureCameraStarted()
{
    auto scheduleStartCheck = [this]() {
        QTimer::singleShot(1500, this, [this]() {
            if (!m_isCameraMode) {
                return;
            }
            if (!m_camera) {
                return;
            }
            bool hasFrame = false;
            if (m_videoSink) {
                QVideoFrame frame = m_videoSink->videoFrame();
                hasFrame = frame.isValid();
            }
            if (m_camera->isActive() || hasFrame) {
                return;
            }
            m_isCameraMode = false;
            stopCamera();
            QMessageBox::warning(this, QStringLiteral("摄像头启动失败"),
                QStringLiteral("摄像头可能被占用或无法启动。"));
            m_lastPreviewFramePixmap = QPixmap();
            m_lastPreviewSendPixmap = QPixmap();
            m_lastPreviewCaptureAtMs = 0;
            publishLocalScreenFrameTriggered("camera_error", true, true);
        });
    };

    if (m_camera) {
        if (m_camera->isActive()) return;
        connect(m_camera.data(), &QCamera::errorOccurred, this, [this](QCamera::Error error, const QString &errorString) {
            Q_UNUSED(error);
            if (!m_isCameraMode) {
                return;
            }
            m_isCameraMode = false;
            stopCamera();
            QMessageBox::warning(this, QStringLiteral("摄像头启动失败"),
                QStringLiteral("摄像头可能被占用或无法启动。\n错误信息: %1").arg(errorString));
            m_lastPreviewFramePixmap = QPixmap();
            m_lastPreviewSendPixmap = QPixmap();
            m_lastPreviewCaptureAtMs = 0;
            publishLocalScreenFrameTriggered("camera_error", true, true);
        }, Qt::UniqueConnection);
        m_camera->start();
        scheduleStartCheck();
        return;
    }

    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (cameras.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无法启动摄像头"), QStringLiteral("未检测到摄像头设备"));
        m_isCameraMode = false;
        m_lastPreviewFramePixmap = QPixmap();
        m_lastPreviewSendPixmap = QPixmap();
        m_lastPreviewCaptureAtMs = 0;
        publishLocalScreenFrameTriggered("camera_error", true, true);
        return;
    }

    m_camera.reset(new QCamera(cameras.first()));
    m_captureSession.reset(new QMediaCaptureSession());
    m_videoSink.reset(new QVideoSink());

    m_captureSession->setCamera(m_camera.data());
    m_captureSession->setVideoSink(m_videoSink.data());

    connect(m_camera.data(), &QCamera::errorOccurred, this, [this](QCamera::Error error, const QString &errorString) {
        Q_UNUSED(error);
        if (!m_isCameraMode) {
            return;
        }
        m_isCameraMode = false;
        stopCamera();
        QMessageBox::warning(this, QStringLiteral("摄像头启动失败"),
            QStringLiteral("摄像头可能被占用或无法启动。\n错误信息: %1").arg(errorString));
        m_lastPreviewFramePixmap = QPixmap();
        m_lastPreviewSendPixmap = QPixmap();
        m_lastPreviewCaptureAtMs = 0;
        publishLocalScreenFrameTriggered("camera_error", true, true);
    }, Qt::UniqueConnection);

    m_camera->start();
    scheduleStartCheck();
}

void NewUiWindow::stopCamera()
{
    if (m_camera) {
        m_camera->stop();
    }
}

#include "NewUiWindow.moc"
