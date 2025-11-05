#ifndef SYSTEMSETTINGSWINDOW_H
#define SYSTEMSETTINGSWINDOW_H

#include <QDialog>
#include <QListWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QProgressDialog>
#include <QButtonGroup>
#include <QRadioButton>

class SystemSettingsWindow : public QDialog {
    Q_OBJECT
public:
    explicit SystemSettingsWindow(QWidget* parent = nullptr);

signals:
    void screenSelected(int index);
    void localQualitySelected(const QString& quality);

public slots:
    void notifySwitchSucceeded();

private:
    void populateScreens();
    void setupQualityControls();
    QListWidget* m_list;
    QProgressDialog* m_progress = nullptr;
    // 质量选择控件
    QLabel* m_qualityLabel = nullptr;
    QButtonGroup* m_qualityGroup = nullptr;
    QRadioButton* m_lowBtn = nullptr;
    QRadioButton* m_mediumBtn = nullptr;
    QRadioButton* m_highBtn = nullptr;
};

#endif // SYSTEMSETTINGSWINDOW_H