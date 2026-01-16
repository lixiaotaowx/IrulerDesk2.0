#include "NewUiWindow.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidgetItem>
#include <QPushButton>

bool NewUiWindow::isInMyRoomViewerList(const QString &userId) const
{
    if (userId.isEmpty()) {
        return false;
    }
    if (userId == m_myStreamId) {
        return false;
    }

    QString displayName = userId;
    if (QListWidgetItem *it = m_userItems.value(userId, nullptr)) {
        const QString n = it->data(Qt::UserRole + 1).toString();
        if (!n.isEmpty()) {
            displayName = n;
        }
    }

    if (m_viewerList) {
        for (int i = 0; i < m_viewerList->count(); ++i) {
            QListWidgetItem *vit = m_viewerList->item(i);
            if (!vit) {
                continue;
            }
            const QString vid = vit->data(Qt::UserRole).toString();
            if (!vid.isEmpty() && vid == userId) {
                return true;
            }

            QString text;
            if (QWidget *vw = m_viewerList->itemWidget(vit)) {
                const QList<QLabel*> labels = vw->findChildren<QLabel*>();
                if (!labels.isEmpty() && labels.first()) {
                    text = labels.first()->text().trimmed();
                }
            }
            if (text.isEmpty()) {
                text = vit->text().trimmed();
            }
            if (!text.isEmpty()) {
                if (text == userId) {
                    return true;
                }
                if (!displayName.isEmpty() && text == displayName) {
                    return true;
                }
            }
        }
    }

    return m_viewerItems.contains(userId);
}

void NewUiWindow::addViewer(const QString &id, const QString &name)
{
    if (m_viewerItems.contains(id)) {
        QListWidgetItem *existingItem = m_viewerItems.value(id);
        if (m_viewerList && existingItem) {
            QWidget *w = m_viewerList->itemWidget(existingItem);
            if (w) {
                QList<QLabel*> labels = w->findChildren<QLabel*>();
                for (auto label : labels) {
                    label->setText(name.isEmpty() ? id : name);
                    break;
                }
            }
        }
        return;
    }

    if (!m_viewerList) {
        m_viewerItems.insert(id, nullptr);
        const QStringList userIds = m_userItems.keys();
        for (const QString &userId : userIds) {
            updateTalkOverlay(userId);
        }
        updateLocalWatchedOverlay();
        return;
    }

    QString appDir = QCoreApplication::applicationDirPath();

    QListWidgetItem *item = new QListWidgetItem(m_viewerList);
    item->setData(Qt::UserRole, id);
    item->setSizeHint(QSize(180, 50));

    QWidget *w = new QWidget();
    w->setStyleSheet("background: transparent;");
    QHBoxLayout *l = new QHBoxLayout(w);
    l->setContentsMargins(10, 0, 10, 0);

    QLabel *txt = new QLabel(name.isEmpty() ? id : name);
    txt->setStyleSheet("color: #dddddd; font-size: 14px; border: none;");

    QPushButton *removeBtn = new QPushButton();
    removeBtn->setFixedSize(24, 24);
    removeBtn->setCursor(Qt::PointingHandCursor);
    removeBtn->setIcon(QIcon(appDir + "/maps/logo/Remove.png"));
    removeBtn->setIconSize(QSize(18, 18));
    removeBtn->setFlat(true);
    removeBtn->setStyleSheet("border: none; background: transparent;");
    removeBtn->setToolTip("移除");
    removeBtn->installEventFilter(this);

    connect(removeBtn, &QPushButton::clicked, this, [this, id]() {
        // qInfo().noquote() << "[KickDiag] kick button clicked"
        //                   << " viewer_id=" << id
        //                   << " my_id=" << m_myStreamId;
        emit kickViewerRequested(id);
    });

    QPushButton *mic = new QPushButton();
    mic->setFixedSize(24, 24);
    mic->setCursor(Qt::ArrowCursor);
    const bool initialMic = m_viewerMicStates.value(id, false);
    mic->setIcon(QIcon(appDir + "/maps/logo/" + QString(initialMic ? "Mic_on.png" : "Mic_off.png")));
    mic->setIconSize(QSize(18, 18));
    mic->setFlat(true);
    mic->setStyleSheet("border: none; background: transparent;");
    mic->installEventFilter(this);
    mic->setProperty("isOn", initialMic);

    l->addWidget(txt);
    l->addStretch();
    l->addWidget(removeBtn);
    l->addWidget(mic);

    m_viewerList->setItemWidget(item, w);
    m_viewerItems.insert(id, item);
    m_viewerMicButtons.insert(id, mic);
    const QStringList userIds = m_userItems.keys();
    for (const QString &userId : userIds) {
        updateTalkOverlay(userId);
    }
    updateLocalWatchedOverlay();
}

void NewUiWindow::setViewerMicState(const QString &viewerId, bool enabled)
{
    m_viewerMicStates[viewerId] = enabled;
    QPushButton *btn = m_viewerMicButtons.value(viewerId, nullptr);
    if (!btn) return;
    const QString appDir = QCoreApplication::applicationDirPath();
    btn->setProperty("isOn", enabled);
    btn->setIcon(QIcon(appDir + "/maps/logo/" + QString(enabled ? "Mic_on.png" : "Mic_off.png")));
}

void NewUiWindow::updateViewerNameIfExists(const QString &id, const QString &name)
{
    if (m_viewerItems.contains(id)) {
        QListWidgetItem *existingItem = m_viewerItems.value(id);
        if (existingItem) {
            QWidget *w = m_viewerList ? m_viewerList->itemWidget(existingItem) : nullptr;
            if (w) {
                QList<QLabel*> labels = w->findChildren<QLabel*>();
                for (auto label : labels) {
                    label->setText(name.isEmpty() ? id : name);
                    break;
                }
            }
        }
    }

    if (m_userItems.contains(id)) {
        QListWidgetItem *existingItem = m_userItems.value(id);
        if (existingItem && m_listWidget) {
            QWidget *w = m_listWidget->itemWidget(existingItem);
            if (w) {
                QLabel *lbl = w->findChild<QLabel*>("UserNameLabel");
                if (lbl) {
                    lbl->setText(name.isEmpty() ? id : name);
                }
            }
        }
    }
    updateTalkOverlay(id);
}

void NewUiWindow::sendKickToSubscribers(const QString &viewerId)
{
    if (!m_streamClient || !m_streamClient->isConnected()) {
        // qInfo().noquote() << "[KickDiag] kick not sent to room: stream client not connected"
    //                   << " viewer_id=" << viewerId
    //                   << " my_id=" << m_myStreamId;
    //    return;
    }
    QJsonObject msg;
    msg["type"] = "kick_viewer";
    msg["viewer_id"] = viewerId;
    msg["target_id"] = m_myStreamId;
    msg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    QString payload = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    // qint64 bytes = m_streamClient->sendTextMessage(payload);
    // // qInfo().noquote() << "[KickDiag] kick_viewer sent to room"
    // //                   << " bytes=" << bytes
    // //                   << " payload=" << payload;
}

void NewUiWindow::removeViewer(const QString &id)
{
    if (m_viewerItems.contains(id)) {
        QListWidgetItem *item = m_viewerItems.take(id);
        if (m_viewerList && item) {
            int row = m_viewerList->row(item);
            if (row >= 0) {
                m_viewerList->takeItem(row);
            }
            delete item;
        }
    }
    m_viewerMicButtons.remove(id);
    m_viewerMicStates.remove(id);

    if (m_viewerList) {
        for(int i = m_viewerList->count() - 1; i >= 0; --i) {
            QListWidgetItem* item = m_viewerList->item(i);
            if (!item) {
                continue;
            }
            if (item->data(Qt::UserRole).toString() == id) {
                m_viewerList->takeItem(i);
                delete item;
                continue;
            }

            QWidget* w = m_viewerList->itemWidget(item);
            if(w) {
                QList<QLabel*> labels = w->findChildren<QLabel*>();
                for(auto label : labels) {
                    if(label->text() == id) {
                        m_viewerList->takeItem(i);
                        delete item;
                        break;
                    }
                }
            }
        }
    }
    updateTalkOverlay(id);
    updateLocalWatchedOverlay();
}

void NewUiWindow::clearViewers()
{
    const QStringList ids = getViewerIds();

    if (m_viewerList) {
        m_viewerList->clear();
    }
    m_viewerItems.clear();
    m_viewerMicButtons.clear();
    m_viewerMicStates.clear();
    for (const QString &id : ids) {
        updateTalkOverlay(id);
    }
    updateLocalWatchedOverlay();
}

int NewUiWindow::getViewerCount() const
{
    return m_viewerItems.size();
}

QStringList NewUiWindow::getViewerIds() const
{
    return m_viewerItems.keys();
}

void NewUiWindow::updateLocalWatchedOverlay()
{
    if (!m_localWatchedOverlay) {
        return;
    }
    // [Fix] Disable "watched by N" overlay as requested
    m_localWatchedOverlay->setVisible(false);
}

