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
#include <QDateTime>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <QUrl>
#include <algorithm>

namespace AppConfig {

inline QString applicationName()
{
    return QStringLiteral("irulerdeskpro");
}

inline QString applicationVersion()
{
    return QStringLiteral("1.0.4");
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

inline QStringList localLanBaseUrls()
{
    const int port = lanWsPort();

    struct Cand {
        QString url;
        quint32 ip = 0;
        int score = 0;
        QString iface;
    };

    auto ipInCidr = [](quint32 ip, quint32 net, quint32 mask) -> bool {
        return (ip & mask) == (net & mask);
    };

    auto isReservedTestNet = [](quint32 ip) -> bool {
        if ((ip & 0xFFFF0000u) == 0xC0000200u) return true;
        if ((ip & 0xFFFFFF00u) == 0xC6336400u) return true;
        if ((ip & 0xFFFFFF00u) == 0xCB007100u) return true;
        if ((ip & 0xFFFE0000u) == 0xC6120000u) return true;
        return false;
    };

    auto isCarrierNat = [](quint32 ip) -> bool {
        return (ip & 0xFFC00000u) == 0x64400000u;
    };

    auto ipv4Score = [](quint32 ip) -> int {
        if ((ip & 0xFFFF0000u) == 0xC0A80000u) return 300;
        if ((ip & 0xFF000000u) == 0x0A000000u) return 200;
        if ((ip & 0xFFF00000u) == 0xAC100000u) return 100;
        return 0;
    };

    auto isLinkLocal = [](quint32 ip) -> bool {
        return (ip & 0xFFFF0000u) == 0xA9FE0000u;
    };

    auto isBogusLanIp = [&](quint32 ip) -> bool {
        if (ip == 0) return true;
        if (isLinkLocal(ip)) return true;
        if (isReservedTestNet(ip)) return true;
        if (isCarrierNat(ip)) return true;
        return false;
    };

    auto preferredOutboundIp = [&]() -> quint32 {
        const QString base = wsBaseUrl();
        QUrl u(base);
        QString host = u.host();
        if (host.isEmpty()) {
            host = base;
            host.remove(QStringLiteral("ws://"), Qt::CaseInsensitive);
            host.remove(QStringLiteral("wss://"), Qt::CaseInsensitive);
            const int p = host.indexOf('/');
            if (p >= 0) host = host.left(p);
            const int c = host.indexOf(':');
            if (c >= 0) host = host.left(c);
        }

        QHostAddress remote;
        if (!host.isEmpty()) {
            remote = QHostAddress(host);
        }
        if (remote.isNull() || remote.protocol() != QAbstractSocket::IPv4Protocol) {
            remote = QHostAddress(QStringLiteral("8.8.8.8"));
        }

        QUdpSocket s;
        s.connectToHost(remote, 53);
        const QHostAddress local = s.localAddress();
        if (local.protocol() != QAbstractSocket::IPv4Protocol || local.isNull() || local.isLoopback()) {
            return 0;
        }
        const quint32 ip4 = local.toIPv4Address();
        if (isBogusLanIp(ip4)) {
            return 0;
        }
        return ip4;
    };

    auto isLikelyVirtualSubnet = [](quint32 ip) -> bool {
        return (ip & 0xFFFFFF00u) == 0xC0A83800u;
    };

    auto isBadInterfaceName = [](const QNetworkInterface &iface) -> bool {
        QString n = (iface.humanReadableName() + QStringLiteral(" ") + iface.name()).toLower();
        if (n.contains(QStringLiteral("vmware"))) return true;
        if (n.contains(QStringLiteral("virtualbox"))) return true;
        if (n.contains(QStringLiteral("vbox"))) return true;
        if (n.contains(QStringLiteral("host-only"))) return true;
        if (n.contains(QStringLiteral("host only"))) return true;
        if (n.contains(QStringLiteral("vmnet"))) return true;
        if (n.contains(QStringLiteral("hyper-v"))) return true;
        if (n.contains(QStringLiteral("vethernet"))) return true;
        if (n.contains(QStringLiteral("wsl"))) return true;
        if (n.contains(QStringLiteral("docker"))) return true;
        if (n.contains(QStringLiteral("zerotier"))) return true;
        if (n.contains(QStringLiteral("hamachi"))) return true;
        if (n.contains(QStringLiteral("radmin"))) return true;
        if (n.contains(QStringLiteral("tailscale"))) return true;
        if (n.contains(QStringLiteral("wireguard"))) return true;
        if (n.contains(QStringLiteral("openvpn"))) return true;
        if (n.contains(QStringLiteral("wintun"))) return true;
        if (n.contains(QStringLiteral("tap"))) return true;
        if (n.contains(QStringLiteral("tun"))) return true;
        if (n.contains(QStringLiteral("teredo"))) return true;
        if (n.contains(QStringLiteral("npcap"))) return true;
        return false;
    };

    QVector<Cand> cands;
    const quint32 preferredIp = preferredOutboundIp();
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        if (!iface.isValid()) continue;
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (!(iface.flags() & QNetworkInterface::IsRunning)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if (iface.flags() & QNetworkInterface::IsPointToPoint) continue;
        if (isBadInterfaceName(iface)) continue;

        const auto entries = iface.addressEntries();
        for (const QNetworkAddressEntry &e : entries) {
            const QHostAddress ip = e.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (ip.isNull() || ip.isLoopback()) continue;
            const QString ipStr = ip.toString();
            if (ipStr.isEmpty()) continue;
            const quint32 ip4 = ip.toIPv4Address();
            if (isBogusLanIp(ip4)) continue;
            Cand c;
            c.ip = ip4;
            c.score = ipv4Score(ip4);
            c.iface = iface.humanReadableName();
            if (isLikelyVirtualSubnet(ip4)) {
                c.score -= 250;
            }
            if (preferredIp != 0) {
                const QHostAddress nm = e.netmask();
                quint32 mask = 0xFFFFFF00u;
                if (!nm.isNull() && nm.protocol() == QAbstractSocket::IPv4Protocol) {
                    const quint32 m = nm.toIPv4Address();
                    if (m != 0) mask = m;
                } else if (e.prefixLength() >= 0 && e.prefixLength() <= 32) {
                    const int pl = e.prefixLength();
                    mask = pl == 0 ? 0u : (0xFFFFFFFFu << (32 - pl));
                }
                if (mask != 0 && ipInCidr(ip4, preferredIp, mask)) {
                    c.score += 1000;
                }
            }
            c.url = QStringLiteral("ws://%1:%2").arg(ipStr).arg(port);
            cands.push_back(c);
        }
    }

    std::stable_sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) {
        if (a.score != b.score) return a.score > b.score;
        return a.ip < b.ip;
    });

    QStringList out;
    out.reserve(cands.size());
    for (const Cand &c : cands) {
        out.append(c.url);
    }
    out.removeDuplicates();

    static qint64 lastLogAtMs = 0;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - lastLogAtMs > 60000) {
        lastLogAtMs = nowMs;
        QStringList top;
        for (int i = 0; i < cands.size() && i < 6; ++i) {
            const Cand &c = cands[i];
            top.append(QStringLiteral("%1 score=%2 iface=%3").arg(c.url).arg(c.score).arg(c.iface));
        }
        qInfo().noquote() << "[KickDiag][LanBase] preferred_ip="
                          << (preferredIp == 0 ? QStringLiteral("-") : QHostAddress(preferredIp).toString())
                          << " top=" << top.join(QStringLiteral(" | "));
    }
    return out;
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

#endif
