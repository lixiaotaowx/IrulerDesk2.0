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
#include <QCoreApplication>
#include <QCheckBox>
#include <QPainter>

SystemSettingsWindow::SystemSettingsWindow(QWidget* parent)
    : QDialog(parent), m_list(new QListWidget(this))
{
    setWindowTitle("系统设置");
    setModal(true);
    resize(560, 680);
    setStyleSheet(
        "QDialog { background-color: #2b2b2b; }"
        "QLabel { color: #e9e9e9; background: transparent; }"
        "QFrame#card { background-color: rgba(255, 255, 255, 10); border: 1px solid rgba(255, 255, 255, 14); border-radius: 20px; }"
        "QFrame#headerCard { background-color: rgba(255, 255, 255, 8); border: 1px solid rgba(255, 255, 255, 12); border-radius: 20px; }"
        "QPushButton { color: #ffffff; background-color: rgba(255, 255, 255, 14); border: 1px solid rgba(255, 255, 255, 16); padding: 8px 14px; border-radius: 12px; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 18); }"
        "QPushButton:pressed { background-color: rgba(255, 255, 255, 22); }"
        "QRadioButton { color: #e0e0e0; spacing: 8px; background: transparent; }"
        "QRadioButton::indicator { width: 16px; height: 16px; }"
        "QRadioButton::indicator:unchecked { border: 2px solid #777; border-radius: 8px; background: transparent; }"
        "QRadioButton::indicator:checked { border: 2px solid #ff6600; border-radius: 8px; background: #ff6600; }"
        "QCheckBox { color: #e0e0e0; background: transparent; }"
        "QCheckBox::indicator { width: 18px; height: 18px; }"
        "QCheckBox::indicator:unchecked { border: 2px solid #777; border-radius: 6px; background: transparent; }"
        "QCheckBox::indicator:checked { border: 2px solid #ff6600; border-radius: 6px; background: #ff6600; }"
        "QLineEdit { background-color: rgba(0, 0, 0, 18); color: #f0f0f0; border: 1px solid rgba(255, 255, 255, 14); border-radius: 12px; padding: 8px 10px; }"
        "QLineEdit:focus { border: 1px solid #ff6600; }"
        "QListWidget { background-color: rgba(0, 0, 0, 14); color: #e0e0e0; border: 1px solid rgba(255, 255, 255, 10); border-radius: 16px; padding: 6px; }"
        "QListWidget::item { border-radius: 16px; padding: 8px; }"
        "QListWidget::item:hover { background-color: rgba(255, 255, 255, 25); }"
        "QListWidget::item:selected { background-color: rgba(255, 102, 0, 70); color: #ffffff; }"
    );

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet(
        "QScrollArea { border: none; background: transparent; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"
        "QScrollBar:vertical { background-color: transparent; width: 14px; margin: 8px 6px 8px 0; border: none; }"
        "QScrollBar::handle:vertical { background-color: rgba(255, 255, 255, 55); min-height: 32px; border-radius: 7px; }"
        "QScrollBar::handle:vertical:hover { background-color: rgba(255, 255, 255, 75); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal { height: 14px; background: transparent; margin: 0 8px 6px 8px; }"
        "QScrollBar::handle:horizontal { background-color: rgba(255, 255, 255, 55); min-width: 32px; border-radius: 7px; }"
        "QScrollBar::handle:horizontal:hover { background-color: rgba(255, 255, 255, 75); }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
    );
    QWidget* content = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(16);

    QFrame* headerCard = new QFrame(this);
    headerCard->setObjectName("headerCard");
    QVBoxLayout* headerBox = new QVBoxLayout(headerCard);
    headerBox->setContentsMargins(16, 14, 16, 14);
    headerBox->setSpacing(6);

    QLabel* header = new QLabel("系统设置", headerCard);
    QFont hf = header->font(); hf.setPointSize(16); hf.setBold(true); header->setFont(hf);
    headerBox->addWidget(header);

    QLabel* sub = new QLabel("推流源、画质、观看请求处理等选项", headerCard);
    sub->setStyleSheet("QLabel { color: rgba(255, 255, 255, 150); font-size: 12px; }");
    headerBox->addWidget(sub);

    layout->addWidget(headerCard);

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
    tip->setStyleSheet("QLabel { color: rgba(255, 255, 255, 170); }");
    QFrame* screenCard = new QFrame(this); screenCard->setObjectName("card");
    QVBoxLayout* screenBox = new QVBoxLayout(screenCard); screenBox->setContentsMargins(12, 12, 12, 12); screenBox->setSpacing(10);
    screenBox->addWidget(tip);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setFlow(QListView::LeftToRight);
    m_list->setWrapping(false);
    m_list->setWordWrap(true);
    m_list->setIconSize(QSize(200, 112));
    m_list->setGridSize(QSize(220, 160));
    m_list->setSpacing(10);
    m_list->setTextElideMode(Qt::ElideNone);
    m_list->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setFixedHeight(170);
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
        QPixmap pix = s ? s->grabWindow(0) : QPixmap();
        if (pix.isNull()) {
            pix = QPixmap(200, 112);
            pix.fill(QColor(20, 20, 20));
            QPainter p(&pix);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QColor(230, 230, 230, 180));
            p.drawText(pix.rect().adjusted(10, 10, -10, -10), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, s ? s->name() : QStringLiteral("Screen"));
        }

        QPixmap scaled = pix.scaled(200, 112, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QString text = QString("%1\n%2x%3")
                           .arg(s ? s->name() : QStringLiteral("Screen"))
                           .arg(s ? s->size().width() : 0)
                           .arg(s ? s->size().height() : 0);
        auto* item = new QListWidgetItem(QIcon(scaled), text);
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
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

QFrame* SystemSettingsWindow::setupManualApprovalControls()
{
    QFrame* card = new QFrame(this); card->setObjectName("card");
    QVBoxLayout* box = new QVBoxLayout(card); box->setContentsMargins(12, 12, 12, 12); box->setSpacing(10);
    QLabel* title = new QLabel("观看请求处理", card);
    title->setStyleSheet("QLabel { font-size: 13px; font-weight: 600; }");
    box->addWidget(title);
    QHBoxLayout* row = new QHBoxLayout();
    QLabel* lbl = new QLabel("启用手动同意", card);
    m_manualApprovalCheck = new QCheckBox(card);
    m_manualApprovalCheck->setText(QString());
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
