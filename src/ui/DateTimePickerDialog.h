#pragma once

#include <QDialog>
#include <QDateTimeEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDateTime>
#include <QLineEdit>
#include <QGraphicsDropShadowEffect>

class DateTimePickerDialog : public QDialog {
    Q_OBJECT

public:
    explicit DateTimePickerDialog(const QString &initialContent = "", QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("添加任务提醒");
        setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
        setAttribute(Qt::WA_TranslucentBackground);
        resize(380, 260);

        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(10, 10, 10, 10);

        QWidget *container = new QWidget(this);
        container->setObjectName("container");
        container->setStyleSheet(
            "#container { "
            "background-color: #2d2d2d; "
            "border-radius: 12px; "
            "border: 1px solid #3e3e42; "
            "}"
            "QLabel { color: #e0e0e0; font-size: 14px; }"
            "QLineEdit { "
            "background: #1e1e1e; border: 1px solid #3e3e42; border-radius: 6px; padding: 8px; color: white; font-size: 14px; "
            "}"
            "QDateTimeEdit { "
            "background: #1e1e1e; border: 1px solid #3e3e42; border-radius: 6px; padding: 8px; color: white; font-size: 14px; "
            "}"
            "QDateTimeEdit::drop-down { border: none; }"
            "QCalendarWidget QWidget { background-color: #2d2d2d; color: white; }"
            "QCalendarWidget QToolButton { color: white; }"
        );

        QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(20);
        shadow->setColor(QColor(0, 0, 0, 80));
        shadow->setOffset(0, 4);
        container->setGraphicsEffect(shadow);

        QVBoxLayout *contentLayout = new QVBoxLayout(container);
        contentLayout->setSpacing(15);
        contentLayout->setContentsMargins(25, 25, 25, 25);

        // Title
        QLabel *title = new QLabel("添加任务 & 提醒", container);
        title->setStyleSheet("font-size: 18px; font-weight: bold; color: white;");
        contentLayout->addWidget(title);

        // Task Content Input
        QLabel *contentLabel = new QLabel("任务内容:", container);
        contentLayout->addWidget(contentLabel);
        
        m_contentEdit = new QLineEdit(initialContent, container);
        m_contentEdit->setPlaceholderText("请输入任务内容...");
        contentLayout->addWidget(m_contentEdit);

        // Time Input
        QLabel *timeLabel = new QLabel("提醒时间:", container);
        contentLayout->addWidget(timeLabel);

        m_dateTimeEdit = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(300), container); // Default +5 mins
        m_dateTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm");
        m_dateTimeEdit->setCalendarPopup(true);
        contentLayout->addWidget(m_dateTimeEdit);

        // Buttons
        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(10);
        
        QPushButton *cancelBtn = new QPushButton("取消", container);
        cancelBtn->setCursor(Qt::PointingHandCursor);
        cancelBtn->setStyleSheet(
            "QPushButton { background-color: #3e3e42; color: #e0e0e0; border: none; padding: 8px 20px; border-radius: 6px; font-weight: bold; }"
            "QPushButton:hover { background-color: #4e4e52; }"
        );
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

        QPushButton *okBtn = new QPushButton("确认添加", container);
        okBtn->setCursor(Qt::PointingHandCursor);
        okBtn->setStyleSheet(
            "QPushButton { background-color: #0078d4; color: white; border: none; padding: 8px 20px; border-radius: 6px; font-weight: bold; }"
            "QPushButton:hover { background-color: #1084d8; }"
        );
        connect(okBtn, &QPushButton::clicked, [this]() {
             if (m_contentEdit->text().trimmed().isEmpty()) {
                 m_contentEdit->setPlaceholderText("内容不能为空!");
                 m_contentEdit->setStyleSheet("background: #1e1e1e; border: 1px solid #ff4d4f; border-radius: 6px; padding: 8px; color: white; font-size: 14px;");
                 return;
             }
             accept();
        });

        btnLayout->addStretch();
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(okBtn);
        contentLayout->addLayout(btnLayout);

        mainLayout->addWidget(container);
    }

    QDateTime selectedDateTime() const {
        return m_dateTimeEdit->dateTime();
    }
    
    QString taskContent() const {
        return m_contentEdit->text().trimmed();
    }

private:
    QDateTimeEdit *m_dateTimeEdit;
    QLineEdit *m_contentEdit;
};
