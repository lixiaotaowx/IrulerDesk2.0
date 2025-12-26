#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QStringConverter>
#include <QTextStream>
#include <QStandardPaths>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace AppConfig {

inline QString applicationName()
{
    return QStringLiteral("irulerdeskpro");
}

inline QString applicationVersion()
{
    return QStringLiteral("1.0.2");
}

inline QString organizationName()
{
    return QStringLiteral("irulerdeskpro");
}

inline void applyApplicationInfo(QCoreApplication &app)
{
    app.setApplicationName(applicationName());
    app.setApplicationVersion(applicationVersion());
    app.setOrganizationName(organizationName());
}

inline QString defaultServerAddress()
{
    return QStringLiteral("115.159.43.237:8765");
}

inline QString normalizeWsBaseUrl(const QString &value)
{
    const QString v = value.trimmed();
    if (v.startsWith(QStringLiteral("ws://"), Qt::CaseInsensitive) ||
        v.startsWith(QStringLiteral("wss://"), Qt::CaseInsensitive)) {
        return v;
    }
    return QStringLiteral("ws://") + v;
}

inline QString configFilePathInAppDir()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QDir dir(appDir + QStringLiteral("/config"));
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.filePath(QStringLiteral("app_config.txt"));
}

inline QStringList configFileCandidatePaths()
{
    QStringList paths;
    paths << configFilePathInAppDir();
    paths << QDir::currentPath() + QStringLiteral("/config/app_config.txt");
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) {
        paths << appData + QStringLiteral("/config/app_config.txt");
        paths << appData + QStringLiteral("/app_config.txt");
    }
    return paths;
}

inline QString readConfigValue(const QString &key)
{
    const QString prefix = key + QStringLiteral("=");
    const QStringList paths = configFileCandidatePaths();
    for (const QString &path : paths) {
        QFile f(path);
        if (!f.exists()) continue;
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream in(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        in.setEncoding(QStringConverter::Utf8);
#else
        in.setCodec("UTF-8");
#endif
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty()) continue;
            if (line.startsWith(QStringLiteral("#"))) continue;
            if (line.startsWith(prefix)) {
                const QString value = line.mid(prefix.size()).trimmed();
                f.close();
                return value;
            }
        }
        f.close();
    }
    return QString();
}

inline QString serverAddress()
{
    const QString v = readConfigValue(QStringLiteral("server_address"));
    if (!v.isEmpty()) return v;
    return defaultServerAddress();
}

inline QString wsBaseUrl()
{
    return normalizeWsBaseUrl(serverAddress());
}

inline bool lanWsEnabled()
{
    const QString v = readConfigValue(QStringLiteral("lan_ws_enabled")).trimmed();
    if (v.isEmpty()) return true;
    return v.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 || v == QStringLiteral("1");
}

inline int lanWsPort()
{
    const QString v = readConfigValue(QStringLiteral("lan_ws_port")).trimmed();
    bool ok = false;
    int p = v.toInt(&ok);
    if (!ok || p <= 0 || p > 65535) {
        p = 8766;
    }
    return p;
}

inline QString lanWsLoopbackBaseUrl()
{
    return QStringLiteral("ws://127.0.0.1:%1").arg(lanWsPort());
}

inline QMutex &lanBaseUrlMutex()
{
    static QMutex m;
    return m;
}

inline QHash<QString, QStringList> &lanBaseUrlsByTarget()
{
    static QHash<QString, QStringList> map;
    return map;
}

inline void setLanBaseUrlsForTarget(const QString &targetId, const QStringList &baseUrls)
{
    if (targetId.isEmpty() || baseUrls.isEmpty()) return;
    QMutexLocker locker(&lanBaseUrlMutex());
    lanBaseUrlsByTarget().insert(targetId, baseUrls);
}

inline QStringList lanBaseUrlsForTarget(const QString &targetId)
{
    if (targetId.isEmpty()) return {};
    QMutexLocker locker(&lanBaseUrlMutex());
    return lanBaseUrlsByTarget().value(targetId);
}

}

118| #endif
