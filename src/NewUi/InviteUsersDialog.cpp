#include "InviteUsersDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QStandardPaths>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QFrame>

InviteUsersDialog::InviteUsersDialog(const QMap<QString, QString> &users, const QMap<QString, QPixmap> &avatars, QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(300, 400); // Default size
    
    setupUi();
    loadUsers(users, avatars);
}

InviteUsersDialog::~InviteUsersDialog() {}

void InviteUsersDialog::setupUi() {
    // Main layout
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Container frame for rounded corners and background
    QFrame *container = new QFrame(this);
    container->setObjectName("Container");
    container->setStyleSheet(
        "#Container {"
        "    background-color: #2d2d30;"
        "    border: 1px solid #3e3e42;"
        "    border-radius: 10px;"
        "}"
    );
    mainLayout->addWidget(container);

    QVBoxLayout *contentLayout = new QVBoxLayout(container);
    contentLayout->setContentsMargins(20, 20, 20, 20);
    contentLayout->setSpacing(15);

    // Title bar (Title + Close Button)
    QHBoxLayout *titleLayout = new QHBoxLayout();
    QLabel *titleLabel = new QLabel("邀请用户", this);
    titleLabel->setStyleSheet("color: white; font-size: 16px; font-weight: bold; background: transparent;");
    
    m_closeBtn = new QPushButton("×", this);
    m_closeBtn->setFixedSize(24, 24);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(
        "QPushButton {"
        "    background: transparent;"
        "    color: #aaaaaa;"
        "    border: none;"
        "    font-size: 18px;"
        "    font-weight: bold;"
        "}"
        "QPushButton:hover { color: white; }"
    );
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    titleLayout->addWidget(m_closeBtn);
    contentLayout->addLayout(titleLayout);

    // User List
    m_userListWidget = new QListWidget(this);
    m_userListWidget->setFrameShape(QFrame::NoFrame);
    m_userListWidget->setStyleSheet(
        "QListWidget {"
        "    background: transparent;"
        "    color: white;"
        "    outline: none;"
        "}"
        "QListWidget::item {"
        "    border-bottom: 1px solid #3e3e42;"
        "    padding: 5px;"
        "}"
        "QListWidget::item:hover { background-color: #3e3e42; }"
        "QListWidget::item:selected { background-color: #3e3e42; }"
    );
    m_userListWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(m_userListWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        QWidget *widget = m_userListWidget->itemWidget(item);
        if (widget) {
            QCheckBox *checkBox = widget->findChild<QCheckBox*>();
            if (checkBox) {
                checkBox->setChecked(!checkBox->isChecked());
            }
        }
    });
    contentLayout->addWidget(m_userListWidget);

    // Invite Button
    m_inviteBtn = new QPushButton("发起邀请", this);
    m_inviteBtn->setCursor(Qt::PointingHandCursor);
    m_inviteBtn->setFixedHeight(36);
    m_inviteBtn->setStyleSheet(
        "QPushButton {"
        "    background-color: #0078d4;"
        "    color: white;"
        "    border: none;"
        "    border-radius: 4px;"
        "    font-size: 14px;"
        "}"
        "QPushButton:hover { background-color: #1084d8; }"
        "QPushButton:pressed { background-color: #006cc1; }"
    );
    connect(m_inviteBtn, &QPushButton::clicked, this, &InviteUsersDialog::onInviteClicked);
    contentLayout->addWidget(m_inviteBtn);
}

void InviteUsersDialog::loadUsers(const QMap<QString, QString> &users, const QMap<QString, QPixmap> &avatars) {
    for (auto it = users.begin(); it != users.end(); ++it) {
        QString userId = it.key();
        QString userName = it.value();

        QListWidgetItem *item = new QListWidgetItem(m_userListWidget);
        item->setSizeHint(QSize(0, 50));
        
        QWidget *itemWidget = new QWidget();
        itemWidget->setStyleSheet("background: transparent;");
        QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
        itemLayout->setContentsMargins(5, 0, 5, 0);
        itemLayout->setSpacing(10);

        // Checkbox
        QCheckBox *checkBox = new QCheckBox();
        checkBox->setStyleSheet(
            "QCheckBox::indicator { width: 18px; height: 18px; }"
            "QCheckBox::indicator:unchecked { border: 1px solid #888; background: transparent; border-radius: 3px; }"
            "QCheckBox::indicator:checked { border: 1px solid #0078d4; background: #0078d4; border-radius: 3px; }"
        );
        itemLayout->addWidget(checkBox);

        // Avatar
        QLabel *avatarLabel = new QLabel();
        avatarLabel->setFixedSize(32, 32);
        
        QPixmap avatarPix;
        if (avatars.contains(userId)) {
            avatarPix = avatars.value(userId);
        }

        if (avatarPix.isNull()) {
            // Try loading from temp dir as fallback
            QString avatarDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/iruler_avatars/";
            avatarPix = QPixmap(avatarDir + userId + ".png");
        }

        if (avatarPix.isNull()) {
            // Default placeholder (simple circle)
            avatarPix = QPixmap(32, 32);
            avatarPix.fill(Qt::transparent);
            QPainter p(&avatarPix);
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(QColor("#555"));
            p.setPen(Qt::NoPen);
            p.drawEllipse(0, 0, 32, 32);
        } else {
             // Circular mask
             QPixmap rounded(32, 32);
             rounded.fill(Qt::transparent);
             QPainter p(&rounded);
             p.setRenderHint(QPainter::Antialiasing);
             QPainterPath path;
             path.addEllipse(0, 0, 32, 32);
             p.setClipPath(path);
             p.drawPixmap(0, 0, 32, 32, avatarPix);
             avatarPix = rounded;
        }
        avatarLabel->setPixmap(avatarPix);
        itemLayout->addWidget(avatarLabel);

        // Name
        QLabel *nameLabel = new QLabel(userName);
        nameLabel->setStyleSheet("color: white; font-size: 14px; background: transparent;");
        itemLayout->addWidget(nameLabel);
        
        itemLayout->addStretch();

        item->setData(Qt::UserRole, userId);
        m_userListWidget->setItemWidget(item, itemWidget);
    }
}

void InviteUsersDialog::setConfirmButtonText(const QString &text) {
    if (m_inviteBtn) {
        m_inviteBtn->setText(text);
    }
}

void InviteUsersDialog::onInviteClicked() {
    QStringList invitedIds;
    for (int i = 0; i < m_userListWidget->count(); ++i) {
        QListWidgetItem *item = m_userListWidget->item(i);
        QWidget *widget = m_userListWidget->itemWidget(item);
        if (widget) {
            QCheckBox *checkBox = widget->findChild<QCheckBox*>();
            if (checkBox && checkBox->isChecked()) {
                invitedIds.append(item->data(Qt::UserRole).toString());
            }
        }
    }
    emit inviteRequested(invitedIds);
    accept();
}
