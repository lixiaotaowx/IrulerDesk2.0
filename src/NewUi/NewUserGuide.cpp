#include "NewUserGuide.h"
#include "NewUiWindow.h"
#include "../ui/AnnotationToolbar.h"
#include <QPainter>
#include <QPainterPath>
#include <QEvent>
#include <QApplication>
#include <QDebug>
#include <QFontMetrics>

NewUserGuide::NewUserGuide(NewUiWindow *parent)
    : QWidget(parent), m_parentWindow(parent), m_currentIndex(0)
{
    if (parent) {
        resize(parent->size());
        parent->installEventFilter(this);
    }
    
    // Setup dismiss button
    m_dismissBtn = new QPushButton("下一步", this);
    m_dismissBtn->setCursor(Qt::PointingHandCursor);
    m_dismissBtn->setStyleSheet(
        "QPushButton {"
        "   background-color: #0078d4;"
        "   color: white;"
        "   border: none;"
        "   border-radius: 20px;"
        "   padding: 10px 40px;"
        "   font-size: 16px;"
        "   font-weight: bold;"
        "}"
        "QPushButton:hover { background-color: #1084d9; }"
        "QPushButton:pressed { background-color: #006abc; }"
    );
    connect(m_dismissBtn, &QPushButton::clicked, this, &NewUserGuide::nextStep);

    // Setup fake remote control button
    m_fakeRemoteBtn = new QPushButton("控", this);
    m_fakeRemoteBtn->setFixedSize(24, 24);
    m_fakeRemoteBtn->setStyleSheet(
        "QPushButton { "
        "   background-color: transparent; "
        "   color: white; "
        "   border: none; "
        "   border-radius: 4px; "
        "   font-weight: bold;"
        "   font-family: 'Microsoft YaHei';"
        "} "
        "QPushButton:hover { background-color: rgba(255, 255, 255, 20); }"
        "QPushButton:checked { background-color: rgba(64, 158, 255, 180); border: 1px solid #409EFF; }"
    );
    m_fakeRemoteBtn->setVisible(false); // Hidden by default

    setAttribute(Qt::WA_TranslucentBackground);
    // Ensure it blocks input to underlying widgets
    // By default a widget consumes mouse events
    raise();
}

void NewUserGuide::nextStep()
{
    if (m_currentIndex < m_items.size() - 1) {
        m_currentIndex++;
        layoutItems(); // Re-layout for current item if needed
        update();
        if (m_currentIndex == m_items.size() - 1) {
             m_dismissBtn->setText("完成");
        }
    } else {
        close();
    }
}

void NewUserGuide::calculateTargets()
{
    m_items.clear();
    if (!m_parentWindow) return;

    auto addTarget = [&](const QString &name, const QString &desc, const QString &detail, GuideItem::Position pos) {
        QWidget *w = m_parentWindow->findChild<QWidget*>(name);
        if (w && w->isVisible()) {
            QPoint p = w->mapToGlobal(QPoint(0, 0));
            p = this->mapFromGlobal(p);
            m_items.append({QRect(p, w->size()), desc, detail, pos, QPoint(), QPoint(), QRect()});
        }
    };

    addTarget("HomeButton", "主页", 
              "主页按钮：\n点击此处可以查看所有用户的实时画面。\n这是您的主要语音和视频互动界面。", 
              GuideItem::Right);
              
    addTarget("Function1Button", "故事板", 
              "故事板：\n李哥做的故事板白板。\n等于浏览器。", 
              GuideItem::Right);
              
    addTarget("SettingButton", "系统设置", 
              "系统设置：\n配置软件的各项参数选项。\n包括网络设置、用户偏好和音频视频配置。", 
              GuideItem::Right);
              
    // Split Tools
    addTarget("ToolLocalDraw", "本地绘制", 
              "本地绘制工具：\n在您的屏幕上进行标注和绘图。\n方便给别人观看讲解。", 
              GuideItem::Bottom);
              
    addTarget("ToolTask", "公告任务", 
              "任务管理：\n您可以给所有用户发布广播通知。\n查看和管理当前进行的任务。", 
              GuideItem::Bottom);
              
    addTarget("ToolClear", "清空标注", 
              "清空标注：\n一键清除屏幕上所有的绘图和标注。\n别人在您屏幕上的绘制一键清理。", 
              GuideItem::Bottom);

    addTarget("MeetingContainer", "会议", 
              "会议功能：\n发起会议邀请。\n把您的屏幕推送给团队或者个人。", 
              GuideItem::Right);

    // Remote Control Button Logic
    bool remoteBtnAdded = false;
    AnnotationToolbar *toolbar = m_parentWindow->findChild<AnnotationToolbar*>();
    
    // Hide fake button initially
    if (m_fakeRemoteBtn) m_fakeRemoteBtn->setVisible(false);

    if (toolbar) {
        QPushButton *remoteBtn = toolbar->remoteControlButton();
        if (remoteBtn) {
             // Try to use the real button if it's visible and on screen
             if (remoteBtn->isVisible() && !remoteBtn->visibleRegion().isEmpty()) {
                 QPoint p = remoteBtn->mapToGlobal(QPoint(0, 0));
                 p = this->mapFromGlobal(p);
                 // Simple check if it's inside the window
                 if (this->rect().contains(p)) {
                     m_items.append({QRect(p, remoteBtn->size()), "远程控制", 
                                     "远程控制：\n观看别人画面时会出现的按钮。\n控制对方电脑。\n协助对方进行操作。", 
                                     GuideItem::Bottom, QPoint(), QPoint(), QRect()});
                     remoteBtnAdded = true;
                 }
             }
        }
    }
    
    // Fallback: Use fake button if real one not found/visible
    if (!remoteBtnAdded && m_fakeRemoteBtn) {
        // Estimate position: Top center-right
        // Assuming video top bar is at top, and annotation toolbar is centered.
        // Let's place it at top 50px, center + 200px offset roughly
        int fakeX = width() / 2 + 180; 
        int fakeY = 42; // Approximation of where it would be
        m_fakeRemoteBtn->move(fakeX, fakeY);
        m_fakeRemoteBtn->setVisible(true);
        m_fakeRemoteBtn->raise(); // Ensure on top

        m_items.append({m_fakeRemoteBtn->geometry(), "远程控制", 
                        "远程控制：\n请求控制对方电脑。\n协助对方进行操作。", 
                        GuideItem::Bottom, QPoint(), QPoint(), QRect()});
    }

    // Don't reset index if we are just resizing, unless invalid
    if (m_currentIndex >= m_items.size()) m_currentIndex = 0;
    if (m_items.isEmpty()) return;
    
    // Ensure button text is correct if we re-calculated
    if (m_currentIndex == m_items.size() - 1) {
        m_dismissBtn->setText("完成");
    } else {
        m_dismissBtn->setText("下一步");
    }

    layoutItems();
}

void NewUserGuide::layoutItems()
{
    if (m_items.isEmpty()) return;

    // Only layout the current item
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size()) return;
    
    GuideItem &item = m_items[m_currentIndex];
    
    // Only show fake button if it's the current target
    if (m_fakeRemoteBtn) {
        if (item.description == "远程控制" && item.rect == m_fakeRemoteBtn->geometry()) {
             m_fakeRemoteBtn->setVisible(true);
             m_fakeRemoteBtn->raise();
        } else {
             m_fakeRemoteBtn->setVisible(false);
        }
    }

    QFont font = this->font();
    font.setPointSize(12);
    font.setBold(true);
    QFontMetrics fm(font);
    
    QRect textBound = fm.boundingRect(item.description);
    int textW = textBound.width() + 20; 
    int textH = textBound.height() + 10;
    int lineLen = 50;

    if (item.position == GuideItem::Right) {
        item.lineStart = item.rect.center() + QPoint(item.rect.width()/2 + 5, 0);
        item.lineEnd = item.lineStart + QPoint(lineLen, 0); 
        item.textRect = QRect(item.lineEnd.x() + 10, item.lineEnd.y() - textH/2, textW, textH);
    } else if (item.position == GuideItem::Top) {
        item.lineStart = item.rect.center() - QPoint(0, item.rect.height()/2 + 5);
        item.lineEnd = item.lineStart - QPoint(0, lineLen + 20); 
        item.textRect = QRect(item.lineEnd.x() - textW/2, item.lineEnd.y() - textH - 5, textW, textH);
    } else { // Bottom
        item.lineStart = item.rect.center() + QPoint(0, item.rect.height()/2 + 5);
        item.lineEnd = item.lineStart + QPoint(0, lineLen);
        item.textRect = QRect(item.lineEnd.x() - textW/2, item.lineEnd.y() + 10, textW, textH);
    }
    
    // No collision detection needed for single item mode
}

void NewUserGuide::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    calculateTargets();
    if (m_dismissBtn) {
        m_dismissBtn->adjustSize();
        m_dismissBtn->move((width() - m_dismissBtn->width()) / 2, height() - 100);
    }
}

void NewUserGuide::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    calculateTargets();
    if (m_dismissBtn) {
        m_dismissBtn->move((width() - m_dismissBtn->width()) / 2, height() - 100);
    }
}

bool NewUserGuide::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_parentWindow && event->type() == QEvent::Resize) {
        resize(m_parentWindow->size());
    }
    return QWidget::eventFilter(obj, event);
}

void NewUserGuide::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_items.isEmpty() || m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
        // Just draw background if no items (shouldn't happen)
         p.setBrush(QColor(0, 0, 0, 200));
         p.setPen(Qt::NoPen);
         p.drawRect(rect());
         return;
    }

    const GuideItem &item = m_items[m_currentIndex];

    // Draw background with hole for current item
    QPainterPath path;
    path.addRect(rect());
    path.addRoundedRect(item.rect, 8, 8);
    
    p.setBrush(QColor(0, 0, 0, 200)); // Darker background
    p.setPen(Qt::NoPen);
    p.drawPath(path);

    // Draw indicator for current item
    p.setPen(QColor(255, 255, 255));
    QFont font = p.font();
    font.setPointSize(12);
    font.setBold(true);
    p.setFont(font);

    // Highlight border
    p.setPen(QPen(QColor(0, 120, 212), 3));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(item.rect.adjusted(-2,-2,2,2), 10, 10);

    // Text and line
    p.setPen(QPen(Qt::white, 1));
    p.drawLine(item.lineStart, item.lineEnd);
    p.drawText(item.textRect, Qt::AlignCenter, item.description);
    
    // Draw a small dot at line start
    p.setBrush(Qt::white);
    p.drawEllipse(item.lineStart, 3, 3);
    
    // Draw Central Description Box
    // Increased size as requested
    QRect centerRect(width()/2 - 350, height()/2 - 150, 700, 300);
    // Remove background box as requested
    // p.setBrush(QColor(40, 40, 40, 240));
    // p.setPen(QPen(QColor(80, 80, 80), 1));
    // p.drawRoundedRect(centerRect, 10, 10);
    
    p.setPen(Qt::white);
    
    // Add letter spacing
    QFont detailFont = p.font();
    detailFont.setLetterSpacing(QFont::AbsoluteSpacing, 2); // Slight spacing
    detailFont.setPointSize(14); // Make it slightly larger for readability since no box
    p.setFont(detailFont);

    p.drawText(centerRect.adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap, item.detailText);
}

