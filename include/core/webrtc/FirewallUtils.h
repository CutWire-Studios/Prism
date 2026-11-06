#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <functional>

namespace FirewallUtils {

enum class Backend { None, Ufw, Firewalld };

struct Status {
    Backend backend = Backend::None;
    bool    active  = false;
};

Status detect();

/// Confirmation text to show before openPortsAsync() for an active firewall (from detect()).
/// purposeDescription is part of the text (e.g. "phone pairing", "remote control access").
QString promptText(const Status &status, const QList<quint16> &tcpPorts,
                   const QString &purposeDescription = QString());

/// Opens the TCP ports through the firewall in @p status via pkexec without blocking. The caller
/// has already got the user's consent. @p done runs on the calling thread's event loop with
/// ok=false and an error text on failure; it is not called if @p context is destroyed first.
void openPortsAsync(const Status &status, const QList<quint16> &tcpPorts, QObject *context,
                    std::function<void(bool ok, QString err)> done);

/// Remove specific temporary rules opened by openPortsAsync (best effort).
void releasePorts(const QList<quint16> &tcpPorts);

/// Remove all temporary rules opened by openPortsAsync (best effort).
void releaseOpenedPorts();

} // namespace FirewallUtils
