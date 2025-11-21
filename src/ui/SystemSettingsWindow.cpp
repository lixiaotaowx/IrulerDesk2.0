#include "SystemSettingsWindow.h"
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QFile>
#include <QTextStream>

SystemSettingsWindow::SystemSettingsWindow(QWidget* parent)
    : QDialog(parent), m_list(new QListWidget(this))
{
    setWindowTitle("系统设置 - 屏幕切换");
    setModal(true);
    resize(380, 420);

    QVBoxLayout* layout = new QVBoxLayout(this);
    setupUserNameControls();
    if (m_userNameLabel && m_userNameEdit) {
        QHBoxLayout* userRow = new QHBoxLayout();
        userRow->addWidget(m_userNameLabel);
        userRow->addWidget(m_userNameEdit, 1);
        if (m_userNameConfirmBtn) userRow->addWidget(m_userNameConfirmBtn);
        layout->addLayout(userRow);
    } else {
        if (m_userNameLabel) layout->addWidget(m_userNameLabel);
        if (m_userNameEdit) layout->addWidget(m_userNameEdit);
    }
    QLabel* tip = new QLabel("选择一个屏幕作为推流源", this);
    layout->addWidget(tip);
    layout->addWidget(m_list);

    // 在屏幕列表下面添加质量选择控件
    setupQualityControls();
    layout->addWidget(m_qualityLabel);
    QHBoxLayout* qualityRow = new QHBoxLayout();
    qualityRow->addWidget(m_lowBtn);
    qualityRow->addWidget(m_mediumBtn);
    qualityRow->addWidget(m_highBtn);
    qualityRow->addStretch();
    layout->addLayout(qualityRow);

    populateScreens();

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item){
        int idx = item->data(Qt::UserRole).toInt();

        // 显示“切换屏幕中”等待弹窗（不可取消，直到成功）
        if (!m_progress) {
            m_progress = new QProgressDialog(QString(), QString(), 0, 0, this);
            m_progress->setWindowTitle(QStringLiteral("请稍候"));
            m_progress->setWindowModality(Qt::WindowModal);
            m_progress->setCancelButton(nullptr);
            m_progress->setMinimumDuration(0);
            m_progress->setAutoClose(false);
            m_progress->setAutoReset(false);
            m_progress->setMinimumSize(320, 120);
            m_progress->setStyleSheet(
                "QProgressDialog {"
                "    background-color: #2a2a2a;"
                "    color: white;"
                "    border: 1px solid #555;"
                "    border-radius: 6px;"
                "}"
                "QLabel {"
                "    color: white;"
                "    font-size: 14px;"
                "}"
                "QProgressBar {"
                "    background-color: #3a3a3a;"
                "    border: 1px solid #555;"
                "    border-radius: 4px;"
                "    height: 12px;"
                "    text-align: center;"
                "    color: white;"
                "}"
                "QProgressBar::chunk {"
                "    background-color: #4caf50;"
                "}"
                "QPushButton {"
                "    color: white;"
                "    background-color: #4a4a4a;"
                "    border: 1px solid #666;"
                "    padding: 4px 10px;"
                "    border-radius: 4px;"
                "}"
                "QPushButton:hover {"
                "    background-color: #5a5a5a;"
                "}"
            );
        }
        m_progress->setLabelText(QStringLiteral("切换屏幕中..."));
        m_progress->show();

        // 触发屏幕切换，等待首帧到达后由外部调用notifySwitchSucceeded
        emit screenSelected(idx);
    });
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
}

void SystemSettingsWindow::setupQualityControls()
{
    m_qualityLabel = new QLabel("视频质量：", this);
    m_qualityLabel->setStyleSheet("QLabel { font-size: 13px; }");
    m_qualityGroup = new QButtonGroup(this);
    m_lowBtn = new QRadioButton("低", this);
    m_mediumBtn = new QRadioButton("中", this);
    m_highBtn = new QRadioButton("高", this);
    m_qualityGroup->addButton(m_lowBtn);
    m_qualityGroup->addButton(m_mediumBtn);
    m_qualityGroup->addButton(m_highBtn);
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
                else if (q == "high") m_highBtn->setChecked(true);
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
        else if (m_highBtn->isChecked()) q = "high";
        emit localQualitySelected(q);
    };
    connect(m_lowBtn, &QRadioButton::toggled, this, [emitQuality](bool checked){ if (checked) emitQuality(); });
    connect(m_mediumBtn, &QRadioButton::toggled, this, [emitQuality](bool checked){ if (checked) emitQuality(); });
    connect(m_highBtn, &QRadioButton::toggled, this, [emitQuality](bool checked){ if (checked) emitQuality(); });
}

void SystemSettingsWindow::notifySwitchSucceeded()
{
    if (m_progress && m_progress->isVisible()) {
        m_progress->close();
    }
    accept();
}