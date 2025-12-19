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
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onTimerTimeout();
    void onStreamLog(const QString &msg);
    void onUserListUpdated(const QJsonArray &users);
    void onLoginConnected();

public:
    void setMyStreamId(const QString &id, const QString &name = QString());

    // Viewer List Management
    void addViewer(const QString &id, const QString &name);
    void removeViewer(const QString &id);
    void clearViewers(); // Clear all viewers
    int getViewerCount() const; // Get current viewer count

public:
    // Main Program Integration Methods
    void addUser(const QString &userId, const QString &userName);
    void addUser(const QString &userId, const QString &userName, int iconId);
    void removeUser(const QString &userId);
    void clearUserList();
    void updateUserAvatar(const QString &userId, int iconId);
    QString getCurrentUserId() const; // Returns the local user ID

signals:
    void startWatchingRequested(const QString &targetId);
    void systemSettingsRequested();
    void clearMarksRequested();
    void toggleStreamingIslandRequested();

private:
    void setupUi();
    void updateListWidget(const QJsonArray &users);
    
    // Dragging support
    bool m_dragging = false;
    QPoint m_dragPosition;

    QListWidget *m_listWidget = nullptr;
    QTimer *m_timer = nullptr;
    QLabel *m_videoLabel = nullptr; // Local preview label (Index 0)
    QLabel *m_logoLabel = nullptr;
    QWidget *m_farRightPanel = nullptr; // Far right panel (My Room)
    QListWidget *m_viewerList = nullptr; // Viewer list widget
    QMap<QString, QListWidgetItem*> m_viewerItems; // Viewer ID -> List Item
    QLabel *m_localNameLabel = nullptr; // Local name label (Index 0)
    QFrame *m_localCard = nullptr; // Local card frame (Index 0)
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
