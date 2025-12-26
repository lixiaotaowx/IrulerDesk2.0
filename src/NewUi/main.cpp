#include <QApplication>
#include "NewUiWindow.h"
#include "../common/AppConfig.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    AppConfig::applyApplicationInfo(a);
    NewUiWindow w;
    w.show();
    return a.exec();
}
