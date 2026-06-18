#include "core/NetworkUtils.h"
#include <QAbstractSocket>
#include <QHostAddress>
#include <QInputDialog>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QWidget>

namespace NetworkUtils {

namespace {

QString interfaceKind(const QNetworkInterface &iface) {
    const QString name = iface.name().toLower();
    if (name.startsWith(QStringLiteral("wl")) || name.contains(QStringLiteral("wifi")))
        return QStringLiteral("Wi-Fi");
    if (name.startsWith(QStringLiteral("eth")) || name.startsWith(QStringLiteral("en")))
        return QStringLiteral("Ethernet");
    if (!iface.humanReadableName().isEmpty())
        return iface.humanReadableName();
    return iface.name();
}

} // namespace

QString localIpv4() {
    const QList<Ipv4Interface> ifaces = listIpv4Interfaces();
    if (!ifaces.isEmpty())
        return ifaces.first().address;
    return QStringLiteral("127.0.0.1");
}

QList<Ipv4Interface> listIpv4Interfaces() {
    QList<Ipv4Interface> out;
    const QList<QNetworkInterface> all = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : all) {
        if (!(iface.flags() & QNetworkInterface::IsUp))
            continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack)
            continue;

        const QString kind = interfaceKind(iface);
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip == QHostAddress::LocalHost)
                continue;

            Ipv4Interface row;
            row.deviceName = iface.name();
            row.address    = ip.toString();
            row.kind       = kind;
            row.label      = QStringLiteral("%1 (%2) — %3").arg(kind, row.deviceName, row.address);
            out.append(row);
        }
    }
    return out;
}

int defaultInterfaceIndex(const QList<Ipv4Interface> &ifaces) {
    if (ifaces.isEmpty()) return 0;
    for (int i = 0; i < ifaces.size(); ++i) {
        if (ifaces[i].kind == QStringLiteral("Wi-Fi"))
            return i;
    }
    for (int i = 0; i < ifaces.size(); ++i) {
        if (ifaces[i].kind == QStringLiteral("Ethernet"))
            return i;
    }
    return 0;
}

QString promptInterface(QWidget *parent, const QList<Ipv4Interface> &ifaces, int defaultIndex) {
    if (ifaces.isEmpty())
        return {};
    if (ifaces.size() == 1)
        return ifaces.first().address;

    QStringList labels;
    for (const Ipv4Interface &iface : ifaces)
        labels << iface.label;

    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        parent,
        QObject::tr("Network Interface"),
        QObject::tr("Phone must reach SwitchX on this network:"),
        labels,
        defaultIndex,
        false,
        &ok);
    if (!ok || chosen.isEmpty())
        return {};

    const int idx = labels.indexOf(chosen);
    return idx >= 0 ? ifaces[idx].address : QString{};
}

} // namespace NetworkUtils
