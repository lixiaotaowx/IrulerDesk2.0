#include <QApplication>
#include "NewUiWindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    NewUiWindow w;
    w.show();
    return a.exec();
}
