#pragma once

#include <QList>
#include <QString>

namespace FirewallUtils {

enum class Backend { None, Ufw, Firewalld };

struct Status {
    Backend backend = Backend::None;
    bool    active  = false;
};

Status detect();

/// Confirmation text to show before openPorts() for an active firewall (from detect()).
/// purposeDescription is part of the text (e.g. "phone pairing", "remote control access").
QString promptText(const Status &status, const QList<quint16> &tcpPorts,
                   const QString &purposeDescription = QString());

/// Opens the TCP ports through the firewall in @p status via pkexec. The caller has already got
/// the user's consent. Returns false on failure with errorOut set.
bool openPorts(const Status &status, const QList<quint16> &tcpPorts, QString &errorOut);

/// Remove specific temporary rules opened by openPorts (best effort).
void releasePorts(const QList<quint16> &tcpPorts);

/// Remove all temporary rules opened by openPorts (best effort).
void releaseOpenedPorts();

} // namespace FirewallUtils
