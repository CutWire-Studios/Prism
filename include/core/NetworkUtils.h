#pragma once

#include <QString>
#include <QList>

class QWidget;

namespace NetworkUtils {

struct Ipv4Interface {
    QString deviceName;
    QString address;
    QString label;   // e.g. "Wi-Fi (wlan0) — 192.168.1.5"
    QString kind;    // "Wi-Fi", "Ethernet", or human-readable name
};

/// First non-loopback IPv4 address, or 127.0.0.1 if none found.
QString localIpv4();

/// Up, non-loopback interfaces that have an IPv4 address.
QList<Ipv4Interface> listIpv4Interfaces();

/// Pick a sensible default index into listIpv4Interfaces() (Wi-Fi, then Ethernet, else 0).
int defaultInterfaceIndex(const QList<Ipv4Interface> &ifaces);

/// Ask the user to choose an interface; returns empty address if cancelled.
QString promptInterface(QWidget *parent, const QList<Ipv4Interface> &ifaces, int defaultIndex);

} // namespace NetworkUtils
