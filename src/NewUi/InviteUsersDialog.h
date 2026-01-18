#pragma once

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QMap>
#include <QStringList>

class InviteUsersDialog : public QDialog {
    Q_OBJECT

public:
    explicit InviteUsersDialog(const QMap<QString, QString> &users, const QMap<QString, QPixmap> &avatars, QWidget *parent = nullptr);
    ~InviteUsersDialog();

    void setConfirmButtonText(const QString &text);

signals:
    void inviteRequested(const QStringList &userIds);

private slots:
    void onInviteClicked();

private:
    void setupUi();
    void loadUsers(const QMap<QString, QString> &users, const QMap<QString, QPixmap> &avatars);

    QListWidget *m_userListWidget;
    QPushButton *m_inviteBtn;
    QPushButton *m_closeBtn;
};
