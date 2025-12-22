#include "BubbleTipWidget.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>

BubbleTipWidget::BubbleTipWidget(QWidget *parent)
    : QWidget(parent)
{
    // 设置窗口属性：无边框、置顶、透明背景、不接受焦点
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    
    // 初始化布局和标签
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(ARROW_WIDTH + PADDING_H,
                               PADDING_V,
                               PADDING_H,
                               PADDING_V);
    
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
    int x = anchorPoint.x();
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

    // 定义气泡主体矩形
    QRect bodyRect = rect;
    bodyRect.setLeft(rect.left() + ARROW_WIDTH); // 左侧留出箭头位置
    
    // 构建路径
    QPainterPath path;
    path.addRoundedRect(bodyRect, BORDER_RADIUS, BORDER_RADIUS);
    
    // 箭头中心点垂直位置
    int centerY = rect.center().y();
    // 箭头尖端
    QPoint arrowTip(rect.left(), centerY);
    // 箭头与主体连接的上点
    QPoint arrowTop(rect.left() + ARROW_WIDTH, centerY - ARROW_HEIGHT / 2);
    // 箭头与主体连接的下点
    QPoint arrowBottom(rect.left() + ARROW_WIDTH, centerY + ARROW_HEIGHT / 2);
    
    QPainterPath arrowPath;
    arrowPath.moveTo(arrowTop);
    arrowPath.lineTo(arrowTip);
    arrowPath.lineTo(arrowBottom);
    
    path = path.united(arrowPath);

    painter.setBrush(m_backgroundColor);
    painter.setPen(Qt::NoPen);
    painter.drawPath(path);
}
