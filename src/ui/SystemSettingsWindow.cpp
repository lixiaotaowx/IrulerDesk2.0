#include "SystemSettingsWindow.h"
#include <QGuiApplication>
#include <QScreen>

SystemSettingsWindow::SystemSettingsWindow(QWidget* parent)
    : QDialog(parent), m_list(new QListWidget(this))
{
    setWindowTitle("系统设置 - 屏幕切换");
    setModal(true);
    resize(360, 300);

    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* tip = new QLabel("选择一个屏幕作为推流源", this);
    layout->addWidget(tip);
    layout->addWidget(m_list);

    populateScreens();

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item){
        int idx = item->data(Qt::UserRole).toInt();
        emit screenSelected(idx);
        accept();
    });
}

void SystemSettingsWindow::populateScreens()
{
    const auto screens = QGuiApplication::screens();
    m_list->clear();
    for (int i = 0; i < screens.size(); ++i) {
        QScreen* s = screens[i];
        QString name = s->name();
        QSize size = s->size();
        QRect geom = s->geometry();
        QString text = QString("屏幕 %1 - %2 (%3x%4 @%5,%6)")
                           .arg(i)
                           .arg(name)
                           .arg(size.width()).arg(size.height())
                           .arg(geom.x()).arg(geom.y());
        auto* item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, i);
        m_list->addItem(item);
    }
}