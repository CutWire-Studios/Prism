#pragma once

#include <QSslConfiguration>
#include <QString>

/// Generates or loads a persistent self-signed TLS certificate for LAN phone pairing.
class WebRtcTlsStore {
public:
    static bool isAvailable();

    /// Ensures a certificate exists, regenerating when bindAddress is not covered.
    static bool ensureCertificate(const QString &bindAddress, QString *errorOut = nullptr);

    static QSslConfiguration sslConfiguration();

    static QString storageDir();

private:
    WebRtcTlsStore() = delete;
};
