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

QString getOpenPortCmd(Backend backend, quint16 port) {
    const QString portSpec = QStringLiteral("%1/tcp").arg(port);
    if (backend == Backend::Ufw) {
        return QStringLiteral("ufw allow %1 comment 'SwitchX (temp)'").arg(portSpec);
    }
    if (backend == Backend::Firewalld) {
        // 2-hour temporary hole; refreshed on each pairing request.
        return QStringLiteral("firewall-cmd --add-port=%1 --timeout=7200").arg(portSpec);
    }
    return QString();
}

QString getClosePortCmd(Backend backend, quint16 port) {
    const QString portSpec = QStringLiteral("%1/tcp").arg(port);
    if (backend == Backend::Ufw) {
        return QStringLiteral("ufw delete allow %1").arg(portSpec);
    }
    if (backend == Backend::Firewalld) {
        return QStringLiteral("firewall-cmd --remove-port=%1").arg(portSpec);
    }
    return QString();
}

bool runPrivilegedBatch(const QStringList &commands, QString &errorOut) {
    if (commands.isEmpty())
        return true;
    const QString batchScript = commands.join(QStringLiteral(" && "));
    return runCommand(QStringLiteral("pkexec"),
                      {QStringLiteral("sh"), QStringLiteral("-c"), batchScript},
                      errorOut);
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

    QStringList commands;
    QList<quint16> portsToOpen;
    for (quint16 port : tcpPorts) {
        if (g_openedPorts.contains(port))
            continue;
        const QString cmd = getOpenPortCmd(status.backend, port);
        if (!cmd.isEmpty()) {
            commands << cmd;
            portsToOpen << port;
        }
    }

    if (commands.isEmpty())
        return true;

    if (!runPrivilegedBatch(commands, errorOut))
        return false;

    for (quint16 port : portsToOpen)
        g_openedPorts.insert(port);
    return true;
}

void releasePorts(const QList<quint16> &tcpPorts) {
    if (g_backend == Backend::None)
        return;

    QStringList commands;
    QList<quint16> portsToClose;
    for (quint16 port : tcpPorts) {
        if (!g_openedPorts.contains(port))
            continue;
        const QString cmd = getClosePortCmd(g_backend, port);
        if (!cmd.isEmpty()) {
            commands << cmd;
            portsToClose << port;
        }
    }

    if (!commands.isEmpty()) {
        QString err;
        runPrivilegedBatch(commands, err);
    }

    for (quint16 port : portsToClose)
        g_openedPorts.remove(port);
    if (g_openedPorts.isEmpty())
        g_backend = Backend::None;
}

void releaseOpenedPorts() {
    if (g_openedPorts.isEmpty() || g_backend == Backend::None)
        return;

    QStringList commands;
    for (quint16 port : g_openedPorts) {
        const QString cmd = getClosePortCmd(g_backend, port);
        if (!cmd.isEmpty())
            commands << cmd;
    }

    if (!commands.isEmpty()) {
        QString err;
        runPrivilegedBatch(commands, err);
    }

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
