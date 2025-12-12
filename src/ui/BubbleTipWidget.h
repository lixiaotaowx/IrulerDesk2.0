#ifndef BUBBLETIPWIDGET_H
#define BUBBLETIPWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QColor>

class BubbleTipWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BubbleTipWidget(QWidget *parent = nullptr);
    
    // 显示气泡
    // text: 显示的文本
    // anchorPoint: 目标控件（头像）的右侧中心点（全局坐标）
    // isSelf: 是否是当前用户（决定颜色）
    void showBubble(const QString &text, const QPoint &anchorPoint, bool isSelf);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QLabel *m_label;
    QColor m_backgroundColor;
    
    // 样式常量
    static const int ARROW_WIDTH = 6;  // 箭头宽度
    static const int ARROW_HEIGHT = 10; // 箭头高度
    static const int BORDER_RADIUS = 4; // 圆角半径
    static const int PADDING_H = 10;    // 水平内边距
    static const int PADDING_V = 8;     // 垂直内边距
};

#endif // BUBBLETIPWIDGET_H
