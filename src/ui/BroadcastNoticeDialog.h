#pragma once

#include <QDialog>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QGraphicsDropShadowEffect>
#include "../common/TaskManager.h"

class BroadcastNoticeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BroadcastNoticeDialog(const QMap<QString, QString> &users, const QMap<QString, QPixmap> &avatars, QWidget *parent = nullptr);
    ~BroadcastNoticeDialog();

signals:
    void publishRequested(const QString &content, const QStringList &targets);

private:
    QTextEdit *m_editor;
    QPushButton *m_publishBtn;
    QPushButton *m_cancelBtn;
    QPushButton *m_selectUsersBtn;
    QLabel *m_targetLabel;
    
    // Task List related
    QListWidget *m_taskListWidget;
    QPushButton *m_addTaskBtn;

    QMap<QString, QString> m_users;
    QMap<QString, QPixmap> m_avatars;
    QStringList m_selectedTargets; // Empty means all

    void setupUi();
    void updateTargetLabel();
    void refreshTaskList();
};
