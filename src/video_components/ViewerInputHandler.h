#ifndef VIEWER_INPUT_HANDLER_H
#define VIEWER_INPUT_HANDLER_H

#include <QObject>
#include <QLabel>
#include <QPointer>
#include "../player/WebSocketReceiver.h"

class ViewerInputHandler : public QObject
{
    Q_OBJECT
public:
    explicit ViewerInputHandler(QObject *parent = nullptr);
    
    void setVideoLabel(QLabel *label);
    void setReceiver(WebSocketReceiver *receiver);
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    void setSourceSize(const QSize &size);

signals:
    void mouseClicked();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void handleMouseEvent(QEvent *event);
    void handleKeyEvent(QEvent *event);
    QPoint mapToRemote(const QPoint &localPos);

    QPointer<QLabel> m_videoLabel;
    QPointer<WebSocketReceiver> m_receiver;
    bool m_enabled;
    QSize m_sourceSize;
};

#endif // VIEWER_INPUT_HANDLER_H
