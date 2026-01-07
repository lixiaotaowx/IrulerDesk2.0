#pragma once

#include <QDialog>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QGraphicsDropShadowEffect>

class BroadcastNoticeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BroadcastNoticeDialog(QWidget *parent = nullptr);
    ~BroadcastNoticeDialog();

signals:
    void publishRequested(const QString &content);

private:
    QTextEdit *m_editor;
    QPushButton *m_publishBtn;
    QPushButton *m_cancelBtn;

    void setupUi();
};
