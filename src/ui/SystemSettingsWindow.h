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
#include <QGridLayout>
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
    void avatarSelected(int iconId);
    void manualApprovalEnabledChanged(bool enabled);

public slots:
    void notifySwitchSucceeded();

private:
    void populateScreens();
    void setupQualityControls();
    void setupUserNameControls();
    QFrame* setupAvatarControls();
    QFrame* setupManualApprovalControls();
    void loadAvatarImages();
    void selectAvatar(int iconId);
    bool eventFilter(QObject* obj, QEvent* event) override;
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
    QGridLayout* m_avatarGridLayout = nullptr;
    QList<QLabel*> m_avatarLabels;
    QLabel* m_selectedAvatarLabel = nullptr;
    int m_selectedIconId = -1;
    class QCheckBox* m_manualApprovalCheck = nullptr;
};

#endif // SYSTEMSETTINGSWINDOW_H
