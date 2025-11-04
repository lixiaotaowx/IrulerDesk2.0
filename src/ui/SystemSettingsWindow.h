#ifndef SYSTEMSETTINGSWINDOW_H
#define SYSTEMSETTINGSWINDOW_H

#include <QDialog>
#include <QListWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>

class SystemSettingsWindow : public QDialog {
    Q_OBJECT
public:
    explicit SystemSettingsWindow(QWidget* parent = nullptr);

signals:
    void screenSelected(int index);

private:
    void populateScreens();
    QListWidget* m_list;
};

#endif // SYSTEMSETTINGSWINDOW_H