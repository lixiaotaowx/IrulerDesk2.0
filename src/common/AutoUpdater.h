#ifndef AUTOUPDATER_H
#define AUTOUPDATER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>

class AutoUpdater : public QObject
{
    Q_OBJECT
public:
    explicit AutoUpdater(QObject *parent = nullptr);
    ~AutoUpdater();

    // 检查更新
    void checkUpdate(const QString &checkUrl);
    
    // 下载并安装
    void downloadAndInstall();

signals:
    // 发现新版本
    void updateAvailable(const QString &version, const QString &downloadUrl, const QString &description, bool force);
    // 无新版本
    void noUpdateAvailable();
    // 发生错误
    void errorOccurred(const QString &error);
    // 下载进度
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);

private slots:
    void onCheckFinished();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();

private:
    // 版本比较：v1 > v2 返回 1，v1 < v2 返回 -1，相等返回 0
    int compareVersions(const QString &v1, const QString &v2);

    QNetworkAccessManager *m_manager;
    QNetworkReply *m_reply;
    QString m_downloadUrl;
    QString m_tempFilePath;
    QFile *m_file;
};

#endif // AUTOUPDATER_H
