#include "AnnotationToolbar.h"
#include <QApplication>
#include <QDebug>

AnnotationToolbar::AnnotationToolbar(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
}

AnnotationToolbar::~AnnotationToolbar()
{
}

void AnnotationToolbar::setupUI()
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(2);

    QString appDir = QCoreApplication::applicationDirPath();
    QString iconDir = appDir + "/maps/logo";

    // Styles
    m_micButtonStyle = 
        "QPushButton { background-color: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 20); }";
    
    // Color Button
    m_colorButton = new ColorCircleButton(this);
    m_colorButton->setFixedSize(13, 13);
    m_colorButton->setToolTip(QStringLiteral("颜色"));
    connect(m_colorButton, &ColorCircleButton::clicked, this, [this]() {
        int nextId = (m_colorButton->colorId() + 1) % 4;
        m_colorButton->setColorId(nextId);
        emit colorChanged(nextId);
    });

    // Tools
    auto createToolBtn = [&](const QString &icon, const QString &tooltip) -> RainbowToolButton* {
        RainbowToolButton *btn = new RainbowToolButton(this);
        btn->setIcon(QIcon(iconDir + "/" + icon));
        btn->setIconSize(QSize(16, 16));
        btn->setToolTip(tooltip);
        btn->setCheckable(true);
        btn->setFixedSize(24, 24);
        return btn;
    };

    m_penButton = createToolBtn("pen.png", QStringLiteral("画笔"));
    m_rectButton = createToolBtn("sele_box.png", QStringLiteral("矩形"));
    m_circleButton = createToolBtn("sele.png", QStringLiteral("圆形"));
    m_arrowButton = createToolBtn("arrow.png", QStringLiteral("箭头"));
    m_textButton = createToolBtn("text.png", QStringLiteral("文字"));
    m_eraserButton = createToolBtn("xiangpi.png", QStringLiteral("橡皮擦"));

    // Connect tool signals
    connect(m_penButton, &QPushButton::toggled, this, [this](bool c){ if(c) emit toolSelected(1); onToolToggled(c); });
    connect(m_rectButton, &QPushButton::toggled, this, [this](bool c){ if(c) emit toolSelected(2); onToolToggled(c); });
    connect(m_circleButton, &QPushButton::toggled, this, [this](bool c){ if(c) emit toolSelected(3); onToolToggled(c); });
    connect(m_arrowButton, &QPushButton::toggled, this, [this](bool c){ if(c) emit toolSelected(4); onToolToggled(c); }); 
    connect(m_textButton, &QPushButton::toggled, this, [this](bool c){ if(c) emit toolSelected(5); onToolToggled(c); }); 
    connect(m_eraserButton, &QPushButton::toggled, this, [this](bool c){ if(c) emit toolSelected(6); onToolToggled(c); });

    // Other buttons
    auto createNormalBtn = [&](const QString &icon, const QString &tooltip) -> QPushButton* {
        QPushButton *btn = new QPushButton(this);
        btn->setIcon(QIcon(iconDir + "/" + icon));
        btn->setIconSize(QSize(16, 16));
        btn->setStyleSheet(m_micButtonStyle);
        btn->setToolTip(tooltip);
        btn->setFixedSize(24, 24);
        return btn;
    };

    m_undoButton = createNormalBtn("z.png", QStringLiteral("撤销"));
    connect(m_undoButton, &QPushButton::clicked, this, &AnnotationToolbar::undoRequested);

    m_cameraButton = createNormalBtn("camera.png", QStringLiteral("截图到剪贴板"));
    connect(m_cameraButton, &QPushButton::clicked, this, &AnnotationToolbar::cameraRequested);

    // Clear Button (Replaces Like)
    m_clearButton = new QPushButton("清", this);
    m_clearButton->setFixedSize(24, 24);
    m_clearButton->setToolTip(QStringLiteral("清屏"));
    m_clearButton->setStyleSheet(
        "QPushButton { "
        "   background-color: transparent; "
        "   color: white; "
        "   border: none; "
        "   border-radius: 4px; "
        "   font-weight: bold;"
        "   font-family: 'Microsoft YaHei';"
        "} "
        "QPushButton:hover { background-color: rgba(255, 255, 255, 20); }"
    );
    connect(m_clearButton, &QPushButton::clicked, this, &AnnotationToolbar::clearRequested);

    // Layout
    m_layout->addWidget(m_colorButton);
    m_layout->addSpacing(2);
    m_layout->addWidget(m_penButton);
    m_layout->addSpacing(2);
    m_layout->addWidget(m_rectButton);
    m_layout->addSpacing(2);
    m_layout->addWidget(m_circleButton);
    m_layout->addSpacing(2);
    m_layout->addWidget(m_arrowButton);
    m_layout->addSpacing(2);
    m_layout->addWidget(m_textButton);
    m_layout->addSpacing(2);
    m_layout->addWidget(m_eraserButton);
    m_layout->addSpacing(2);
    m_layout->addWidget(m_undoButton);
    m_layout->addSpacing(2);
    m_layout->addWidget(m_cameraButton);
    m_layout->addSpacing(2);
    m_layout->addWidget(m_clearButton);
}

void AnnotationToolbar::onToolToggled(bool checked)
{
    if (!checked) {
        // If unchecked, check if any other is checked. If not, emit mode 0
        if (!m_penButton->isChecked() && !m_rectButton->isChecked() && !m_circleButton->isChecked() &&
            !m_arrowButton->isChecked() && !m_textButton->isChecked() && !m_eraserButton->isChecked()) {
            emit toolSelected(0);
        }
        return;
    }

    RainbowToolButton *senderBtn = qobject_cast<RainbowToolButton*>(sender());
    if (!senderBtn) return;

    // Exclusive logic
    QList<RainbowToolButton*> tools = {m_penButton, m_rectButton, m_circleButton, m_arrowButton, m_textButton, m_eraserButton};
    for (auto btn : tools) {
        if (btn != senderBtn) {
            bool wasBlocked = btn->signalsBlocked();
            btn->blockSignals(true);
            btn->setChecked(false);
            btn->blockSignals(wasBlocked);
        }
    }
}

void AnnotationToolbar::resetTools()
{
    QList<RainbowToolButton*> tools = {m_penButton, m_rectButton, m_circleButton, m_arrowButton, m_textButton, m_eraserButton};
    for (auto btn : tools) {
        bool wasBlocked = btn->signalsBlocked();
        btn->blockSignals(true);
        btn->setChecked(false);
        btn->blockSignals(wasBlocked);
    }
    emit toolSelected(0);
}

int AnnotationToolbar::currentColorId() const
{
    return m_colorButton->colorId();
}

void AnnotationToolbar::updateToolStyles()
{
    // RainbowToolButton handles its own styling, so this might be empty or used for other updates
}
