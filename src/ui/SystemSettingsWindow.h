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
#include <QLineEdit>
#include <QScrollArea>
#include <QList>
#include <QFrame>

class SystemSettingsWindow : public QDialog {
    Q_OBJECT
public:
    explicit SystemSettingsWindow(QWidget* parent = nullptr);

signals:
    void screenSelected(int index);
    void localQualitySelected(const QString& quality);
    void userNameChanged(const QString& name);
    void manualApprovalEnabledChanged(bool enabled);

public slots:
    void notifySwitchSucceeded();

private:
    void populateScreens();
    void setupQualityControls();
    void setupUserNameControls();
    QFrame* setupManualApprovalControls();
    QListWidget* m_list;
    QProgressDialog* m_progress = nullptr;
    // 质量选择控件
    QLabel* m_qualityLabel = nullptr;
    QButtonGroup* m_qualityGroup = nullptr;
    QRadioButton* m_lowBtn = nullptr;
    QRadioButton* m_mediumBtn = nullptr;
    QRadioButton* m_highBtn = nullptr;
    QRadioButton* m_extremeBtn = nullptr;
    QLabel* m_userNameLabel = nullptr;
    QLineEdit* m_userNameEdit = nullptr;
    QPushButton* m_userNameConfirmBtn = nullptr;
    class QCheckBox* m_manualApprovalCheck = nullptr;
};

#endif // SYSTEMSETTINGSWINDOW_H
