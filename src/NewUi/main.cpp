#include <QApplication>
#include "NewUiWindow.h"
#include "../common/AppConfig.h"
#include "../common/CrashGuard.h"
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QWebEngineUrlScheme>
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif
static void enablePerMonitorDpiAwareness()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) {
        return;
    }
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
    auto fn = reinterpret_cast<SetProcessDpiAwarenessContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (!fn) {
        return;
    }
    fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}
#endif

int main(int argc, char *argv[])
{
    CrashGuard::install();
    {
        static bool done = false;
        if (!done) {
            done = true;
            QWebEngineUrlScheme scheme("iruler");
            scheme.setSyntax(QWebEngineUrlScheme::Syntax::HostAndPort);
            scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme | QWebEngineUrlScheme::ContentSecurityPolicyIgnored);
            QWebEngineUrlScheme::registerScheme(scheme);
        }
    }

#ifdef _WIN32
    enablePerMonitorDpiAwareness();
#endif

    QApplication a(argc, argv);
    AppConfig::applyApplicationInfo(a);
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    NewUiWindow w;
    w.show();
    return a.exec();
}
