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

class AutoUpdater;

class SystemSettingsWindow : public QDialog {
    Q_OBJECT
public:
    explicit SystemSettingsWindow(QWidget* parent = nullptr);
    ~SystemSettingsWindow(); // Add destructor

signals:
    void screenSelected(int index);
    void localQualitySelected(const QString& quality);
    void userNameChanged(const QString& name);
    void manualApprovalEnabledChanged(bool enabled);
    void onlineNotificationEnabledChanged(bool enabled);
    void storyboardUrlChanged(const QString& url);
    void function2UrlChanged(const QString& url);
    void function3UrlChanged(const QString& url);

public slots:
    void notifySwitchSucceeded();

private slots:
    void onCheckUpdateClicked();
    void onUpdateAvailable(const QString &version, const QString &downloadUrl, const QString &description, bool force);
    void onUpdateDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onUpdateError(const QString &error);

private:
    void populateScreens();
    void setupQualityControls();
    void setupUserNameControls();
    QFrame* setupManualApprovalControls();
    QFrame* setupNotificationControls();
    QFrame* setupConfigControls();
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
    class QCheckBox* m_onlineNotificationCheck = nullptr;
    QLabel* m_storyboardUrlLabel = nullptr;
    QLineEdit* m_storyboardUrlEdit = nullptr;
    QPushButton* m_storyboardUrlConfirmBtn = nullptr;
    QLabel* m_function2UrlLabel = nullptr;
    QLineEdit* m_function2UrlEdit = nullptr;
    QPushButton* m_function2UrlConfirmBtn = nullptr;
    QLabel* m_function3UrlLabel = nullptr;
    QLineEdit* m_function3UrlEdit = nullptr;
    QPushButton* m_function3UrlConfirmBtn = nullptr;

    // Auto Update
    AutoUpdater* m_autoUpdater = nullptr;
    QPushButton* m_checkUpdateBtn = nullptr;
    QProgressDialog* m_updateProgress = nullptr;
};

#endif // SYSTEMSETTINGSWINDOW_H
