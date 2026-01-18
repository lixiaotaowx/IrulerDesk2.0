#include "BroadcastNoticeDialog.h"
#include "DateTimePickerDialog.h"
#include "../NewUi/InviteUsersDialog.h"
#include <QHBoxLayout>
#include <QApplication>
#include <QCheckBox>

BroadcastNoticeDialog::BroadcastNoticeDialog(const QMap<QString, QString> &users, const QMap<QString, QPixmap> &avatars, QWidget *parent)
    : QDialog(parent)
    , m_users(users)
    , m_avatars(avatars)
{
    setupUi();
    connect(&TaskManager::instance(), &TaskManager::tasksChanged, this, &BroadcastNoticeDialog::refreshTaskList);
    refreshTaskList();
}

BroadcastNoticeDialog::~BroadcastNoticeDialog()
{
}

void BroadcastNoticeDialog::setupUi()
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    QWidget *container = new QWidget(this);
    container->setObjectName("container");
    container->setStyleSheet(
        "#container { "
        "background-color: #2d2d2d; " // Dark theme to match InviteUsersDialog
        "border-radius: 12px; "
        "border: 1px solid #3e3e42; "
        "}"
        "QLabel { color: #e0e0e0; }"
    );

    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(20);
    shadow->setColor(QColor(0, 0, 0, 50));
    shadow->setOffset(0, 4);
    container->setGraphicsEffect(shadow);

    QVBoxLayout *contentLayout = new QVBoxLayout(container);
    contentLayout->setSpacing(15);
    contentLayout->setContentsMargins(20, 20, 20, 20);

    // Title + Close Button
    QHBoxLayout *headerLayout = new QHBoxLayout();
    QLabel *titleLabel = new QLabel(QStringLiteral("发布公告 & 任务代办"), container);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    
    QPushButton *closeBtn = new QPushButton("×", container);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet("background: transparent; color: #aaaaaa; border: none; font-size: 18px; font-weight: bold;");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    headerLayout->addWidget(closeBtn);
    contentLayout->addLayout(headerLayout);

    // --- Announcement Section ---
    QLabel *announceLabel = new QLabel("公告内容:", container);
    announceLabel->setStyleSheet("font-weight: bold;");
    contentLayout->addWidget(announceLabel);

    m_editor = new QTextEdit(container);
    m_editor->setPlaceholderText(QStringLiteral("请输入公告内容..."));
    m_editor->setStyleSheet(
        "QTextEdit { "
        "border: 1px solid #3e3e42; "
        "border-radius: 6px; "
        "padding: 8px; "
        "background-color: #1e1e1e; "
        "color: #e0e0e0; "
        "font-size: 14px; "
        "}"
    );
    contentLayout->addWidget(m_editor);

    // User Selection
    QHBoxLayout *targetLayout = new QHBoxLayout();
    m_targetLabel = new QLabel("发送给: 全员", container);
    targetLayout->addWidget(m_targetLabel);
    targetLayout->addStretch();
    
    m_selectUsersBtn = new QPushButton("选择用户", container);
    m_selectUsersBtn->setCursor(Qt::PointingHandCursor);
    m_selectUsersBtn->setStyleSheet("background-color: #444; color: white; border: none; border-radius: 4px; padding: 4px 10px;");
    connect(m_selectUsersBtn, &QPushButton::clicked, [this]() {
        InviteUsersDialog dlg(m_users, m_avatars, this);
        dlg.setWindowTitle("选择公告接收人");
        dlg.setConfirmButtonText("确认");
        connect(&dlg, &InviteUsersDialog::inviteRequested, [this](const QStringList &ids) {
            m_selectedTargets = ids;
            updateTargetLabel();
        });
        dlg.exec();
    });
    targetLayout->addWidget(m_selectUsersBtn);
    contentLayout->addLayout(targetLayout);

    // Publish Button
    m_publishBtn = new QPushButton(QStringLiteral("发布公告"), container);
    m_publishBtn->setCursor(Qt::PointingHandCursor);
    m_publishBtn->setFixedHeight(36);
    m_publishBtn->setStyleSheet(
        "QPushButton { "
        "background-color: #0078d4; "
        "color: white; "
        "border: none; "
        "border-radius: 18px; "
        "font-weight: bold; "
        "}"
        "QPushButton:hover { background-color: #1084d8; }"
    );
    connect(m_publishBtn, &QPushButton::clicked, [this]() {
        if (!m_editor->toPlainText().trimmed().isEmpty()) {
            emit publishRequested(m_editor->toPlainText(), m_selectedTargets);
            // We don't close the dialog immediately if users want to manage tasks too?
            // Usually "Publish" implies action done. But the user asked for "Tasks below UI".
            // Maybe clear editor after publish?
            m_editor->clear();
            // Optional: close or keep open. Let's keep open as it's a "Work Center".
            // Or maybe close? "发布公告的功能也加上...".
            // If it's a dialog, usually "Publish" is the main action.
            // But if I put Tasks here, I shouldn't close it when publishing announcement.
            // Let's just show a toast "Announcement Sent" and clear.
        }
    });
    contentLayout->addWidget(m_publishBtn);

    // Separator
    QFrame *line = new QFrame(container);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setStyleSheet("background-color: #3e3e42;");
    contentLayout->addWidget(line);

    // --- Task List Section ---
    QHBoxLayout *taskHeader = new QHBoxLayout();
    QLabel *taskLabel = new QLabel("任务代办:", container);
    taskLabel->setStyleSheet("font-weight: bold;");
    taskHeader->addWidget(taskLabel);
    taskHeader->addStretch();
    
    m_addTaskBtn = new QPushButton("+ 添加任务", container);
    m_addTaskBtn->setCursor(Qt::PointingHandCursor);
    m_addTaskBtn->setStyleSheet("background-color: #28a745; color: white; border: none; border-radius: 4px; padding: 4px 10px;");
    connect(m_addTaskBtn, &QPushButton::clicked, [this]() {
        QString initialContent = m_editor->toPlainText().trimmed();
        DateTimePickerDialog dlg(initialContent, this);
        if (dlg.exec() == QDialog::Accepted) {
            QString finalContent = dlg.taskContent();
            if (finalContent.isEmpty()) return; // Should be handled by dialog validation
            
            TaskManager::instance().addTask(finalContent, dlg.selectedDateTime());
            refreshTaskList();
            
            // If the user used the exact text from the editor, clear the editor
            if (!initialContent.isEmpty() && finalContent == initialContent) {
                 m_editor->clear();
            }
        }
    });
    taskHeader->addWidget(m_addTaskBtn);
    contentLayout->addLayout(taskHeader);

    m_taskListWidget = new QListWidget(container);
    m_taskListWidget->setStyleSheet(
        "QListWidget { background: #1e1e1e; border: 1px solid #3e3e42; border-radius: 6px; outline: none; }"
        "QListWidget::item { padding: 5px; border-bottom: 1px solid #2d2d2d; }"
        "QListWidget::item:hover { background-color: #2d2d2d; }"
    );
    contentLayout->addWidget(m_taskListWidget);

    contentLayout->addStretch();
    mainLayout->addWidget(container);
    
    setFixedSize(450, 600); // Increased size
}

void BroadcastNoticeDialog::updateTargetLabel()
{
    if (m_selectedTargets.isEmpty()) {
        m_targetLabel->setText("发送给: 全员");
        m_targetLabel->setToolTip("");
    } else {
        QStringList names;
        for (const QString &uid : m_selectedTargets) {
            if (m_users.contains(uid)) {
                names.append(m_users.value(uid));
            } else {
                names.append(uid);
            }
        }
        
        QString text = "发送给: " + names.join(", ");
        QFontMetrics fm(m_targetLabel->font());
        QString elided = fm.elidedText(text, Qt::ElideRight, 300);
        m_targetLabel->setText(elided);
        m_targetLabel->setToolTip(text);
    }
}

void BroadcastNoticeDialog::refreshTaskList()
{
    m_taskListWidget->clear();
    QList<Task> tasks = TaskManager::instance().getTasks();
    
    // Sort by time?
    // std::sort(tasks.begin(), tasks.end(), ...);

    for (const Task &t : tasks) {
        QListWidgetItem *item = new QListWidgetItem(m_taskListWidget);
        QWidget *w = new QWidget();
        QHBoxLayout *l = new QHBoxLayout(w);
        l->setContentsMargins(5, 2, 5, 2);
        
        QCheckBox *cb = new QCheckBox();
        cb->setChecked(t.isCompleted);
        connect(cb, &QCheckBox::toggled, [t, this](bool checked) {
            TaskManager::instance().completeTask(t.id, checked);
            // Don't refresh immediately to avoid item jumping, or maybe strikeout text?
        });
        
        QLabel *content = new QLabel(t.content);
        if (t.isCompleted) {
             QFont f = content->font();
             f.setStrikeOut(true);
             content->setFont(f);
             content->setStyleSheet("color: #777;");
        } else {
             content->setStyleSheet("color: #e0e0e0;");
        }

        QLabel *time = new QLabel(t.reminderTime.toString("MM-dd HH:mm"));
        time->setStyleSheet("color: #aaa; font-size: 11px;");

        QPushButton *del = new QPushButton("×");
        del->setFixedSize(20, 20);
        del->setStyleSheet("background: transparent; color: #aaa; border: none; font-weight: bold;");
        connect(del, &QPushButton::clicked, [t, this]() {
            TaskManager::instance().removeTask(t.id);
            refreshTaskList();
        });

        l->addWidget(cb);
        l->addWidget(content, 1);
        l->addWidget(time);
        l->addWidget(del);
        
        item->setSizeHint(w->sizeHint());
        m_taskListWidget->setItemWidget(item, w);
    }
}
