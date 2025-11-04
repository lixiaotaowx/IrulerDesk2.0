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

        // 显示“切换屏幕中”等待弹窗（不可取消，直到成功）
        if (!m_progress) {
            m_progress = new QProgressDialog(QString(), QString(), 0, 0, this);
            m_progress->setWindowTitle(QStringLiteral("请稍候"));
            m_progress->setWindowModality(Qt::WindowModal);
            m_progress->setCancelButton(nullptr);
            m_progress->setMinimumDuration(0);
            m_progress->setAutoClose(false);
            m_progress->setAutoReset(false);
            m_progress->setMinimumSize(320, 120);
            m_progress->setStyleSheet(
                "QProgressDialog {"
                "    background-color: #2a2a2a;"
                "    color: white;"
                "    border: 1px solid #555;"
                "    border-radius: 6px;"
                "}"
                "QLabel {"
                "    color: white;"
                "    font-size: 14px;"
                "}"
                "QProgressBar {"
                "    background-color: #3a3a3a;"
                "    border: 1px solid #555;"
                "    border-radius: 4px;"
                "    height: 12px;"
                "    text-align: center;"
                "    color: white;"
                "}"
                "QProgressBar::chunk {"
                "    background-color: #4caf50;"
                "}"
                "QPushButton {"
                "    color: white;"
                "    background-color: #4a4a4a;"
                "    border: 1px solid #666;"
                "    padding: 4px 10px;"
                "    border-radius: 4px;"
                "}"
                "QPushButton:hover {"
                "    background-color: #5a5a5a;"
                "}"
            );
        }
        m_progress->setLabelText(QStringLiteral("切换屏幕中..."));
        m_progress->show();

        // 触发屏幕切换，等待首帧到达后由外部调用notifySwitchSucceeded
        emit screenSelected(idx);
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

void SystemSettingsWindow::notifySwitchSucceeded()
{
    if (m_progress && m_progress->isVisible()) {
        m_progress->close();
    }
    accept();
}