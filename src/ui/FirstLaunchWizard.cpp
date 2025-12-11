#include "FirstLaunchWizard.h"
#include <QCoreApplication>
#include <QStyle>
#include <QMessageBox>

FirstLaunchWizard::FirstLaunchWizard(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("首次启动设置"));
    setModal(true);
    resize(680, 520);
    setupStyle();
    m_stack = new QStackedWidget(this);
    buildPages();
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);
    QLabel* header = new QLabel(QStringLiteral("欢迎，请完成基础设置"), this);
    QFont hf = header->font(); hf.setPointSize(16); hf.setBold(true); header->setFont(hf);
    root->addWidget(header);
    root->addWidget(m_stack, 1);
    QHBoxLayout* nav = new QHBoxLayout();
    m_prevBtn = new QPushButton(QStringLiteral("上一页"), this);
    m_nextBtn = new QPushButton(QStringLiteral("下一页"), this);
    m_finishBtn = new QPushButton(QStringLiteral("完成"), this);
    m_skipBtn = new QPushButton(QStringLiteral("跳过"), this);
    nav->addWidget(m_skipBtn);
    nav->addStretch();
    nav->addWidget(m_prevBtn);
    nav->addWidget(m_nextBtn);
    nav->addWidget(m_finishBtn);
    root->addLayout(nav);
    connect(m_prevBtn, &QPushButton::clicked, this, [this]() {
        int i = m_stack->currentIndex();
        if (i > 0) m_stack->setCurrentIndex(i - 1);
        updateNav();
    });
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        int i = m_stack->currentIndex();
        if (i + 1 < m_stack->count()) m_stack->setCurrentIndex(i + 1);
        updateNav();
    });
    connect(m_finishBtn, &QPushButton::clicked, this, [this]() {
        QString n = m_nameEdit ? m_nameEdit->text().trimmed() : QString();
        if (n.isEmpty()) return;
        if (m_selectedIconId <= 0) m_selectedIconId = 3;
        if (m_selectedScreenIndex < 0) m_selectedScreenIndex = 0;
        accept();
    });
    connect(m_skipBtn, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("可后期系统设置"));
        accept();
    });
    updateNav();
}

QString FirstLaunchWizard::userName() const { return m_nameEdit ? m_nameEdit->text().trimmed() : QString(); }
int FirstLaunchWizard::iconId() const { return m_selectedIconId; }
int FirstLaunchWizard::screenIndex() const { return m_selectedScreenIndex; }

void FirstLaunchWizard::setupStyle()
{
    setStyleSheet(
        "QDialog { background-color: #1a1a1a; }"
        "QLabel { color: #eaeaea; }"
        "QLineEdit { background-color: #121212; color: #f0f0f0; border: 1px solid #333; border-radius: 6px; padding: 8px; }"
        "QPushButton { color: #ffffff; background-color: #2b2b2b; border: 1px solid #444; padding: 8px 16px; border-radius: 6px; }"
        "QPushButton:hover { background-color: #3a3a3a; }"
        "QFrame#card { background-color: #161616; border: 1px solid #2e2e2e; border-radius: 10px; }"
        "QScrollArea { border: none; }"
        "QAbstractButton#screen { border: 2px solid #333; border-radius: 8px; background-color: #222; }"
        "QAbstractButton#screen:hover { border-color: #777; }"
        "QAbstractButton#screen:checked { border-color: #4caf50; background-color: #2a3b2f; }"
        "QAbstractButton#avatar { border: 2px solid transparent; border-radius: 8px; background-color: #3a3a3a; }"
        "QAbstractButton#avatar:hover { border-color: #4caf50; background-color: #4a4a4a; }"
        "QAbstractButton#avatar:checked { border: 3px solid #4caf50; background-color: #4a4a4a; }"
    );
}

void FirstLaunchWizard::buildPages()
{
    buildUserPage();
    buildAvatarPage();
    buildScreenPage();
}

void FirstLaunchWizard::buildUserPage()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* box = new QVBoxLayout(page);
    box->setContentsMargins(12, 12, 12, 12);
    box->setSpacing(12);
    QLabel* t = new QLabel(QStringLiteral("设置用户名或代称"), page);
    QFont tf = t->font(); tf.setPointSize(14); tf.setBold(true); t->setFont(tf);
    box->addWidget(t);
    m_nameEdit = new QLineEdit(page);
    box->addWidget(m_nameEdit);
    box->addStretch();
    m_userPage = page;
    m_stack->addWidget(page);
}

void FirstLaunchWizard::buildAvatarPage()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* box = new QVBoxLayout(page);
    box->setContentsMargins(12, 12, 12, 12);
    box->setSpacing(12);
    QLabel* t = new QLabel(QStringLiteral("选择头像"), page);
    QFont tf = t->font(); tf.setPointSize(14); tf.setBold(true); t->setFont(tf);
    box->addWidget(t);
    QWidget* gridWidget = new QWidget(page);
    m_avatarGrid = new QGridLayout(gridWidget);
    m_avatarGrid->setSpacing(15);
    m_avatarGrid->setContentsMargins(0, 0, 0, 0);
    box->addWidget(gridWidget);
    m_avatarGroup = new QButtonGroup(this);
    m_avatarGroup->setExclusive(true);
    QString appDir = QCoreApplication::applicationDirPath();
    QString mapsDir = QString("%1/maps/icon").arg(appDir);
    int r = 0, c = 0;
    for (int i = 3; i <= 21; ++i) {
        QString imagePath = QString("%1/%2.png").arg(mapsDir, QString::number(i));
        QPixmap pix(imagePath);
        QPushButton* b = new QPushButton(gridWidget);
        b->setObjectName("avatar");
        b->setCheckable(true);
        b->setFixedSize(80, 80);
        if (!pix.isNull()) {
            b->setIcon(QIcon(pix));
            b->setIconSize(QSize(72, 72));
        } else {
            b->setText(QString::number(i));
        }
        b->setProperty("iconId", i);
        m_avatarGrid->addWidget(b, r, c);
        m_avatarGroup->addButton(b, i);
        m_avatarButtons.append(b);
        connect(b, &QPushButton::clicked, this, [this, i]() { selectAvatar(i); });
        ++c; if (c >= 4) { c = 0; ++r; }
    }
    if (!m_avatarButtons.isEmpty()) selectAvatar(3);
    m_avatarPage = page;
    m_stack->addWidget(page);
}

void FirstLaunchWizard::buildScreenPage()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* box = new QVBoxLayout(page);
    box->setContentsMargins(12, 12, 12, 12);
    box->setSpacing(12);
    QLabel* t = new QLabel(QStringLiteral("选择常用屏幕"), page);
    QFont tf = t->font(); tf.setPointSize(14); tf.setBold(true); t->setFont(tf);
    box->addWidget(t);
    QScrollArea* sa = new QScrollArea(page);
    sa->setWidgetResizable(true);
    QWidget* rowWidget = new QWidget(sa);
    m_screenRow = new QHBoxLayout(rowWidget);
    m_screenRow->setSpacing(16);
    m_screenRow->setContentsMargins(8, 8, 8, 8);
    QList<QScreen*> screens = QGuiApplication::screens();
    m_screenGroup = new QButtonGroup(this);
    m_screenGroup->setExclusive(true);
    for (int i = 0; i < screens.size(); ++i) {
        QScreen* s = screens[i];
        QPixmap pix = s->grabWindow(0);
        QPixmap scaled = pix.scaled(240, 135, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPushButton* b = new QPushButton(rowWidget);
        b->setObjectName("screen");
        b->setCheckable(true);
        b->setFixedSize(260, 170);
        b->setIcon(QIcon(scaled));
        b->setIconSize(QSize(240, 135));
        b->setText(QString("%1\n%2x%3").arg(s->name()).arg(s->size().width()).arg(s->size().height()));
        b->setStyleSheet("QPushButton#screen { text-align: left; padding: 6px; } QPushButton#screen::menu-indicator { image: none; }");
        b->setProperty("screenIndex", i);
        m_screenRow->addWidget(b);
        m_screenGroup->addButton(b, i);
        m_screenButtons.append(b);
        connect(b, &QPushButton::clicked, this, [this, i]() { selectScreen(i); });
    }
    if (!m_screenButtons.isEmpty()) selectScreen(0);
    rowWidget->setLayout(m_screenRow);
    sa->setWidget(rowWidget);
    box->addWidget(sa, 1);
    m_screenPage = page;
    m_stack->addWidget(page);
}

void FirstLaunchWizard::selectAvatar(int id)
{
    m_selectedIconId = id;
    for (auto* b : m_avatarButtons) {
        bool on = b->property("iconId").toInt() == id;
        b->setChecked(on);
    }
}

void FirstLaunchWizard::selectScreen(int idx)
{
    m_selectedScreenIndex = idx;
    for (auto* b : m_screenButtons) {
        bool on = b->property("screenIndex").toInt() == idx;
        b->setChecked(on);
    }
}

void FirstLaunchWizard::updateNav()
{
    int i = m_stack->currentIndex();
    bool first = (i == 0);
    bool last = (i + 1 == m_stack->count());
    m_prevBtn->setEnabled(!first);
    m_nextBtn->setVisible(!last);
    m_finishBtn->setVisible(last);
    if (m_skipBtn) m_skipBtn->setVisible(first);
}
