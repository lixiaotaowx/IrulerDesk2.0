#pragma once
#include <QWidget>
#include <QMouseEvent>
#include <QListWidget>
#include <QTimer>
#include <QLabel>
#include "StreamClient.h"
#include "LoginClient.h"

class NewUiWindow : public QWidget
{
    Q_OBJECT

public:
    explicit NewUiWindow(QWidget *parent = nullptr);
    ~NewUiWindow();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void onTimerTimeout();
    void onStreamLog(const QString &msg);
    void onUserListUpdated(const QJsonArray &users);
    void onLoginConnected();

private:
    void setupUi();
    void updateListWidget(const QJsonArray &users);
    
    // Dragging support
    bool m_dragging = false;
    QPoint m_dragPosition;

    QListWidget *m_listWidget = nullptr;
    QTimer *m_timer = nullptr;
    QLabel *m_videoLabel = nullptr; // Local preview label (Index 0)
    StreamClient *m_streamClient = nullptr;
    LoginClient *m_loginClient = nullptr;

    QString m_myStreamId; // Store my own ID to identify myself in the list
    QString m_myUserName;

    // Remote Stream Management
    QMap<QString, StreamClient*> m_remoteStreams; // userId -> StreamClient
    QMap<QString, QListWidgetItem*> m_userItems;  // userId -> ListWidgetItem
    QMap<QString, QLabel*> m_userLabels;          // userId -> Image Label (for updating frame)

    // Layout constants
    int m_cardBaseWidth;
    int m_bottomAreaHeight;
    int m_shadowSize;
    double m_aspectRatio;
    int m_cardBaseHeight;
    int m_totalItemWidth;
    int m_totalItemHeight;
    int m_imgWidth;
    int m_imgHeight;
    int m_marginX;
    int m_topAreaHeight;
    int m_marginTop;
};
