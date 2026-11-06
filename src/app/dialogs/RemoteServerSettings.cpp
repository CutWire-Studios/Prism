#include "app/dialogs/RemoteServerSettings.h"

#include "app/AppController.h"
#include "core/webrtc/FirewallUtils.h"
#include "app/common/QrCodeHelper.h"
#include "app/remote/RemoteControlServer.h"

#include <QNetworkInterface>

RemoteServerSettings::RemoteServerSettings(QObject *parent)
    : QObject(parent)
{
    refresh();
}

bool RemoteServerSettings::running() const
{
    auto *app = AppController::instance();
    return app && app->remoteServer() && app->remoteServer()->isRunning();
}

int RemoteServerSettings::port() const { return RemoteControlServer::kPort; }

void RemoteServerSettings::setLocalhostOnly(bool on)
{
    if (on == m_localhostOnly || running())
        return;
    m_localhostOnly = on;
    emit changed();
}

QStringList RemoteServerSettings::interfaces() const
{
    QStringList labels;
    for (const NetworkUtils::Ipv4Interface &iface : m_ifaces)
        labels << iface.label;
    return labels;
}

void RemoteServerSettings::setInterfaceIndex(int index)
{
    if (index == m_interfaceIndex || index < 0 || index >= m_ifaces.size())
        return;
    m_interfaceIndex = index;
    emit changed();
}

void RemoteServerSettings::refresh()
{
    if (!running()) {
        m_ifaces = NetworkUtils::listIpv4Interfaces();
        m_interfaceIndex = m_ifaces.isEmpty() ? 0 : NetworkUtils::defaultInterfaceIndex(m_ifaces);
    }
    emit changed();
}

QStringList RemoteServerSettings::accessUrls() const
{
    QStringList urls;
    auto *app = AppController::instance();
    if (!running())
        return urls;
    const QHostAddress addr = app->remoteServer()->serverAddress();
    const quint16 p = RemoteControlServer::kPort;
    if (addr == QHostAddress::Any) {
        for (const QHostAddress &ip : QNetworkInterface::allAddresses()) {
            if (ip.protocol() == QAbstractSocket::IPv4Protocol && ip != QHostAddress::LocalHost)
                urls << QStringLiteral("http://%1:%2").arg(ip.toString()).arg(p);
        }
        urls << QStringLiteral("http://127.0.0.1:%1").arg(p);
    } else {
        urls << QStringLiteral("http://%1:%2").arg(addr.toString()).arg(p);
    }
    return urls;
}

QString RemoteServerSettings::qrUrl() const
{
    if (!running())
        return {};
    const quint16 p = RemoteControlServer::kPort;
    if (m_localhostOnly)
        return QStringLiteral("http://127.0.0.1:%1/").arg(p);
    if (m_interfaceIndex < 0 || m_interfaceIndex >= m_ifaces.size())
        return {};
    return QStringLiteral("http://%1:%2/").arg(m_ifaces.at(m_interfaceIndex).address).arg(p);
}

QString RemoteServerSettings::qrImage() const
{
    const QString url = qrUrl();
    return url.isEmpty() ? QString() : QrCodeHelper::toDataUrl(url, 5);
}

void RemoteServerSettings::toggle()
{
    auto *app = AppController::instance();
    RemoteControlServer *server = app ? app->remoteServer() : nullptr;
    if (!server)
        return;

    m_error.clear();
    const quint16 p = RemoteControlServer::kPort;
    if (server->isRunning()) {
        server->stopServer();
        refresh();
        return;
    }

    const QHostAddress addr = m_localhostOnly ? QHostAddress::LocalHost : QHostAddress::Any;
    const FirewallUtils::Status fw = addr != QHostAddress::LocalHost ? FirewallUtils::detect()
                                                                      : FirewallUtils::Status{};
    if (!fw.active) {
        startServer(addr);
        return;
    }

    const QString text = FirewallUtils::promptText(fw, {p}, tr("remote control access from your phone"));
    app->requestConfirm(tr("Firewall Detected"), text, [this, fw, addr, p](bool allow) {
        QString fwErr;
        if (!allow || !FirewallUtils::openPorts(fw, {p}, fwErr)) {
            m_error = tr("Could not open the firewall port. %1 Your phone may not be able to connect.")
                          .arg(fwErr.isEmpty() ? tr("Permission denied or cancelled.") : fwErr);
        }
        startServer(addr);
    });
}

void RemoteServerSettings::startServer(const QHostAddress &addr)
{
    auto *app = AppController::instance();
    RemoteControlServer *server = app ? app->remoteServer() : nullptr;
    if (!server)
        return;
    const quint16 p = RemoteControlServer::kPort;
    if (!server->startServer(addr, p)) {
        FirewallUtils::releasePorts({p});
        m_error = tr("Could not start the remote control server on port %1. "
                     "Check whether another application is using the port.").arg(p);
    }
    refresh();
}
