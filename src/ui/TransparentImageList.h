#ifndef TRANSPARENTIMAGELIST_H
#define TRANSPARENTIMAGELIST_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QMouseEvent>
#include <QTimer>
#include <QMap>
#include <QPixmap>
#include <QFile>
#include <QPainterPath>
#include <QPainter>
#include <QEvent>
#include <QMenu>
#include <QAction>
#include <QMovie>
#include <QApplication>
#include <QSvgRenderer>
#include <QJsonArray>
#include <QJsonObject>
#include <memory>
#include "BubbleTipWidget.h"

class TransparentImageList : public QWidget
{
    Q_OBJECT

public:
    explicit TransparentImageList(QWidget *parent = nullptr);
    ~TransparentImageList();

    // 添加用户头像
    void addUser(const QString &userId, const QString &userName);
    void addUser(const QString &userId, const QString &userName, int iconId); // 新增：支持iconId
    
    // 移除用户头像
    void removeUser(const QString &userId);
    
    // 更新用户列表
    void updateUserList(const QStringList &onlineUsers);
    void updateUserList(const QJsonArray &onlineUsers);  // 新增：支持JSON用户数据
    
    // 设置默认头像路径
    void setDefaultAvatarPath(const QString &path);
    
    // 更新用户头像
    void updateUserAvatar(const QString &userId, int iconId);
    
    // 清空用户列表
    void clearUserList();

    // 获取当前用户ID
    QString getCurrentUserId() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void onImageClicked(const QString &userId);

signals:
    void userImageClicked(const QString &userId, const QString &userName);
    void showMainListRequested();
    void setAvatarRequested();  // 新增：设置头像请求信号
    void systemSettingsRequested(); // 新增：系统设置请求信号
    void clearMarksRequested(); // 新增：清理标记请求
    void exitRequested();       // 新增：退出请求
    void hideRequested();
    void micToggleRequested(bool enabled);
    void speakerToggleRequested(bool enabled);

private:
    struct UserImageItem {
        QLabel *imageLabel;
        QString userId;
        QString userName;
        QGraphicsOpacityEffect *opacityEffect;
        QPropertyAnimation *fadeAnimation;
        QMovie *gifMovie; // 用于播放GIF动画
    };

    void setupUI();
    void updateLayout();
    void createUserImage(const QString &userId, const QString &userName);
    QLabel* createUserImage(const QString &userId);  // 创建用户图片标签
    QLabel* createUserImage(const QString &userId, int iconId);  // 新增：支持指定icon ID
    void removeUserImage(const QString &userId);
    void loadStaticImage(QLabel* label, const QString& pngPath);
    void loadDefaultAvatar(QLabel* label);
    void positionOnScreen();// 获取用户的icon ID
    int getUserIconId(const QString &userId);  // 获取用户的icon ID   

public slots:
    void setCurrentUserInfo(const QString &currentUserId, int currentUserIconId);  // 设置当前用户信息

private:
    QVBoxLayout *m_mainLayout;
    QVBoxLayout *m_layout;  // 添加m_layout成员变量
    QMap<QString, UserImageItem*> m_userImages;
    QMap<QString, int> m_userIconIds;  // 存储用户的图标ID
    QList<QLabel*> m_userLabels;  // 添加m_userLabels成员变量
    QString m_defaultAvatarPath;
    
    // 当前用户信息
    QString m_currentUserId;
    int m_currentUserIconId;
    
    // 图片尺寸和间距
    static const int IMAGE_SIZE = 80;
    static const int IMAGE_SPACING = 10;
    static const int MARGIN_FROM_EDGE = 5; // 减少边距，让图片更靠近边缘
    // 位置与拖动状态
    int m_screenIndex = -1;
    bool m_anchorRight = false;
    bool m_dragCandidate = false;
    bool m_draggingList = false;
    QPoint m_dragStartGlobal;
    int m_dragThreshold = 8;
    int m_offsetY = -1;
    
    // 气泡提示
    BubbleTipWidget *m_bubbleTip;

    void alignToScreenIndex(int index);
    void readPositionFromConfig();
    void writePositionToConfig();
    void updateDragPosition(const QPoint &globalPos);
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
};

#endif // TRANSPARENTIMAGELIST_H
