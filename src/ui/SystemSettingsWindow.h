#ifndef SYSTEMSETTINGSWINDOW_H
#define SYSTEMSETTINGSWINDOW_H

#include <QDialog>
#include <QListWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QProgressDialog>

class SystemSettingsWindow : public QDialog {
    Q_OBJECT
public:
    explicit SystemSettingsWindow(QWidget* parent = nullptr);

signals:
    void screenSelected(int index);

public slots:
    void notifySwitchSucceeded();

private:
    void populateScreens();
    QListWidget* m_list;
    QProgressDialog* m_progress = nullptr;
};

#endif // SYSTEMSETTINGSWINDOW_H