#ifndef AVATARSETTINGSWINDOW_H
#define AVATARSETTINGSWINDOW_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QFrame>
#include <QApplication>

class AvatarSettingsWindow : public QWidget
{
    Q_OBJECT

public:
    explicit AvatarSettingsWindow(QWidget *parent = nullptr);
    ~AvatarSettingsWindow();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onMinimizeClicked();
    void onCloseClicked();
    void onAvatarClicked(int iconId);

signals:
    void avatarSelected(int iconId);

private:
    void setupUI();
    void setupCustomTitleBar();
    void setupAvatarGrid();
    void loadAvatarImages();
    void selectAvatar(int iconId);
    
    // UI组件
    QVBoxLayout *m_mainLayout;
    QWidget *m_titleBar;
    QHBoxLayout *m_titleBarLayout;
    QLabel *m_titleLabel;
    QPushButton *m_minimizeButton;
    QPushButton *m_closeButton;
    
    QScrollArea *m_scrollArea;
    QWidget *m_contentWidget;
    QVBoxLayout *m_contentLayout;
    QGridLayout *m_avatarGridLayout;
    
    // 窗口拖拽相关
    bool m_dragging;
    QPoint m_dragPosition;
    
    // 头像选择相关
    int m_selectedIconId;
    QLabel *m_selectedAvatarLabel;
    QList<QLabel*> m_avatarLabels;
    
    // 常量
    static const int AVATAR_SIZE = 80;
    static const int GRID_COLUMNS = 4;
    static const int WINDOW_WIDTH = 400;
    static const int WINDOW_HEIGHT = 500;
};

#endif // AVATARSETTINGSWINDOW_H