#include <QApplication>
#include "NewUiWindow.h"
#include "../common/AppConfig.h"
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QWebEngineUrlScheme>

int main(int argc, char *argv[])
{
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
    QApplication a(argc, argv);
    AppConfig::applyApplicationInfo(a);
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    NewUiWindow w;
    w.show();
    return a.exec();
}
