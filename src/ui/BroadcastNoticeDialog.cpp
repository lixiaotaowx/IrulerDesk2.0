#include "BroadcastNoticeDialog.h"
#include <QHBoxLayout>
#include <QApplication>

BroadcastNoticeDialog::BroadcastNoticeDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
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
        "background-color: white; "
        "border-radius: 12px; "
        "border: 1px solid #e0e0e0; "
        "}"
    );

    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(20);
    shadow->setColor(QColor(0, 0, 0, 50));
    shadow->setOffset(0, 4);
    container->setGraphicsEffect(shadow);

    QVBoxLayout *contentLayout = new QVBoxLayout(container);
    contentLayout->setSpacing(15);
    contentLayout->setContentsMargins(20, 20, 20, 20);

    QLabel *titleLabel = new QLabel(QStringLiteral("发布公告"), container);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
    titleLabel->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(titleLabel);

    m_editor = new QTextEdit(container);
    m_editor->setPlaceholderText(QStringLiteral("请输入公告内容..."));
    m_editor->setMinimumHeight(120);
    m_editor->setStyleSheet(
        "QTextEdit { "
        "border: 1px solid #ccc; "
        "border-radius: 6px; "
        "padding: 8px; "
        "font-size: 14px; "
        "background-color: #f9f9f9; "
        "color: #333333; "
        "}"
        "QTextEdit:focus { "
        "border: 1px solid #2196F3; "
        "background-color: white; "
        "color: #000000; "
        "}"
    );
    contentLayout->addWidget(m_editor);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(15);

    m_cancelBtn = new QPushButton(QStringLiteral("取消"), container);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setFixedHeight(36);
    m_cancelBtn->setStyleSheet(
        "QPushButton { "
        "background-color: #f0f0f0; "
        "color: #333; "
        "border: none; "
        "border-radius: 18px; "
        "font-weight: bold; "
        "}"
        "QPushButton:hover { "
        "background-color: #e0e0e0; "
        "}"
    );
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_cancelBtn);

    m_publishBtn = new QPushButton(QStringLiteral("发布"), container);
    m_publishBtn->setCursor(Qt::PointingHandCursor);
    m_publishBtn->setFixedHeight(36);
    m_publishBtn->setStyleSheet(
        "QPushButton { "
        "background-color: #2196F3; "
        "color: white; "
        "border: none; "
        "border-radius: 18px; "
        "font-weight: bold; "
        "}"
        "QPushButton:hover { "
        "background-color: #1976D2; "
        "}"
    );
    connect(m_publishBtn, &QPushButton::clicked, [this]() {
        if (!m_editor->toPlainText().trimmed().isEmpty()) {
            emit publishRequested(m_editor->toPlainText());
            accept();
        }
    });
    btnLayout->addWidget(m_publishBtn);

    contentLayout->addLayout(btnLayout);
    mainLayout->addWidget(container);
    
    setFixedSize(400, 300);
}
