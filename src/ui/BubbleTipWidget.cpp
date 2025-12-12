#include "BubbleTipWidget.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>

static const int SHADOW_MARGIN = 8;

BubbleTipWidget::BubbleTipWidget(QWidget *parent)
    : QWidget(parent)
{
    // 设置窗口属性：无边框、置顶、透明背景、不接受焦点
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    
    // 初始化布局和标签
    QHBoxLayout *layout = new QHBoxLayout(this);
    // 增加阴影边距
    layout->setContentsMargins(ARROW_WIDTH + PADDING_H + SHADOW_MARGIN, 
                               PADDING_V + SHADOW_MARGIN, 
                               PADDING_H + SHADOW_MARGIN, 
                               PADDING_V + SHADOW_MARGIN);
    
    m_label = new QLabel(this);
    m_label->setStyleSheet("color: black; font-family: 'Microsoft YaHei'; font-size: 14px; font-weight: bold;");
    layout->addWidget(m_label);
    
    // 移除 QGraphicsDropShadowEffect，改为在 paintEvent 中手动绘制
}

void BubbleTipWidget::showBubble(const QString &text, const QPoint &anchorPoint, bool isSelf)
{
    m_label->setText(text);
    m_backgroundColor = isSelf ? QColor(149, 236, 105) : QColor(255, 255, 255); // 微信绿 or 白色
    
    // 调整大小以适应文本
    adjustSize();
    
    // 计算位置：anchorPoint是目标控件的右边缘中心
    // 我们需要把气泡的左边缘（箭头尖端）对准anchorPoint
    // 注意：需要减去阴影边距，使视觉上的箭头尖端对准 anchorPoint
    int x = anchorPoint.x() - SHADOW_MARGIN;
    int y = anchorPoint.y() - height() / 2;
    
    move(x, y);
    show();
    raise();
}

void BubbleTipWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 整个控件区域
    QRect rect = this->rect();
    
    // 计算实际内容区域（减去阴影边距）
    QRect visualRect = rect.adjusted(SHADOW_MARGIN, SHADOW_MARGIN, -SHADOW_MARGIN, -SHADOW_MARGIN);
    
    // 定义气泡主体矩形（相对于visualRect）
    QRect bodyRect = visualRect;
    bodyRect.setLeft(visualRect.left() + ARROW_WIDTH); // 左侧留出箭头位置
    
    // 构建路径
    QPainterPath path;
    path.addRoundedRect(bodyRect, BORDER_RADIUS, BORDER_RADIUS);
    
    // 箭头中心点垂直位置
    int centerY = visualRect.center().y();
    // 箭头尖端
    QPoint arrowTip(visualRect.left(), centerY);
    // 箭头与主体连接的上点
    QPoint arrowTop(visualRect.left() + ARROW_WIDTH, centerY - ARROW_HEIGHT / 2);
    // 箭头与主体连接的下点
    QPoint arrowBottom(visualRect.left() + ARROW_WIDTH, centerY + ARROW_HEIGHT / 2);
    
    QPainterPath arrowPath;
    arrowPath.moveTo(arrowTop);
    arrowPath.lineTo(arrowTip);
    arrowPath.lineTo(arrowBottom);
    
    path = path.united(arrowPath);
    
    // 1. 绘制阴影 (简单模拟)
    // 向下偏移2像素
    painter.save();
    painter.translate(0, 2); 
    painter.setBrush(QColor(0, 0, 0, 30)); // 半透明黑色
    painter.setPen(Qt::NoPen);
    painter.drawPath(path);
    painter.restore();
    
    // 2. 绘制主体
    painter.setBrush(m_backgroundColor);
    painter.setPen(Qt::NoPen);
    painter.drawPath(path);
}
