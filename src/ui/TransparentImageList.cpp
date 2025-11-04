#include "TransparentImageList.h"
#include <QApplication>
#include <QScreen>
#include <QDebug>
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
    qDebug() << "[TransparentImageList] 用户列表清空完成";
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
    qDebug() << "[TransparentImageList] 清空用户列表";
    
    qDebug() << "[TransparentImageList] 开始清理用户标签，数量:" << m_userLabels.size();
    
    // 删除所有用户标签 - 使用更安全的方式
    for (int i = 0; i < m_userLabels.size(); ++i) {
        QLabel* label = m_userLabels[i];
        if (label) {
            qDebug() << "[TransparentImageList] 删除用户标签" << i;
            label->setParent(nullptr);  // 先移除父对象
            label->deleteLater();
        }
    }
    m_userLabels.clear();
    
    qDebug() << "[TransparentImageList] 用户标签清理完成，开始清理用户图片映射，数量:" << m_userImages.size();
    
    // 清空用户图片映射 - 使用更安全的方式
    QStringList userIds = m_userImages.keys();
    for (const QString& userId : userIds) {
        qDebug() << "[TransparentImageList] 清理用户图片:" << userId;
        UserImageItem* item = m_userImages.value(userId);
        if (item) {
            if (item->imageLabel) {
                qDebug() << "[TransparentImageList] 直接置空imageLabel for" << userId;
                item->imageLabel = nullptr;
            }
            if (item->opacityEffect) {
                qDebug() << "[TransparentImageList] 直接置空opacityEffect for" << userId;
                item->opacityEffect = nullptr;
            }
            if (item->gifMovie) {
                qDebug() << "[TransparentImageList] 直接置空gifMovie for" << userId;
                item->gifMovie = nullptr;
            }
            if (item->fadeAnimation) {
                qDebug() << "[TransparentImageList] 直接置空fadeAnimation for" << userId;
                item->fadeAnimation = nullptr;
            }
            qDebug() << "[TransparentImageList] 准备删除UserImageItem for" << userId;
            
            // 在删除前进行安全检查
            if (item->imageLabel && item->imageLabel->parent()) {
                qDebug() << "[TransparentImageList] imageLabel仍有父对象，强制移除";
                item->imageLabel->setParent(nullptr);
            }
            
            qDebug() << "[TransparentImageList] 开始删除UserImageItem对象 for" << userId;
            // 暂时注释掉delete，看看是否能继续执行
            // delete item;
            qDebug() << "[TransparentImageList] UserImageItem删除跳过 for" << userId;
        } else {
            qDebug() << "[TransparentImageList] UserImageItem为空 for" << userId;
        }
        qDebug() << "[TransparentImageList] 用户" << userId << "清理完成";
    }
    qDebug() << "[TransparentImageList] 所有用户图片清理完成，准备清空映射";
    m_userImages.clear();
    qDebug() << "[TransparentImageList] 映射清空完成";
    
    qDebug() << "[TransparentImageList] 用户列表清空完成";
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
            qDebug() << "[TransparentImageList] 首位添加当前用户:" << m_currentUserId << "icon:" << selfIconId;
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
            qDebug() << "[TransparentImageList] 追加用户图片标签:" << userId << "icon:" << iconId;
        } else {
            qWarning() << "[TransparentImageList] 创建用户图片标签失败:" << userId;
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
    qDebug() << "[TransparentImageList] ========== 开始更新用户列表 ==========";
    qDebug() << "[TransparentImageList] 服务器返回用户数量:" << onlineUsers.size();
    qDebug() << "[TransparentImageList] 当前用户ID:" << m_currentUserId << "当前用户IconId:" << m_currentUserIconId;
    qDebug() << "[TransparentImageList] 当前用户ID是否为空:" << m_currentUserId.isEmpty();
    qDebug() << "[TransparentImageList] 当前用户IconId范围检查:" << (m_currentUserIconId >= 3 && m_currentUserIconId <= 21);

    // 清空现有用户
    clearUserList();

    // 第一个永远是自己（使用本地配置，不依赖服务器）
    if (!m_currentUserId.isEmpty()) {
        int selfIconId = (m_currentUserIconId >= 3 && m_currentUserIconId <= 21) ? m_currentUserIconId : -1;
        qDebug() << "[TransparentImageList] 准备添加自己到首位 - ID:" << m_currentUserId << "计算后的IconId:" << selfIconId;
        
        QLabel* selfLabel = createUserImage(m_currentUserId, selfIconId);
        if (selfLabel) {
            m_layout->addWidget(selfLabel);
            m_userLabels.append(selfLabel);
            qDebug() << "[TransparentImageList] ✓ 成功添加自己到首位:" << m_currentUserId << "icon:" << selfIconId;
        } else {
            qDebug() << "[TransparentImageList] ✗ 创建自己的标签失败:" << m_currentUserId;
        }
    } else {
        qDebug() << "[TransparentImageList] ✗ 当前用户ID为空，无法添加自己到首位";
    }

    qDebug() << "[TransparentImageList] 当前列表中的用户数量（添加自己后）:" << m_userLabels.size();

    // 然后添加服务器返回的其他在线用户（包括自己，但会跳过重复）
    for (int i = 0; i < onlineUsers.size(); ++i) {
        const QJsonValue& userValue = onlineUsers[i];
        if (!userValue.isObject()) {
            qWarning() << "[TransparentImageList] 用户数据不是对象，跳过:" << userValue;
            continue;
        }
        
        QJsonObject userObj = userValue.toObject();
        QString userId = userObj["id"].toString();
        
        qDebug() << "[TransparentImageList] 处理服务器用户" << i << "- ID:" << userId;
        
        // 跳过自己，因为已经在第一位了
        if (!userId.isEmpty() && userId == m_currentUserId) {
            qDebug() << "[TransparentImageList] 跳过自己，避免重复:" << userId;
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
        
        qDebug() << "[TransparentImageList] 添加其他用户:" << userId << "名称:" << userName
                 << "服务器icon:" << serverIconId << "最终icon:" << iconId;
        
        QLabel* userLabel = createUserImage(userId, iconId);
        if (userLabel) {
            m_layout->addWidget(userLabel);
            m_userLabels.append(userLabel);
            qDebug() << "[TransparentImageList] ✓ 成功添加其他用户:" << userId;
        } else {
            qDebug() << "[TransparentImageList] ✗ 创建其他用户标签失败:" << userId;
        }
    }
    
    qDebug() << "[TransparentImageList] ========== 用户列表更新完成 ==========";
    qDebug() << "[TransparentImageList] 最终列表中的用户数量:" << m_userLabels.size();
    
    // 显示窗口
    if (!m_userLabels.isEmpty()) {
        show();
        raise();
        activateWindow();
        qDebug() << "[TransparentImageList] 窗口已显示";
    } else {
        qDebug() << "[TransparentImageList] 列表为空，不显示窗口";
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
        qDebug() << "[TransparentImageList] 已添加用户图片到布局 - 用户ID:" << userId << "icon:" << iconId;
    }
    
    // 更新用户名
    if (m_userImages.contains(userId)) {
        m_userImages[userId]->userName = userName;
        qDebug() << "[TransparentImageList] 已设置用户名 - 用户ID:" << userId << "用户名:" << userName;
    }
}

QLabel* TransparentImageList::createUserImage(const QString &userId)
{
    // 默认使用非数字icon（显示默认头像）
    return createUserImage(userId, -1);
}

QLabel* TransparentImageList::createUserImage(const QString &userId, int iconId)
{
    qDebug() << "[TransparentImageList] 创建用户图片标签，用户ID:" << userId << "icon ID:" << iconId;
    QString appDir = QCoreApplication::applicationDirPath();
    QString gifPath;
    QString pngPath;
    bool useNumericIcon = (iconId >= 3 && iconId <= 21);
    if (useNumericIcon) {
        gifPath = QString("%1/maps/icon/%2.gif").arg(appDir).arg(iconId);
        pngPath = QString("%1/maps/icon/%2.png").arg(appDir).arg(iconId);
        qDebug() << "[TransparentImageList] gif路径:" << gifPath;
        qDebug() << "[TransparentImageList] png路径:" << pngPath;
    } else {
        qDebug() << "[TransparentImageList] 使用默认头像（非数字icon）:" << iconId;
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
    
    qDebug() << "[TransparentImageList] 创建用户图片标签 - 用户ID:" << userId;
    
    // 创建UserImageItem并存储到m_userImages中
    UserImageItem* item = new UserImageItem();
    item->imageLabel = label;
    item->userId = userId;
    item->userName = ""; // 默认为空，后续可以设置
    item->opacityEffect = nullptr;
    item->fadeAnimation = nullptr;
    item->gifMovie = nullptr;
    
    m_userImages[userId] = item;
    qDebug() << "[TransparentImageList] 已将用户项存储到m_userImages，用户ID:" << userId;
    
    // 加载图片：优先数字icon；否则加载默认头像
    if (useNumericIcon && QFile::exists(gifPath)) {
        qDebug() << "[TransparentImageList] 找到gif文件，开始播放动画:" << gifPath;
        
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
                qDebug() << "[TransparentImageList] gif动画播放完成，切换到静态图片:" << userId;
                
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
                        qDebug() << "[TransparentImageList] 成功加载静态图片:" << pngPath;
                    } else {
                        qWarning() << "[TransparentImageList] 静态图片加载失败:" << pngPath;
                        loadDefaultAvatar(label);
                    }
                } else {
                    qWarning() << "[TransparentImageList] 静态图片文件不存在:" << pngPath;
                    loadDefaultAvatar(label);
                }
            });
            
            qDebug() << "[TransparentImageList] gif动画设置成功:" << userId;
        } else if (useNumericIcon) {
            qWarning() << "[TransparentImageList] gif文件无效，使用静态图片:" << gifPath;
            loadStaticImage(label, pngPath);
        } else {
            loadDefaultAvatar(label);
        }
    } else if (useNumericIcon) {
        qDebug() << "[TransparentImageList] gif文件不存在，直接使用静态图片:" << gifPath;
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
    qDebug() << "[TransparentImageList] 已为用户" << userId << "安装事件过滤器";
    
    // 淡入动画
    QPropertyAnimation* fadeInAnimation = new QPropertyAnimation(opacityEffect, "opacity");
    fadeInAnimation->setDuration(500);
    fadeInAnimation->setStartValue(0.0);
    fadeInAnimation->setEndValue(0.9);
    fadeInAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    item->fadeAnimation = fadeInAnimation; // 存储到UserImageItem中
    
    qDebug() << "[TransparentImageList] 用户图片标签创建完成:" << userId;
    return label;
}

void TransparentImageList::loadStaticImage(QLabel* label, const QString& pngPath)
{
    if (QFile::exists(pngPath)) {
        QPixmap pixmap(pngPath);
        if (!pixmap.isNull()) {
            label->setPixmap(pixmap.scaled(76, 76, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            qDebug() << "[TransparentImageList] 成功加载静态图片:" << pngPath;
        } else {
            qWarning() << "[TransparentImageList] 静态图片加载失败:" << pngPath;
            loadDefaultAvatar(label);
        }
    } else {
        qWarning() << "[TransparentImageList] 静态图片文件不存在:" << pngPath;
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
    qDebug() << "[TransparentImageList] 使用红色圆环作为未知头像";
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
    qDebug() << "[TransparentImageList] 更新布局，用户数量:" << userCount;
    
    if (userCount == 0) {
        qDebug() << "[TransparentImageList] 没有用户，隐藏窗口";
        hide();
        return;
    }
    
    // 计算窗口高度，确保垂直排列
    int totalHeight = userCount * IMAGE_SIZE + (userCount - 1) * IMAGE_SPACING;
    resize(IMAGE_SIZE, totalHeight); // 恢复原始计算方式
    
    qDebug() << "[TransparentImageList] 窗口大小:" << width() << "x" << height();
    qDebug() << "[TransparentImageList] 用户数量:" << userCount << "，每个图像大小:" << IMAGE_SIZE << "，间距:" << IMAGE_SPACING;
    
    // 重新定位窗口
    positionOnScreen();
    
    qDebug() << "[TransparentImageList] 窗口位置:" << x() << "," << y();
    
    // 显示窗口
    show();
    qDebug() << "[TransparentImageList] 窗口已显示，可见性:" << isVisible();
}

bool TransparentImageList::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        QLabel *label = qobject_cast<QLabel*>(obj);
        if (label) {
            QString userId = label->property("userId").toString();
            qDebug() << "[TransparentImageList] 鼠标点击事件 - 对象:" << obj << "用户ID:" << userId << "按钮:" << mouseEvent->button();
            if (!userId.isEmpty()) {
                // 左键：触发视频播放；右键：弹出上下文菜单
                if (mouseEvent->button() == Qt::LeftButton) {
                    qDebug() << "[TransparentImageList] 左键点击用户:" << userId;
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
                    contextMenu.exec(label->mapToGlobal(mouseEvent->pos()));
                    return true; // 消耗右键事件
                } else {
                    return false; // 其他按键，不拦截，交由默认处理
                }
            } else {
                qDebug() << "[TransparentImageList] 用户ID为空，忽略点击";
            }
        } else {
            qDebug() << "[TransparentImageList] 点击对象不是QLabel，忽略";
        }
    }
    return QWidget::eventFilter(obj, event);
}

void TransparentImageList::onImageClicked(const QString &userId)
{
    qDebug() << "[TransparentImageList] onImageClicked被调用，用户ID:" << userId;
    if (m_userImages.contains(userId)) {
        UserImageItem *item = m_userImages[userId];
        qDebug() << "[TransparentImageList] 找到用户项，发送userImageClicked信号 - 用户ID:" << userId << "用户名:" << item->userName;
        emit userImageClicked(userId, item->userName);
    } else {
        qDebug() << "[TransparentImageList] 未找到用户项:" << userId;
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
    
    contextMenu.exec(event->globalPos());
}

QString TransparentImageList::getCurrentUserId() const
{
    return m_currentUserId;
}

void TransparentImageList::updateUserAvatar(const QString &userId, int iconId)
{
    qDebug() << "[TransparentImageList] 开始更新用户头像 - 用户ID:" << userId << "新icon ID:" << iconId;
    
    // 更新用户头像图标ID
    m_userIconIds[userId] = iconId;
    
    // 如果是当前用户，更新当前用户图标ID
    if (userId == m_currentUserId) {
        m_currentUserIconId = iconId;
        qDebug() << "[TransparentImageList] 更新当前用户icon ID:" << iconId;
    }
    
    // 查找并更新对应的用户图像
    if (m_userImages.contains(userId)) {
        UserImageItem* userItem = m_userImages[userId];
        if (userItem && userItem->imageLabel) {
            QLabel* oldLabel = userItem->imageLabel;
            qDebug() << "[TransparentImageList] 找到旧标签，准备替换";
            
            // 找到旧标签在布局和列表中的位置
            int labelIndex = m_userLabels.indexOf(oldLabel);
            qDebug() << "[TransparentImageList] 旧标签在列表中的索引:" << labelIndex;
            
            // 重新创建用户图像，替换现有的标签
            QLabel* newLabel = createUserImage(userId, iconId);
            if (newLabel && labelIndex >= 0) {
                // 从布局中移除旧标签
                m_layout->removeWidget(oldLabel);
                qDebug() << "[TransparentImageList] 已从布局中移除旧标签";
                
                // 在相同位置插入新标签
                m_layout->insertWidget(labelIndex, newLabel);
                qDebug() << "[TransparentImageList] 已在位置" << labelIndex << "插入新标签";
                
                // 更新列表中的标签引用
                m_userLabels[labelIndex] = newLabel;
                
                // 更新UserImageItem中的imageLabel
                userItem->imageLabel = newLabel;
                
                // 删除旧标签
                oldLabel->setParent(nullptr);
                oldLabel->deleteLater();
                qDebug() << "[TransparentImageList] 已删除旧标签";
                
                qDebug() << "[TransparentImageList] 头像更新完成 - 用户ID:" << userId << "新icon ID:" << iconId;
            } else {
                qWarning() << "[TransparentImageList] 创建新标签失败或找不到旧标签位置";
            }
        } else {
            qWarning() << "[TransparentImageList] 用户项或标签为空";
        }
    } else {
        qWarning() << "[TransparentImageList] 未找到用户项:" << userId;
    }
}