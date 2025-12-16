#include "SnippetOverlay.h"
#include <QPainter>
#include <QMouseEvent>
#include <QClipboard>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <QFontMetrics>

SnippetOverlay::SnippetOverlay(const QPixmap &fullPixmap, QWidget *parent)
    : QWidget(parent)
    , m_fullPixmap(fullPixmap)
    , m_isSelecting(false)
    , m_selectionFinished(false)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::CrossCursor);
    
    // Set geometry to cover the screen(s) or match the pixmap
    // If fullPixmap comes from a specific screen grab, we should position it there.
    // For now, assume the caller positions us or the pixmap matches the widget size.
    // But usually we want fullscreen.
    // We will resize to match pixmap size.
    resize(m_fullPixmap.size());
}

void SnippetOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    
    // Draw full background dimmed
    painter.drawPixmap(0, 0, m_fullPixmap);
    painter.fillRect(rect(), QColor(0, 0, 0, 100)); // Dim layer
    
    // Draw selection
    if (!m_selectionRect.isNull()) {
        // Draw the selected part clearly
        painter.drawPixmap(m_selectionRect, m_fullPixmap, m_selectionRect);
        
        // Draw border
        QPen pen(QColor(30, 144, 255), 2); // DodgerBlue
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(m_selectionRect);
        
        // Draw W x H info
        QString sizeText = QString("%1 x %2").arg(m_selectionRect.width()).arg(m_selectionRect.height());
        
        QFont font = painter.font();
        font.setPixelSize(12);
        font.setBold(true);
        painter.setFont(font);
        
        QFontMetrics fm(font);
        int textW = fm.horizontalAdvance(sizeText) + 10;
        int textH = fm.height() + 6;
        
        // Position near mouse cursor if selecting, otherwise bottom-right of rect
        QPoint textPos;
        if (m_isSelecting) {
            textPos = m_currentPos + QPoint(10, 10);
        } else {
            textPos = m_selectionRect.bottomRight() + QPoint(5, 5);
        }

        // Keep inside screen
        if (textPos.x() + textW > width()) textPos.setX(width() - textW - 5);
        if (textPos.y() + textH > height()) textPos.setY(textPos.y() - textH - 20); // Move above if at bottom
        
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 180));
        painter.drawRoundedRect(textPos.x(), textPos.y(), textW, textH, 4, 4);
        
        painter.setPen(Qt::white);
        painter.drawText(QRect(textPos.x(), textPos.y(), textW, textH), Qt::AlignCenter, sizeText);
    }
}

void SnippetOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isSelecting = true;
        m_startPos = event->pos();
        m_currentPos = m_startPos;
        m_selectionRect = QRect();
        update();
    } else if (event->button() == Qt::RightButton) {
        cancelSelection();
    }
}

void SnippetOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isSelecting) {
        m_currentPos = event->pos();
        m_selectionRect = QRect(m_startPos, m_currentPos).normalized();
        update();
    }
}

void SnippetOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isSelecting = false;
        m_selectionFinished = true;
        update();
    }
}

void SnippetOverlay::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_selectionRect.isEmpty()) {
        confirmSelection();
    }
}

void SnippetOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancelSelection();
    } else if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) {
        if (!m_selectionRect.isEmpty()) {
            confirmSelection();
        }
    } else {
        QWidget::keyPressEvent(event);
    }
}

void SnippetOverlay::confirmSelection()
{
    if (m_selectionRect.isValid() && !m_selectionRect.isEmpty()) {
        QPixmap cropped = m_fullPixmap.copy(m_selectionRect);
        QClipboard *clipboard = QGuiApplication::clipboard();
        if (clipboard) {
            clipboard->setPixmap(cropped);
        }
    }
    close();
}

void SnippetOverlay::cancelSelection()
{
    close();
}
