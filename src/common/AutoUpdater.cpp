#include "AutoUpdater.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

AutoUpdater::AutoUpdater(QObject *parent)
    : QObject(parent), m_reply(nullptr), m_file(nullptr)
{
    m_manager = new QNetworkAccessManager(this);
}

AutoUpdater::~AutoUpdater()
{
    if (m_file) {
        if (m_file->isOpen()) m_file->close();
        delete m_file;
    }
}

void AutoUpdater::checkUpdate(const QString &checkUrl)
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QNetworkRequest request(checkUrl);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork); // 禁用缓存
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy); // 允许重定向
    request.setHeader(QNetworkRequest::UserAgentHeader, "IrulerDeskPro-Updater/1.0"); // 设置 User-Agent
    m_reply = m_manager->get(request);
    
    connect(m_reply, &QNetworkReply::finished, this, &AutoUpdater::onCheckFinished);
}

void AutoUpdater::onCheckFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(QString("检查更新失败: %1").arg(reply->errorString()));
        reply->deleteLater();
        m_reply = nullptr;
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    m_reply = nullptr;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred(QString("解析版本信息失败: %1").arg(parseError.errorString()));
        return;
    }

    if (!doc.isObject()) {
        emit errorOccurred("版本信息格式错误");
        return;
    }

    QJsonObject obj = doc.object();
    QString remoteVersion = obj["version"].toString();
    QString currentVersion = QCoreApplication::applicationVersion();
    
    // 如果没有设置 applicationVersion，默认为 1.0.0
    if (currentVersion.isEmpty()) currentVersion = "1.0.0";
    
    qInfo() << "[Update] Check finished. Local:" << currentVersion << " Remote:" << remoteVersion;

    if (compareVersions(remoteVersion, currentVersion) > 0) {
        m_downloadUrl = obj["url"].toString();
        QString description = obj["description"].toString();
        bool force = obj["force"].toBool();
        emit updateAvailable(remoteVersion, m_downloadUrl, description, force);
    } else {
        emit noUpdateAvailable();
    }
}

void AutoUpdater::downloadAndInstall()
{
    if (m_downloadUrl.isEmpty()) return;

    QString fileName = QFileInfo(m_downloadUrl).fileName();
    if (fileName.isEmpty()) fileName = "update_installer.exe";

    QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_tempFilePath = QDir(tempPath).filePath(fileName);

    // 删除旧文件
    if (QFile::exists(m_tempFilePath)) {
        QFile::remove(m_tempFilePath);
    }

    m_file = new QFile(m_tempFilePath);
    if (!m_file->open(QIODevice::WriteOnly)) {
        emit errorOccurred("无法创建临时文件");
        delete m_file;
        m_file = nullptr;
        return;
    }

    QNetworkRequest request(m_downloadUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, "IrulerDeskPro-Updater/1.0");
    m_reply = m_manager->get(request);

    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_file && m_file->isOpen() && m_reply) {
            m_file->write(m_reply->readAll());
        }
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, &AutoUpdater::onDownloadProgress);
    connect(m_reply, &QNetworkReply::finished, this, &AutoUpdater::onDownloadFinished);
}

void AutoUpdater::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    emit downloadProgress(bytesReceived, bytesTotal);
}

void AutoUpdater::onDownloadFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(QString("下载失败: %1").arg(reply->errorString()));
        m_file->close();
    } else {
        // 写入所有剩余数据
        if (m_file && m_file->isOpen()) {
            m_file->write(reply->readAll());
            m_file->close();
        }
        
        // 启动安装程序
        // 使用 startDetached 确保主程序退出后安装程序继续运行
        bool started = QProcess::startDetached(m_tempFilePath, QStringList());
        if (started) {
            // 退出当前应用
            QCoreApplication::quit();
        } else {
            emit errorOccurred("无法启动安装程序");
        }
    }
    
    reply->deleteLater();
    m_reply = nullptr;
    delete m_file;
    m_file = nullptr;
}

int AutoUpdater::compareVersions(const QString &v1, const QString &v2)
{
    QStringList parts1 = v1.split('.');
    QStringList parts2 = v2.split('.');

    int len = qMax(parts1.length(), parts2.length());

    for (int i = 0; i < len; ++i) {
        int n1 = (i < parts1.length()) ? parts1[i].toInt() : 0;
        int n2 = (i < parts2.length()) ? parts2[i].toInt() : 0;

        if (n1 > n2) return 1;
        if (n1 < n2) return -1;
    }

    return 0;
}
