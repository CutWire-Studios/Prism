#pragma once

#include <QList>
#include <QString>

class QWidget;

namespace FirewallUtils {

enum class Backend { None, Ufw, Firewalld };

struct Status {
    Backend backend = Backend::None;
    bool    active  = false;
};

Status detect();

/// If a host firewall is active, ask the user and open TCP ports via pkexec.
/// purposeDescription is shown in the dialog (e.g. "phone pairing", "remote control access").
/// Returns false on decline or failure.
bool ensurePortsOpen(QWidget *parent, const QList<quint16> &tcpPorts, QString &errorOut,
                     const QString &purposeDescription = QString());

/// Remove specific temporary rules opened by ensurePortsOpen (best effort).
void releasePorts(const QList<quint16> &tcpPorts);

/// Remove all temporary rules opened by ensurePortsOpen (best effort).
void releaseOpenedPorts();

} // namespace FirewallUtils
