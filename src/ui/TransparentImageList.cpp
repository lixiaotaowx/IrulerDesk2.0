#include "TransparentImageList.h"
#include <QApplication>
#include <QScreen>
#include <QContextMenuEvent>
#include <QPainter>
#include <QBitmap>
#include <QMouseEvent>
#include <QTimer>

TransparentImageList::TransparentImageList(QWidget *parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_layout(nullptr)
    , m_defaultAvatarPath(":/images/default_avatar.png")
{
    setupUI();
    positionOnScreen();
}

TransparentImageList::~TransparentImageList()
{
    // 清理所有用户图片项
    for (auto it = m_userImages.begin(); it != m_userImages.end(); ++it) {
        delete it.value()->fadeAnimation;
        delete it.value()->opacityEffect;
        delete it.value()->imageLabel;
        delete it.value();
    }
    m_userImages.clear();
}

void TransparentImageList::setupUI()
{
    // 设置窗口属性
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    
    // 创建主布局
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0); // 恢复贴边设置
    m_mainLayout->setSpacing(IMAGE_SPACING); // 确保有间距
    m_mainLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter); // 列表整体垂直居中，但左对齐
    
    // 初始化m_layout为m_mainLayout
    m_layout = m_mainLayout;
    
    setLayout(m_mainLayout);
    
    // 设置初始大小
    resize(IMAGE_SIZE, 100); // 恢复原始宽度
}

void TransparentImageList::positionOnScreen()
{
    // 获取主屏幕
    QScreen *screen = QApplication::primaryScreen();
    if (!screen) return;
    
    QRect screenGeometry = screen->availableGeometry();
    
    // 计算窗口位置：屏幕左侧边缘，垂直居中
    int x = screenGeometry.x(); // 使用屏幕几何的x坐标，确保真正贴边
    int y = screenGeometry.y() + (screenGeometry.height() - height()) / 2;
    
    move(x, y);
}

int TransparentImageList::getUserIconId(const QString &userId)
{
    // 仅当前用户使用本地配置；其他用户不生成映射，交由服务器提供
    if (userId == m_currentUserId && m_currentUserIconId >= 3 && m_currentUserIconId <= 21) {
        return m_currentUserIconId;
    }
    // 返回-1表示使用默认头像（不使用数字icon）
    return -1;
}

void TransparentImageList::setCurrentUserInfo(const QString &currentUserId, int currentUserIconId)
{
    m_currentUserId = currentUserId;
    m_currentUserIconId = currentUserIconId;
}

void TransparentImageList::addUser(const QString &userId, const QString &userName)
{
    if (m_userImages.contains(userId)) {
        return; // 用户已存在
    }
    
    createUserImage(userId, userName);
    updateLayout();
}

void TransparentImageList::removeUser(const QString &userId)
{
    if (!m_userImages.contains(userId)) {
        return; // 用户不存在
    }
    
    removeUserImage(userId);
    updateLayout();
}

void TransparentImageList::clearUserList()
{
    
    
    // 删除所有用户标签 - 使用更安全的方式
    for (int i = 0; i < m_userLabels.size(); ++i) {
        QLabel* label = m_userLabels[i];
        if (label) {
            label->setParent(nullptr);  // 先移除父对象
            label->deleteLater();
        }
    }
    m_userLabels.clear();
    
    
    // 清空用户图片映射 - 使用更安全的方式
    QStringList userIds = m_userImages.keys();
    for (const QString& userId : userIds) {
        UserImageItem* item = m_userImages.value(userId);
        if (item) {
            if (item->imageLabel) {
                item->imageLabel = nullptr;
            }
            if (item->opacityEffect) {
                item->opacityEffect = nullptr;
            }
            if (item->gifMovie) {
                item->gifMovie = nullptr;
            }
            if (item->fadeAnimation) {
                item->fadeAnimation = nullptr;
            }
            
            // 在删除前进行安全检查
            if (item->imageLabel && item->imageLabel->parent()) {
                item->imageLabel->setParent(nullptr);
            }
            // 暂时注释掉delete，看看是否能继续执行
            // delete item;
        } else {
        }
    }
    m_userImages.clear();
}

void TransparentImageList::updateUserList(const QStringList &onlineUsers)
{
    // 清空现有用户列表
    clearUserList();

    // 先固定把当前用户置于第一位，确保“自己始终可见”
    if (!m_currentUserId.isEmpty()) {
        int selfIconId = (m_currentUserIconId >= 3 && m_currentUserIconId <= 21) ? m_currentUserIconId : -1;
        QLabel* selfLabel = createUserImage(m_currentUserId, selfIconId);
        if (selfLabel) {
            m_layout->addWidget(selfLabel);
            m_userLabels.append(selfLabel);
        }
    }

    // 然后追加其他用户（跳过重复的当前用户）
    for (const QString &userId : onlineUsers) {
        if (userId == m_currentUserId) {
            continue; // 已添加为首位，跳过
        }
        int iconId = -1; // 其他用户使用默认（若服务器未提供）
        QLabel* userLabel = createUserImage(userId, iconId);
        if (userLabel) {
            m_layout->addWidget(userLabel);
            m_userLabels.append(userLabel);
        } else {
        }
    }
    
    // 调整窗口大小
    adjustSize();
    
    // 显示窗口
    if (!onlineUsers.isEmpty()) {
        show();
        raise();
        activateWindow();
    }
}

void TransparentImageList::updateUserList(const QJsonArray &onlineUsers)
{
    

    // 清空现有用户
    clearUserList();

    // 第一个永远是自己（使用本地配置，不依赖服务器）
    if (!m_currentUserId.isEmpty()) {
        int selfIconId = (m_currentUserIconId >= 3 && m_currentUserIconId <= 21) ? m_currentUserIconId : -1;
        
        QLabel* selfLabel = createUserImage(m_currentUserId, selfIconId);
        if (selfLabel) {
            m_layout->addWidget(selfLabel);
            m_userLabels.append(selfLabel);
        } else {
        }
    } else {
    }


    // 然后添加服务器返回的其他在线用户（包括自己，但会跳过重复）
    for (int i = 0; i < onlineUsers.size(); ++i) {
        const QJsonValue& userValue = onlineUsers[i];
        if (!userValue.isObject()) {
            continue;
        }
        
        QJsonObject userObj = userValue.toObject();
        QString userId = userObj["id"].toString();
        
        // 跳过自己，因为已经在第一位了
        if (!userId.isEmpty() && userId == m_currentUserId) {
            continue;
        }
        
        QString userName = userObj["name"].toString();
        
        // 其他用户使用服务器提供的icon_id
        int serverIconId = -1;
        if (userObj.contains("icon_id")) {
            QJsonValue v = userObj["icon_id"];
            serverIconId = v.isString() ? v.toString().toInt() : v.toInt(-1);
        } else if (userObj.contains("viewer_icon_id")) {
            QJsonValue v = userObj["viewer_icon_id"];
            serverIconId = v.isString() ? v.toString().toInt() : v.toInt(-1);
        }

        // 验证服务器icon_id范围，无效则用红圈
        int iconId = (serverIconId >= 3 && serverIconId <= 21) ? serverIconId : -1;
        
        
        QLabel* userLabel = createUserImage(userId, iconId);
        if (userLabel) {
            m_layout->addWidget(userLabel);
            m_userLabels.append(userLabel);
        } else {
        }
    }
    
    
    // 显示窗口
    if (!m_userLabels.isEmpty()) {
        show();
        raise();
        activateWindow();
    } else {
    }
}

void TransparentImageList::setDefaultAvatarPath(const QString &path)
{
    m_defaultAvatarPath = path;
}

void TransparentImageList::createUserImage(const QString &userId, const QString &userName)
{
    // 使用真实icon_id（优先当前用户配置，否则稳定映射）
    int iconId = getUserIconId(userId);
    QLabel* label = createUserImage(userId, iconId);
    if (label) {
        m_layout->addWidget(label);
        m_userLabels.append(label);
    }
    
    // 更新用户名
    if (m_userImages.contains(userId)) {
        m_userImages[userId]->userName = userName;
    }
}

QLabel* TransparentImageList::createUserImage(const QString &userId)
{
    // 默认使用非数字icon（显示默认头像）
    return createUserImage(userId, -1);
}

QLabel* TransparentImageList::createUserImage(const QString &userId, int iconId)
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString gifPath;
    QString pngPath;
    bool useNumericIcon = (iconId >= 3 && iconId <= 21);
    if (useNumericIcon) {
        gifPath = QString("%1/maps/icon/%2.gif").arg(appDir).arg(iconId);
        pngPath = QString("%1/maps/icon/%2.png").arg(appDir).arg(iconId);
    } else {
    }

    // 创建标签
    QLabel* label = new QLabel(this);
    label->setFixedSize(80, 80);
    label->setScaledContents(true);
    label->setStyleSheet("border: none; border-radius: 40px; background: transparent;");
    
    // 设置用户ID属性，用于点击事件
    label->setProperty("userId", userId);
    
    // 确保标签可以接收鼠标事件
    label->setAttribute(Qt::WA_Hover, true);
    label->setMouseTracking(true);
    
    
    // 创建UserImageItem并存储到m_userImages中
    UserImageItem* item = new UserImageItem();
    item->imageLabel = label;
    item->userId = userId;
    item->userName = ""; // 默认为空，后续可以设置
    item->opacityEffect = nullptr;
    item->fadeAnimation = nullptr;
    item->gifMovie = nullptr;
    
    m_userImages[userId] = item;
    
    // 加载图片：优先数字icon；否则加载默认头像
    if (useNumericIcon && QFile::exists(gifPath)) {
        
        QMovie* movie = new QMovie(gifPath);
        movie->setScaledSize(QSize(76, 76));
        movie->setCacheMode(QMovie::CacheAll);
        
        if (movie->isValid()) {
            item->gifMovie = movie; // 存储到UserImageItem中
            
            // 设置只播放一次
            connect(movie, &QMovie::frameChanged, [movie](int frameNumber) {
                if (frameNumber == movie->frameCount() - 1) {
                    // 到达最后一帧时停止播放
                    movie->setPaused(true);
                }
            });
            
            label->setMovie(movie);
            movie->start();
            
            // 设置动画播放完成后的处理
            connect(movie, &QMovie::finished, [this, label, pngPath, userId]() {
                
                // 停止并删除动画
                QMovie* oldMovie = label->movie();
                if (oldMovie) {
                    oldMovie->stop();
                    oldMovie->deleteLater();
                }
                
                // 加载静态图片
                if (QFile::exists(pngPath)) {
                    QPixmap pixmap(pngPath);
                    if (!pixmap.isNull()) {
                        label->setPixmap(pixmap.scaled(76, 76, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                        
                    } else {
                        loadDefaultAvatar(label);
                    }
                } else {
                    loadDefaultAvatar(label);
                }
            });
            
        } else if (useNumericIcon) {
            loadStaticImage(label, pngPath);
        } else {
            loadDefaultAvatar(label);
        }
    } else if (useNumericIcon) {
        loadStaticImage(label, pngPath);
    } else {
        loadDefaultAvatar(label);
    }
    
    // 设置透明度效果
    QGraphicsOpacityEffect* opacityEffect = new QGraphicsOpacityEffect();
    opacityEffect->setOpacity(0.0);
    label->setGraphicsEffect(opacityEffect);
    item->opacityEffect = opacityEffect; // 存储到UserImageItem中
    
    // 安装事件过滤器以处理点击事件
    label->installEventFilter(this);
    
    // 淡入动画
    QPropertyAnimation* fadeInAnimation = new QPropertyAnimation(opacityEffect, "opacity");
    fadeInAnimation->setDuration(500);
    fadeInAnimation->setStartValue(0.0);
    fadeInAnimation->setEndValue(0.9);
    fadeInAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    item->fadeAnimation = fadeInAnimation; // 存储到UserImageItem中
    
    return label;
}

void TransparentImageList::loadStaticImage(QLabel* label, const QString& pngPath)
{
    if (QFile::exists(pngPath)) {
        QPixmap pixmap(pngPath);
        if (!pixmap.isNull()) {
            label->setPixmap(pixmap.scaled(76, 76, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            
        } else {
            loadDefaultAvatar(label);
        }
    } else {
        loadDefaultAvatar(label);
    }
}

void TransparentImageList::loadDefaultAvatar(QLabel* label)
{
    // 未知icon时使用红色圆环作为默认头像，而不是依赖文件资源
    // 绘制一个抗锯齿的红色圆环（透明背景），大小与其他头像一致
    const int size = 76;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 圆环参数：红色、较粗的笔划，留出内圈透明
    QPen pen(QColor(220, 20, 60)); // 猩红红
    pen.setWidth(6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // 在边界内绘制圆环，考虑笔宽留出内边距
    const int margin = pen.width();
    QRectF circleRect(margin, margin, size - margin * 2, size - margin * 2);
    painter.drawEllipse(circleRect);

    // 设置到标签
    label->setPixmap(pixmap);
}

void TransparentImageList::removeUserImage(const QString &userId)
{
    if (!m_userImages.contains(userId)) {
        return;
    }
    
    UserImageItem *item = m_userImages[userId];
    
    // 创建淡出动画
    QPropertyAnimation *fadeOut = new QPropertyAnimation(item->opacityEffect, "opacity", this);
    fadeOut->setDuration(300);
    fadeOut->setStartValue(0.8);
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::InOutQuad);
    
    // 动画完成后删除控件
    connect(fadeOut, &QPropertyAnimation::finished, [this, userId, item, fadeOut]() {
        m_mainLayout->removeWidget(item->imageLabel);
        delete item->fadeAnimation;
        delete item->opacityEffect;
        if (item->gifMovie) {
            delete item->gifMovie; // 清理GIF动画对象
        }
        delete item->imageLabel;
        delete item;
        m_userImages.remove(userId);
        fadeOut->deleteLater();
        updateLayout();
    });
    
    fadeOut->start();
}

void TransparentImageList::updateLayout()
{
    int userCount = m_userImages.size();
    
    if (userCount == 0) {
        hide();
        return;
    }
    
    // 计算窗口高度，确保垂直排列
    int totalHeight = userCount * IMAGE_SIZE + (userCount - 1) * IMAGE_SPACING;
    resize(IMAGE_SIZE, totalHeight); // 恢复原始计算方式
    
    
    // 重新定位窗口
    positionOnScreen();
    
    
    // 显示窗口
    show();
}

bool TransparentImageList::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        QLabel *label = qobject_cast<QLabel*>(obj);
        if (label) {
            QString userId = label->property("userId").toString();
            if (!userId.isEmpty()) {
                // 左键：触发视频播放；右键：弹出上下文菜单
                if (mouseEvent->button() == Qt::LeftButton) {
                    onImageClicked(userId);
                    return true; // 消耗左键事件
                } else if (mouseEvent->button() == Qt::RightButton) {
                    QMenu contextMenu(this);
                    QAction *showMainWindowAction = contextMenu.addAction("显示主窗口");
                    connect(showMainWindowAction, &QAction::triggered, this, &TransparentImageList::showMainListRequested);
                    QAction *setAvatarAction = contextMenu.addAction("设置头像");
                    connect(setAvatarAction, &QAction::triggered, this, &TransparentImageList::setAvatarRequested);
                    QAction *systemSettingsAction = contextMenu.addAction("系统设置");
                    connect(systemSettingsAction, &QAction::triggered, this, &TransparentImageList::systemSettingsRequested);
                    // 新增右键菜单项
                    QAction *clearMarksAction = contextMenu.addAction("清理标记");
                    connect(clearMarksAction, &QAction::triggered, this, &TransparentImageList::clearMarksRequested);
                    QAction *exitAction = contextMenu.addAction("退出");
                    connect(exitAction, &QAction::triggered, this, &TransparentImageList::exitRequested);
                    contextMenu.exec(label->mapToGlobal(mouseEvent->pos()));
                    return true; // 消耗右键事件
                } else {
                    return false; // 其他按键，不拦截，交由默认处理
                }
            } else {
            }
        } else {
        }
    }
    return QWidget::eventFilter(obj, event);
}

void TransparentImageList::onImageClicked(const QString &userId)
{
    if (m_userImages.contains(userId)) {
        UserImageItem *item = m_userImages[userId];
        emit userImageClicked(userId, item->userName);
    } else {
    }
}

void TransparentImageList::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    // 透明背景，不需要绘制任何内容
}

void TransparentImageList::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    positionOnScreen();
}

void TransparentImageList::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu contextMenu(this);
    
    QAction *showMainWindowAction = contextMenu.addAction("显示主窗口");
    connect(showMainWindowAction, &QAction::triggered, this, &TransparentImageList::showMainListRequested);
    
    QAction *setAvatarAction = contextMenu.addAction("设置头像");
    connect(setAvatarAction, &QAction::triggered, this, &TransparentImageList::setAvatarRequested);

    QAction *systemSettingsAction = contextMenu.addAction("系统设置");
    connect(systemSettingsAction, &QAction::triggered, this, &TransparentImageList::systemSettingsRequested);
    
    // 新增右键菜单项（窗口泛用菜单）
    QAction *clearMarksAction = contextMenu.addAction("清理标记");
    connect(clearMarksAction, &QAction::triggered, this, &TransparentImageList::clearMarksRequested);
    QAction *exitAction = contextMenu.addAction("退出");
    connect(exitAction, &QAction::triggered, this, &TransparentImageList::exitRequested);
    
    contextMenu.exec(event->globalPos());
}

QString TransparentImageList::getCurrentUserId() const
{
    return m_currentUserId;
}

void TransparentImageList::updateUserAvatar(const QString &userId, int iconId)
{
    
    // 更新用户头像图标ID
    m_userIconIds[userId] = iconId;
    
    // 如果是当前用户，更新当前用户图标ID
    if (userId == m_currentUserId) {
        m_currentUserIconId = iconId;
    }
    
    // 查找并更新对应的用户图像
    if (m_userImages.contains(userId)) {
        UserImageItem* userItem = m_userImages[userId];
        if (userItem && userItem->imageLabel) {
            QLabel* oldLabel = userItem->imageLabel;
            
            // 找到旧标签在布局和列表中的位置
            int labelIndex = m_userLabels.indexOf(oldLabel);
            
            // 重新创建用户图像，替换现有的标签
            QLabel* newLabel = createUserImage(userId, iconId);
            if (newLabel && labelIndex >= 0) {
                // 从布局中移除旧标签
                m_layout->removeWidget(oldLabel);
                
                // 在相同位置插入新标签
                m_layout->insertWidget(labelIndex, newLabel);
                
                // 更新列表中的标签引用
                m_userLabels[labelIndex] = newLabel;
                
                // 更新UserImageItem中的imageLabel
                userItem->imageLabel = newLabel;
                
                // 删除旧标签
                oldLabel->setParent(nullptr);
                oldLabel->deleteLater();
                
            } else {
            }
        } else {
        }
    } else {
    }
}