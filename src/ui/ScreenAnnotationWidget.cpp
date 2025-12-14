#include "ScreenAnnotationWidget.h"
#include <QApplication>
#include <QScreen>
#include <QInputDialog>
#include <QtMath>
#include <QKeyEvent>

ScreenAnnotationWidget::ScreenAnnotationWidget(QWidget *parent)
    : QWidget(parent)
    , m_isDrawing(false)
    , m_currentTool(0)
    , m_currentColorId(0)
    , m_enabled(false)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowTransparentForInput);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
}

ScreenAnnotationWidget::~ScreenAnnotationWidget()
{
}

void ScreenAnnotationWidget::setTool(int toolMode)
{
    m_currentTool = toolMode;
    // Update cursor
    if (m_currentTool == 0) {
        setCursor(Qt::ArrowCursor);
        setEnabled(false);
    } else {
        if (m_currentTool == 1) { // Pen
            // Custom white dot cursor
            QPixmap pixmap(16, 16);
            pixmap.fill(Qt::transparent);
            QPainter p(&pixmap);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(Qt::white);
            p.drawEllipse(4, 4, 8, 8); // 8px circle in center
            setCursor(QCursor(pixmap, 8, 8)); // Hotspot at center
        } else if (m_currentTool == 6) { // Eraser
            setCursor(Qt::ForbiddenCursor); 
        } else {
            setCursor(Qt::CrossCursor);
        }
        setEnabled(true);
    }
}

void ScreenAnnotationWidget::setColorId(int id)
{
    m_currentColorId = id;
}

void ScreenAnnotationWidget::clear()
{
    m_items.clear();
    update();
}

void ScreenAnnotationWidget::undo()
{
    if (!m_items.isEmpty()) {
        m_items.removeLast();
        update();
    }
}

void ScreenAnnotationWidget::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    
    // Changing window flags requires hiding and showing
    bool wasVisible = isVisible();
    hide();
    
    if (enabled) {
        setWindowFlag(Qt::WindowTransparentForInput, false);
    } else {
        setWindowFlag(Qt::WindowTransparentForInput, true);
    }
    
    show();
    if (enabled) {
        raise();
    }
}

QColor ScreenAnnotationWidget::getColor(int id) const
{
    switch (id) {
    case 0: return QColor(255, 59, 48);   // Red
    case 1: return QColor(52, 199, 89);   // Green
    case 2: return QColor(10, 132, 255);  // Blue
    default: return QColor(255, 214, 10); // Yellow
    }
}

void ScreenAnnotationWidget::drawItem(QPainter &painter, const AnnotationItem &item)
{
    if (item.type == AnnotationItem::Eraser) {
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        QPen pen(Qt::transparent, 20); // Eraser width
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(item.path);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        return;
    }

    QColor color = getColor(item.colorId);
    QPen pen(color, item.penWidth);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (item.type) {
    case AnnotationItem::Pen:
        painter.drawPolyline(item.path);
        break;
    case AnnotationItem::Rect: {
        QRectF rect(item.start, item.end);
        painter.drawRect(rect.normalized());
        break;
    }
    case AnnotationItem::Circle: {
        QRectF rect(item.start, item.end);
        painter.drawEllipse(rect.normalized());
        break;
    }
    case AnnotationItem::Arrow: {
        QLineF line(item.start, item.end);
        painter.drawLine(line);
        
        // Draw arrow head
        double angle = std::atan2(-line.dy(), line.dx());
        double arrowSize = 15;
        QPointF arrowP1 = item.end - QPointF(sin(angle + M_PI / 3) * arrowSize, cos(angle + M_PI / 3) * arrowSize);
        QPointF arrowP2 = item.end - QPointF(sin(angle + M_PI - M_PI / 3) * arrowSize, cos(angle + M_PI - M_PI / 3) * arrowSize);
        
        QPolygonF arrowHead;
        arrowHead << item.end << arrowP1 << arrowP2;
        painter.setBrush(color);
        painter.drawPolygon(arrowHead);
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case AnnotationItem::Text: {
        painter.setPen(color);
        QFont font = painter.font();
        font.setPixelSize(24);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(item.start, item.text);
        break;
    }
    default:
        break;
    }
}

void ScreenAnnotationWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // If enabled (drawing mode), paint a nearly invisible background to capture mouse events
    if (m_enabled) {
        painter.fillRect(rect(), QColor(0, 0, 0, 1));
    }

    // Draw all items
    for (const auto &item : m_items) {
        drawItem(painter, item);
    }

    // Draw current item being drawn
    if (m_isDrawing) {
        drawItem(painter, m_currentItem);
    }
}

void ScreenAnnotationWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    if (m_currentTool == 0) return; // None

    m_isDrawing = true;
    m_currentItem.type = static_cast<AnnotationItem::Type>(m_currentTool);
    if (m_currentTool == 6) { // Eraser mapped to internal type if needed, or handle separately
         m_currentItem.type = AnnotationItem::Eraser;
    } else {
        // Map tool mode to type: 1->Pen, 2->Rect, 3->Circle, 4->Arrow, 5->Text
        // My enum: Pen=0, Rect=1... wait.
        // Enum: Pen, Rect, Circle, Arrow, Text, Eraser
        // ToolMode: 1:Pen, 2:Rect, 3:Circle, 4:Arrow, 5:Text, 6:Eraser
        switch(m_currentTool) {
            case 1: m_currentItem.type = AnnotationItem::Pen; break;
            case 2: m_currentItem.type = AnnotationItem::Rect; break;
            case 3: m_currentItem.type = AnnotationItem::Circle; break;
            case 4: m_currentItem.type = AnnotationItem::Arrow; break;
            case 5: m_currentItem.type = AnnotationItem::Text; break;
            case 6: m_currentItem.type = AnnotationItem::Eraser; break;
        }
    }
    
    m_currentItem.colorId = m_currentColorId;
    m_currentItem.start = event->pos();
    m_currentItem.end = event->pos();
    m_currentItem.path.clear();
    m_currentItem.path << event->pos();

    if (m_currentItem.type == AnnotationItem::Text) {
        bool ok;
        QString text = QInputDialog::getText(this, "输入文字", "请输入内容:", QLineEdit::Normal, "", &ok);
        if (ok && !text.isEmpty()) {
            m_currentItem.text = text;
            m_items.append(m_currentItem);
        }
        m_isDrawing = false;
        update();
    }
}

void ScreenAnnotationWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_isDrawing) return;

    m_currentItem.end = event->pos();
    if (m_currentItem.type == AnnotationItem::Pen || m_currentItem.type == AnnotationItem::Eraser) {
        m_currentItem.path << event->pos();
    }
    update();
}

void ScreenAnnotationWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_isDrawing) return;

    if (m_currentItem.type != AnnotationItem::Text) {
        m_currentItem.end = event->pos();
        if (m_currentItem.type == AnnotationItem::Pen || m_currentItem.type == AnnotationItem::Eraser) {
            m_currentItem.path << event->pos();
        }
        m_items.append(m_currentItem);
    }
    m_isDrawing = false;
    update();
}

void ScreenAnnotationWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_isDrawing) {
            m_isDrawing = false;
            update();
        }
        emit toolCancelled();
    }
    QWidget::keyPressEvent(event);
}
