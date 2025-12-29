#include <QApplication>
#include "NewUiWindow.h"
#include "../common/AppConfig.h"
#include <QNetworkProxy>
#include <QNetworkProxyFactory>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    AppConfig::applyApplicationInfo(a);
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    NewUiWindow w;
    w.show();
    return a.exec();
}
