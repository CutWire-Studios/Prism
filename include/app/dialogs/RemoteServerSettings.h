#pragma once

#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "core/webrtc/NetworkUtils.h"

/// Backend of the remote control server dialog: bind mode, network interface, start / stop and
/// the QR code for the phone console URL.
class RemoteServerSettings : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool running READ running NOTIFY changed)
    /// Bind to 127.0.0.1 instead of every interface. Locked while the server runs.
    Q_PROPERTY(bool localhostOnly READ localhostOnly WRITE setLocalhostOnly NOTIFY changed)
    Q_PROPERTY(QStringList interfaces READ interfaces NOTIFY changed)
    Q_PROPERTY(int interfaceIndex READ interfaceIndex WRITE setInterfaceIndex NOTIFY changed)
    Q_PROPERTY(int port READ port CONSTANT)
    /// Every address the running server answers on.
    Q_PROPERTY(QStringList accessUrls READ accessUrls NOTIFY changed)
    /// URL encoded in the QR code; empty when stopped or no interface is available.
    Q_PROPERTY(QString qrUrl READ qrUrl NOTIFY changed)
    Q_PROPERTY(QString qrImage READ qrImage NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit RemoteServerSettings(QObject *parent = nullptr);

    bool running() const;
    bool localhostOnly() const { return m_localhostOnly; }
    void setLocalhostOnly(bool on);
    QStringList interfaces() const;
    int interfaceIndex() const { return m_interfaceIndex; }
    void setInterfaceIndex(int index);
    int port() const;
    QStringList accessUrls() const;
    QString qrUrl() const;
    QString qrImage() const;
    QString error() const { return m_error; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void toggle();

signals:
    void changed();

private:
    void startServer(const QHostAddress &addr);

    QVector<NetworkUtils::Ipv4Interface> m_ifaces;
    int m_interfaceIndex = 0;
    bool m_localhostOnly = false;
    QString m_error;
};
