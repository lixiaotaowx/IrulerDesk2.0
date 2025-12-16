#include "SystemSettingsWindow.h"
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QFile>
#include <QTextStream>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QGridLayout>
#include <QPixmap>
#include <QCoreApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QCheckBox>

SystemSettingsWindow::SystemSettingsWindow(QWidget* parent)
    : QDialog(parent), m_list(new QListWidget(this))
{
    setWindowTitle("系统设置");
    setModal(true);
    resize(520, 620);
    setStyleSheet(
        "QDialog { background-color: #1a1a1a; }"
        "QLabel { color: #eaeaea; }"
        "QFrame#card { background-color: #161616; border: 1px solid #2e2e2e; border-radius: 10px; }"
        "QGroupBox { border: 1px solid #2e2e2e; border-radius: 10px; margin-top: 16px; color: #9bd28f; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 4px 8px; }"
        "QPushButton { color: #ffffff; background-color: #2b2b2b; border: 1px solid #444; padding: 6px 14px; border-radius: 6px; }"
        "QPushButton:hover { background-color: #3a3a3a; }"
        "QRadioButton { color: #dddddd; }"
        "QLineEdit { background-color: #121212; color: #f0f0f0; border: 1px solid #333; border-radius: 6px; padding: 6px; }"
        "QListWidget { background-color: #121212; color: #dddddd; border: 1px solid #2e2e2e; border-radius: 10px; padding: 6px; }"
        "QListWidget::item { height: 34px; }"
        "QListWidget::item:selected { background-color: #2a3b2f; color: #ffffff; border: none; }"
    );

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet(
        "QScrollArea { border: none; }"
        "QScrollBar:vertical { background-color: #222; width: 16px; margin: 0; border: none; }"
        "QScrollBar::handle:vertical { background-color: #555; min-height: 32px; border-radius: 8px; }"
        "QScrollBar::handle:vertical:hover { background-color: #666; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal { height: 16px; background: #222; }"
        "QScrollBar::handle:horizontal { background: #555; min-width: 32px; border-radius: 8px; }"
        "QScrollBar::handle:horizontal:hover { background: #666; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
    );
    QWidget* content = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(16);

    QLabel* header = new QLabel("系统设置", content);
    QFont hf = header->font(); hf.setPointSize(16); hf.setBold(true); header->setFont(hf);
    layout->addWidget(header);

    setupUserNameControls();
    QFrame* userCard = new QFrame(this); userCard->setObjectName("card");
    QVBoxLayout* userBox = new QVBoxLayout(userCard); userBox->setContentsMargins(12, 12, 12, 12); userBox->setSpacing(10);
    QHBoxLayout* userRow = new QHBoxLayout();
    if (m_userNameLabel) userRow->addWidget(m_userNameLabel);
    if (m_userNameEdit) { userRow->addWidget(m_userNameEdit, 1); }
    if (m_userNameConfirmBtn) userRow->addWidget(m_userNameConfirmBtn);
    userBox->addLayout(userRow);
    layout->addWidget(userCard);

    QLabel* tip = new QLabel("选择一个屏幕作为推流源", this);
    tip->setStyleSheet("QLabel { color: #9bd28f; }");
    QFrame* screenCard = new QFrame(this); screenCard->setObjectName("card");
    QVBoxLayout* screenBox = new QVBoxLayout(screenCard); screenBox->setContentsMargins(12, 12, 12, 12); screenBox->setSpacing(10);
    screenBox->addWidget(tip);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    screenBox->addWidget(m_list, 1);
    layout->addWidget(screenCard, 1);

    setupQualityControls();
    QFrame* qualityCard = new QFrame(this); qualityCard->setObjectName("card");
    QVBoxLayout* qualityBox = new QVBoxLayout(qualityCard); qualityBox->setContentsMargins(12, 12, 12, 12); qualityBox->setSpacing(10);
    if (m_qualityLabel) qualityBox->addWidget(m_qualityLabel);
    QHBoxLayout* qualityRow = new QHBoxLayout();
    qualityRow->addWidget(m_lowBtn);
    qualityRow->addWidget(m_mediumBtn);
    qualityRow->addWidget(m_highBtn);
    qualityRow->addWidget(m_extremeBtn);
    qualityRow->addStretch();
    qualityBox->addLayout(qualityRow);
    layout->addWidget(qualityCard);

    QFrame* manualCard = setupManualApprovalControls();
    layout->addWidget(manualCard);

    QFrame* avatarCard = setupAvatarControls();
    layout->addWidget(avatarCard);

    populateScreens();

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item){
        int idx = item->data(Qt::UserRole).toInt();

        // 触发屏幕切换
        emit screenSelected(idx);
    });

    scroll->setWidget(content);
    root->addWidget(scroll);
}

void SystemSettingsWindow::setupUserNameControls()
{
    m_userNameLabel = new QLabel("用户名：", this);
    m_userNameEdit = new QLineEdit(this);
    m_userNameConfirmBtn = new QPushButton(QStringLiteral("确定"), this);
    QString appDir = QApplication::applicationDirPath();
    QString configPath = appDir + "/config/app_config.txt";
    QFile f(configPath);
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("user_name=")) {
                QString v = line.mid(QString("user_name=").length()).trimmed();
                m_userNameEdit->setText(v);
                break;
            }
        }
        f.close();
    }
    connect(m_userNameEdit, &QLineEdit::editingFinished, this, [this]() {
        emit userNameChanged(m_userNameEdit->text().trimmed());
    });
    connect(m_userNameEdit, &QLineEdit::returnPressed, this, [this]() {
        emit userNameChanged(m_userNameEdit->text().trimmed());
    });
    connect(m_userNameConfirmBtn, &QPushButton::clicked, this, [this]() {
        emit userNameChanged(m_userNameEdit->text().trimmed());
    });
}

void SystemSettingsWindow::populateScreens()
{
    const auto screens = QGuiApplication::screens();
    m_list->clear();
    for (int i = 0; i < screens.size(); ++i) {
        QScreen* s = screens[i];
        QString name = s->name();
        QSize size = s->size();
        QRect geom = s->geometry();
        QString text = QString("屏幕 %1 - %2 (%3x%4 @%5,%6)")
                           .arg(i)
                           .arg(name)
                           .arg(size.width()).arg(size.height())
                           .arg(geom.x()).arg(geom.y());
        auto* item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, i);
        m_list->addItem(item);
    }
    QString appDir = QApplication::applicationDirPath();
    QString configPath = appDir + "/config/app_config.txt";
    QFile f(configPath);
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("screen_index=")) {
                bool ok = false;
                int idx = line.mid(QString("screen_index=").length()).trimmed().toInt(&ok);
                if (ok && idx >= 0 && idx < m_list->count()) {
                    m_list->setCurrentRow(idx);
                }
                break;
            }
        }
        f.close();
    }
}

void SystemSettingsWindow::setupQualityControls()
{
    m_qualityLabel = new QLabel("视频质量：", this);
    m_qualityLabel->setStyleSheet("QLabel { font-size: 13px; }");
    m_qualityGroup = new QButtonGroup(this);
    m_lowBtn = new QRadioButton("低", this);
    m_mediumBtn = new QRadioButton("中", this);
    m_highBtn = new QRadioButton("高", this);
    m_extremeBtn = new QRadioButton("极高", this);
    m_qualityGroup->addButton(m_lowBtn);
    m_qualityGroup->addButton(m_mediumBtn);
    m_qualityGroup->addButton(m_highBtn);
    m_qualityGroup->addButton(m_extremeBtn);
    m_mediumBtn->setChecked(true);

    // 从配置读取默认本地质量
    QString appDir = QApplication::applicationDirPath();
    QString configPath = appDir + "/config/app_config.txt";
    QFile f(configPath);
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("local_quality=")) {
                QString q = line.mid(QString("local_quality=").length()).trimmed();
                if (q == "low") m_lowBtn->setChecked(true);
                else if (q == "medium") m_mediumBtn->setChecked(true);
                else if (q == "high") m_highBtn->setChecked(true);
                else if (q == "extreme") m_extremeBtn->setChecked(true);
                else m_mediumBtn->setChecked(true);
                break;
            }
        }
        f.close();
    }

    // 变更时发出信号
    auto emitQuality = [this]() {
        QString q = "medium";
        if (m_lowBtn->isChecked()) q = "low";
        else if (m_mediumBtn->isChecked()) q = "medium";
        else if (m_highBtn->isChecked()) q = "high";
        else if (m_extremeBtn->isChecked()) q = "extreme";
        emit localQualitySelected(q);
    };
    connect(m_lowBtn, &QRadioButton::toggled, this, [emitQuality](bool checked){ if (checked) emitQuality(); });
    connect(m_mediumBtn, &QRadioButton::toggled, this, [emitQuality](bool checked){ if (checked) emitQuality(); });
    connect(m_highBtn, &QRadioButton::toggled, this, [emitQuality](bool checked){ if (checked) emitQuality(); });
    connect(m_extremeBtn, &QRadioButton::toggled, this, [emitQuality](bool checked){ if (checked) emitQuality(); });
}

void SystemSettingsWindow::notifySwitchSucceeded()
{
    if (m_progress && m_progress->isVisible()) {
        m_progress->close();
    }
}

QFrame* SystemSettingsWindow::setupAvatarControls()
{
    QFrame* avatarCard = new QFrame(this); avatarCard->setObjectName("card");
    QVBoxLayout* box = new QVBoxLayout(avatarCard); box->setContentsMargins(12, 12, 12, 12); box->setSpacing(10);
    QLabel* title = new QLabel("头像设置", avatarCard);
    box->addWidget(title);
    QWidget* gridWidget = new QWidget(avatarCard);
    m_avatarGridLayout = new QGridLayout(gridWidget);
    m_avatarGridLayout->setSpacing(15);
    m_avatarGridLayout->setContentsMargins(0, 0, 0, 0);
    box->addWidget(gridWidget);
    loadAvatarImages();
    return avatarCard;
}

QFrame* SystemSettingsWindow::setupManualApprovalControls()
{
    QFrame* card = new QFrame(this); card->setObjectName("card");
    QVBoxLayout* box = new QVBoxLayout(card); box->setContentsMargins(12, 12, 12, 12); box->setSpacing(10);
    QLabel* title = new QLabel("观看请求处理", card);
    title->setStyleSheet("QLabel { font-size: 13px; }");
    box->addWidget(title);
    QHBoxLayout* row = new QHBoxLayout();
    QLabel* lbl = new QLabel("启用手动同意", card);
    m_manualApprovalCheck = new QCheckBox(card);
    m_manualApprovalCheck->setText(QStringLiteral("启用"));
    row->addWidget(lbl);
    row->addWidget(m_manualApprovalCheck);
    row->addStretch();
    box->addLayout(row);

    // 默认开启手动同意
    m_manualApprovalCheck->setChecked(true);

    QString configPath = QApplication::applicationDirPath() + "/config/app_config.txt";
    QFile f(configPath);
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("manual_approval_enabled=")) {
                QString v = line.mid(QString("manual_approval_enabled=").length()).trimmed();
                bool enabled = v.compare("true", Qt::CaseInsensitive) == 0 || v == "1";
                m_manualApprovalCheck->setChecked(enabled);
                break;
            }
        }
        f.close();
    }

    connect(m_manualApprovalCheck, &QCheckBox::toggled, this, [this](bool checked){
        emit manualApprovalEnabledChanged(checked);
    });

    return card;
}

void SystemSettingsWindow::loadAvatarImages()
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString mapsDir = QString("%1/maps/icon").arg(appDir);
    for (int i = 3; i <= 21; ++i) {
        QString imagePath = QString("%1/%2.png").arg(mapsDir, QString::number(i));
        QLabel *avatarLabel = new QLabel();
        avatarLabel->setFixedSize(80, 80);
        avatarLabel->setStyleSheet(
            "QLabel {"
            "    border: 2px solid transparent;"
            "    border-radius: 8px;"
            "    background-color: #3a3a3a;"
            "}"
            "QLabel:hover {"
            "    border-color: #4caf50;"
            "    background-color: #4a4a4a;"
            "}"
        );
        avatarLabel->setAlignment(Qt::AlignCenter);
        avatarLabel->setScaledContents(true);
        QPixmap pixmap(imagePath);
        if (!pixmap.isNull()) {
            avatarLabel->setPixmap(pixmap);
        } else {
            avatarLabel->setText(QString::number(i));
            avatarLabel->setStyleSheet(avatarLabel->styleSheet() + "QLabel { color: white; font-size: 16px; font-weight: bold; }");
        }
        avatarLabel->setProperty("iconId", i);
        avatarLabel->installEventFilter(this);
        avatarLabel->setAttribute(Qt::WA_Hover, true);
        int row = (i - 3) / 4;
        int col = (i - 3) % 4;
        m_avatarGridLayout->addWidget(avatarLabel, row, col);
        m_avatarLabels.append(avatarLabel);
    }
    QString configPath = QApplication::applicationDirPath() + "/config/app_config.txt";
    QFile f(configPath);
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("icon_id=")) {
                bool ok = false;
                int id = line.mid(QString("icon_id=").length()).trimmed().toInt(&ok);
                if (ok) selectAvatar(id);
                break;
            }
        }
        f.close();
    }
}

void SystemSettingsWindow::selectAvatar(int iconId)
{
    if (m_selectedAvatarLabel) {
        m_selectedAvatarLabel->setStyleSheet(
            "QLabel {"
            "    border: 2px solid transparent;"
            "    border-radius: 8px;"
            "    background-color: #3a3a3a;"
            "}"
            "QLabel:hover {"
            "    border-color: #4caf50;"
            "    background-color: #4a4a4a;"
            "}"
        );
    }
    m_selectedIconId = iconId;
    for (QLabel* label : m_avatarLabels) {
        if (label->property("iconId").toInt() == iconId) {
            m_selectedAvatarLabel = label;
            label->setStyleSheet(
                "QLabel {"
                "    border: 3px solid #4caf50;"
                "    border-radius: 8px;"
                "    background-color: #4a4a4a;"
                "}"
            );
            break;
        }
    }
}

bool SystemSettingsWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QLabel* label = qobject_cast<QLabel*>(obj);
        if (label && m_avatarLabels.contains(label)) {
            int iconId = label->property("iconId").toInt();
            selectAvatar(iconId);
            emit avatarSelected(iconId);
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}
