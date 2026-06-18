#include "core/FirewallUtils.h"
#include <QMessageBox>
#include <QProcess>
#include <QSet>
#include <QWidget>

#ifdef Q_OS_LINUX

namespace FirewallUtils {

namespace {

QSet<quint16> g_openedPorts;
Backend       g_backend = Backend::None;

bool runCommand(const QString &program, const QStringList &args, QString &errorOut, int timeoutMs = 120000) {
    QProcess proc;
    proc.start(program, args);
    if (!proc.waitForFinished(timeoutMs)) {
        errorOut = QObject::tr("Command timed out: %1 %2").arg(program, args.join(QLatin1Char(' ')));
        return false;
    }
    if (proc.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        const QString stdoutText = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        errorOut = stderrText.isEmpty() ? stdoutText : stderrText;
        if (errorOut.isEmpty())
            errorOut = QObject::tr("%1 exited with code %2").arg(program).arg(proc.exitCode());
        return false;
    }
    return true;
}

bool runPrivileged(const QStringList &args, QString &errorOut) {
    return runCommand(QStringLiteral("pkexec"), args, errorOut);
}

bool ufwActive() {
    QString err;
    QProcess proc;
    proc.start(QStringLiteral("ufw"), {QStringLiteral("status")});
    if (!proc.waitForFinished(5000))
        return false;
    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    return out.contains(QStringLiteral("Status: active"));
}

bool firewalldActive() {
    QString err;
    QProcess proc;
    proc.start(QStringLiteral("firewall-cmd"), {QStringLiteral("--state")});
    if (!proc.waitForFinished(5000))
        return false;
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed()
           == QStringLiteral("running");
}

bool openPort(Backend backend, quint16 port, QString &errorOut) {
    const QString portSpec = QStringLiteral("%1/tcp").arg(port);
    switch (backend) {
    case Backend::Ufw:
        return runPrivileged(
            {QStringLiteral("ufw"), QStringLiteral("allow"), portSpec,
             QStringLiteral("comment"), QStringLiteral("SwitchX (temp)")},
            errorOut);
    case Backend::Firewalld:
        // 2-hour temporary hole; refreshed on each pairing request.
        return runPrivileged(
            {QStringLiteral("firewall-cmd"), QStringLiteral("--add-port"), portSpec,
             QStringLiteral("--timeout=7200")},
            errorOut);
    default:
        return true;
    }
}

bool closePort(Backend backend, quint16 port, QString &errorOut) {
    const QString portSpec = QStringLiteral("%1/tcp").arg(port);
    switch (backend) {
    case Backend::Ufw:
        return runPrivileged(
            {QStringLiteral("ufw"), QStringLiteral("delete"), QStringLiteral("allow"), portSpec},
            errorOut);
    case Backend::Firewalld:
        return runPrivileged(
            {QStringLiteral("firewall-cmd"), QStringLiteral("--remove-port"), portSpec},
            errorOut);
    default:
        return true;
    }
}

} // namespace

Status detect() {
    Status status;
    if (ufwActive()) {
        status.backend = Backend::Ufw;
        status.active  = true;
        return status;
    }
    if (firewalldActive()) {
        status.backend = Backend::Firewalld;
        status.active  = true;
    }
    return status;
}

bool ensurePortsOpen(QWidget *parent, const QList<quint16> &tcpPorts, QString &errorOut,
                     const QString &purposeDescription) {
    const Status status = detect();
    if (!status.active)
        return true;

    g_backend = status.backend;
    const QString fwName = status.backend == Backend::Ufw
        ? QObject::tr("UFW")
        : QObject::tr("firewalld");

    QStringList portStrs;
    for (quint16 p : tcpPorts)
        portStrs << QString::number(p);

    const QString purpose = purposeDescription.isEmpty()
        ? QObject::tr("incoming connections")
        : purposeDescription;

    const auto answer = QMessageBox::question(
        parent,
        QObject::tr("Firewall Detected"),
        QObject::tr("%1 is active and may block %2.\n\n"
                      "Allow incoming TCP on port(s) %3 until SwitchX exits?\n"
                      "(Administrator password required)")
            .arg(fwName, purpose, portStrs.join(QStringLiteral(", "))),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);

    if (answer != QMessageBox::Yes)
        return false;

    for (quint16 port : tcpPorts) {
        if (g_openedPorts.contains(port))
            continue;
        if (!openPort(status.backend, port, errorOut))
            return false;
        g_openedPorts.insert(port);
    }
    return true;
}

void releasePorts(const QList<quint16> &tcpPorts) {
    if (g_backend == Backend::None)
        return;

    QString err;
    for (quint16 port : tcpPorts) {
        if (!g_openedPorts.contains(port))
            continue;
        closePort(g_backend, port, err);
        g_openedPorts.remove(port);
    }
    if (g_openedPorts.isEmpty())
        g_backend = Backend::None;
}

void releaseOpenedPorts() {
    if (g_openedPorts.isEmpty() || g_backend == Backend::None)
        return;

    QString err;
    QSet<quint16> ports = g_openedPorts;
    for (quint16 port : ports)
        closePort(g_backend, port, err);
    g_openedPorts.clear();
    g_backend = Backend::None;
}

} // namespace FirewallUtils

#else

namespace FirewallUtils {

Status detect() { return {}; }

bool ensurePortsOpen(QWidget *parent, const QList<quint16> &tcpPorts, QString &errorOut,
                     const QString &purposeDescription) {
    Q_UNUSED(parent);
    Q_UNUSED(tcpPorts);
    Q_UNUSED(errorOut);
    Q_UNUSED(purposeDescription);
    return true;
}

void releasePorts(const QList<quint16> &tcpPorts) {
    Q_UNUSED(tcpPorts);
}

void releaseOpenedPorts() {}

} // namespace FirewallUtils

#endif
