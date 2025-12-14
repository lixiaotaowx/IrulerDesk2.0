#include "StreamingIslandWidget.h"
#include <QScreen>
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QClipboard>
#include <QTimer>
#include <QDebug>

// Custom widget for capsule appearance
class CapsuleWidget : public QWidget {
public:
    explicit CapsuleWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
    }
protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        
        // Draw Capsule
        QLinearGradient gradient(0, 0, 0, height());
        gradient.setColorAt(0, QColor("#3a3a3a"));
        gradient.setColorAt(1, QColor("#2a2a2a"));
        p.setBrush(gradient);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(rect(), height() / 2, height() / 2);
    }
};

StreamingIslandWidget::StreamingIslandWidget(QWidget *parent)
    : QWidget(parent)
    , m_dragging(false)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // 确保背景透明，避免出现黑色矩形背景
    setStyleSheet("background: transparent;");

    // Create annotation widget as a separate top-level window
    m_annotationWidget = new ScreenAnnotationWidget(nullptr); 

    setupUI();
}

StreamingIslandWidget::~StreamingIslandWidget()
{
    if (m_annotationWidget) {
        m_annotationWidget->deleteLater();
    }
}

void StreamingIslandWidget::setupUI()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 0, 10, 10); // Top margin 0 to be flush with edge

    m_contentWidget = new CapsuleWidget(this);

    // Add shadow
    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(20);
    shadow->setColor(QColor(0, 0, 0, 100));
    shadow->setOffset(0, 4);
    m_contentWidget->setGraphicsEffect(shadow);

    m_layout = new QHBoxLayout(m_contentWidget);
    m_layout->setContentsMargins(12, 0, 4, 0); // Match VideoWindow titlebar margins (right is 4)
    m_layout->setSpacing(2); // Match VideoWindow spacing

    // Toolbar
    m_toolbar = new AnnotationToolbar(m_contentWidget);
    m_layout->addWidget(m_toolbar);
    
    // Add spacing to align with VideoWindow's stretch/gap
    // VideoWindow has a stretch here. Since we can't stretch to unknown window width,
    // we use a fixed spacing that visually matches the user's setup (~32px).
    m_layout->addSpacing(32);

    // Placeholder for Speaker Button (Whitespace)
    QWidget *speakerPlaceholder = new QWidget(m_contentWidget);
    speakerPlaceholder->setFixedSize(24, 24);
    speakerPlaceholder->setStyleSheet("background: transparent;");
    m_layout->addWidget(speakerPlaceholder);

    // Close Button (at Mic Position)
    QPushButton *closeButton = new QPushButton("×", m_contentWidget);
    closeButton->setStyleSheet(
        "QPushButton {"
        "    background-color: transparent;"
        "    color: rgba(255, 255, 255, 0.7);"
        "    border: none;"
        "    width: 24px;"
        "    height: 24px;"
        "    border-radius: 12px;"
        "    font-size: 18px;"
        "    font-weight: bold;"
        "    margin: 1px;" // Match VideoWindow button margin
        "}"
        "QPushButton:hover {"
        "    background-color: rgba(220, 53, 69, 0.8);"
        "    color: white;"
        "}"
    );
    closeButton->setToolTip("关闭");
    connect(closeButton, &QPushButton::clicked, this, &StreamingIslandWidget::hide); 
    m_layout->addWidget(closeButton);

    mainLayout->addWidget(m_contentWidget);

    // Connect Toolbar to Annotation Widget
    // Wrap setTool to ensure Island stays on top after AnnotationWidget is raised
    connect(m_toolbar, &AnnotationToolbar::toolSelected, this, [this](int toolMode) {
        if (m_annotationWidget) {
            m_annotationWidget->setTool(toolMode);
            updateAnnotationMask();
            // Ensure Island stays on top of the fullscreen annotation widget
            this->raise();
        }
    });
    connect(m_toolbar, &AnnotationToolbar::colorChanged, m_annotationWidget, &ScreenAnnotationWidget::setColorId);
    connect(m_toolbar, &AnnotationToolbar::undoRequested, m_annotationWidget, &ScreenAnnotationWidget::undo);
    
    // Connect cancelled signal
    connect(m_annotationWidget, &ScreenAnnotationWidget::toolCancelled, m_toolbar, &AnnotationToolbar::resetTools);
    
    // Connect Toolbar to Local Actions
    connect(m_toolbar, &AnnotationToolbar::cameraRequested, this, [this]() {
        if (m_targetScreen) {
            QPixmap pix = m_targetScreen->grabWindow(0);
            QClipboard *cb = QGuiApplication::clipboard();
            if (cb) cb->setPixmap(pix);
            showClipboardToast();
        } else {
             // Fallback to primary
             QScreen *s = QGuiApplication::primaryScreen();
             if (s) {
                 QPixmap pix = s->grabWindow(0);
                 QClipboard *cb = QGuiApplication::clipboard();
                 if (cb) cb->setPixmap(pix);
                 showClipboardToast();
             }
        }
    });
    
    connect(m_toolbar, &AnnotationToolbar::clearRequested, m_annotationWidget, &ScreenAnnotationWidget::clear);
}

void StreamingIslandWidget::showOnScreen()
{
    // Determine target screen
    QScreen *screen = m_targetScreen;
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    if (screen) {
        // Position Island at TOP LEFT with margin
        QRect screenGeom = screen->availableGeometry();
        int x = screenGeom.x() + 20; // 20px from left edge
        int y = screenGeom.y(); // 0px from top (flush)
        
        move(x, y);

        // Position Annotation Widget (Fullscreen on target screen)
        m_annotationWidget->setGeometry(screen->geometry());
        m_annotationWidget->show(); 
    }
    
    show();
    raise();
}

void StreamingIslandWidget::setTargetScreen(QScreen *screen)
{
    m_targetScreen = screen;
    if (isVisible()) {
        showOnScreen();
    }
}

void StreamingIslandWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void StreamingIslandWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton && m_dragging) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void StreamingIslandWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    }
}

void StreamingIslandWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateAnnotationMask();
}

void StreamingIslandWidget::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    updateAnnotationMask();
}

void StreamingIslandWidget::updateAnnotationMask()
{
    if (!m_annotationWidget || !m_annotationWidget->isEnabled()) return;

    // Create a mask that is the whole screen MINUS the island's geometry
    // Since m_annotationWidget is fullscreen at (0,0) (usually), we map island geometry to it.
    
    // We assume m_annotationWidget covers the screen where it is shown.
    // We need island's geometry relative to m_annotationWidget.
    // Both are top-level. 
    // m_annotationWidget pos is global. Island pos is global.
    
    QRect islandRect = this->geometry();
    QPoint annotationPos = m_annotationWidget->pos();
    QRect islandRelative = islandRect.translated(-annotationPos);
    
    QRegion maskRegion(m_annotationWidget->rect());
    maskRegion -= QRegion(islandRelative);
    
    m_annotationWidget->setMask(maskRegion);
}

void StreamingIslandWidget::hideEvent(QHideEvent *event)
{
    if (m_annotationWidget) {
        m_annotationWidget->hide();
    }
    QWidget::hideEvent(event);
}

void StreamingIslandWidget::showClipboardToast()
{
    // Simple toast or feedback
    // Reuse existing BubbleTipWidget if available, or just ignore for now as user didn't complain about this
}
