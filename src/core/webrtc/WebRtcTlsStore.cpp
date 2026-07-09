#include "core/webrtc/WebRtcTlsStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSslCertificate>
#include <QSslKey>
#include <QStandardPaths>

#if defined(PRISM_HAVE_WEBRTC) && !defined(QT_NO_SSL)
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <memory>
#endif

namespace {

#ifdef PRISM_HAVE_WEBRTC
#ifndef QT_NO_SSL

struct TlsMaterial {
    QSslCertificate cert;
    QSslKey         key;
    QStringList     ipAddresses;
    QStringList     dnsNames;
};

TlsMaterial g_material;

QString metaPath() {
    return WebRtcTlsStore::storageDir() + QStringLiteral("/meta.json");
}

QString certPath() {
    return WebRtcTlsStore::storageDir() + QStringLiteral("/cert.pem");
}

QString keyPath() {
    return WebRtcTlsStore::storageDir() + QStringLiteral("/key.pem");
}

bool writePemFile(const QString &path, const QByteArray &pem) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(pem) == pem.size();
}

QByteArray bioToPem(BIO *bio) {
    char *data = nullptr;
    const long len = BIO_get_mem_data(bio, &data);
    if (len <= 0 || !data)
        return {};
    return QByteArray(data, static_cast<int>(len));
}

bool addExtension(X509 *cert, int nid, const char *value) {
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);

    X509_EXTENSION *ext = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value);
    if (!ext)
        return false;

    const int ok = X509_add_ext(cert, ext, -1);
    X509_EXTENSION_free(ext);
    return ok == 1;
}

bool generateSelfSigned(const QString &bindAddress, QByteArray &certPem, QByteArray &keyPem,
                        QString *errorOut) {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(EVP_RSA_gen(2048), EVP_PKEY_free);
    if (!pkey) {
        if (errorOut) *errorOut = QStringLiteral("OpenSSL: failed to generate RSA key");
        return false;
    }

    std::unique_ptr<X509, decltype(&X509_free)> cert(X509_new(), X509_free);
    if (!cert) {
        if (errorOut) *errorOut = QStringLiteral("OpenSSL: failed to allocate certificate");
        return false;
    }

    ASN1_INTEGER_set(X509_get_serialNumber(cert.get()),
                     static_cast<long>(QRandomGenerator::global()->generate()));
    X509_gmtime_adj(X509_get_notBefore(cert.get()), 0);
    X509_gmtime_adj(X509_get_notAfter(cert.get()), 365L * 86400 * 10);

    X509_NAME *subject = X509_get_subject_name(cert.get());
    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("cutwire.org"),
                               -1, -1, 0);
    X509_NAME_add_entry_by_txt(subject, "O", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("CutWire Studios"),
                               -1, -1, 0);
    X509_set_issuer_name(cert.get(), subject);
    X509_set_pubkey(cert.get(), pkey.get());

    const QString san = QStringLiteral("DNS:localhost,DNS:cutwire.org,IP:127.0.0.1,IP:%1")
                            .arg(bindAddress);
    if (!addExtension(cert.get(), NID_subject_alt_name, san.toUtf8().constData())) {
        if (errorOut) *errorOut = QStringLiteral("OpenSSL: failed to add subjectAltName");
        return false;
    }

    if (!addExtension(cert.get(), NID_basic_constraints, const_cast<char *>("critical,CA:FALSE"))) {
        if (errorOut) *errorOut = QStringLiteral("OpenSSL: failed to add basicConstraints");
        return false;
    }

    if (X509_sign(cert.get(), pkey.get(), EVP_sha256()) <= 0) {
        if (errorOut) *errorOut = QStringLiteral("OpenSSL: failed to sign certificate");
        return false;
    }

    std::unique_ptr<BIO, decltype(&BIO_free)> certBio(BIO_new(BIO_s_mem()), BIO_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> keyBio(BIO_new(BIO_s_mem()), BIO_free);
    if (!certBio || !keyBio) {
        if (errorOut) *errorOut = QStringLiteral("OpenSSL: failed to allocate BIO");
        return false;
    }

    if (!PEM_write_bio_X509(certBio.get(), cert.get())
        || !PEM_write_bio_PrivateKey(keyBio.get(), pkey.get(), nullptr, nullptr, 0, nullptr,
                                     nullptr)) {
        if (errorOut) *errorOut = QStringLiteral("OpenSSL: failed to write PEM");
        return false;
    }

    certPem = bioToPem(certBio.get());
    keyPem  = bioToPem(keyBio.get());
    if (certPem.isEmpty() || keyPem.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("OpenSSL: empty PEM output");
        return false;
    }
    return true;
}

bool loadMaterialFromDisk(QString *errorOut) {
    QFile certFile(certPath());
    QFile keyFile(keyPath());
    if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = QStringLiteral("Could not read stored TLS certificate");
        return false;
    }

    const QSslCertificate cert(certFile.readAll(), QSsl::Pem);
    const QSslKey key(keyFile.readAll(), QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
    if (cert.isNull() || key.isNull()) {
        if (errorOut) *errorOut = QStringLiteral("Stored TLS certificate is invalid");
        return false;
    }

    g_material.cert = cert;
    g_material.key  = key;
    g_material.ipAddresses.clear();
    g_material.dnsNames.clear();

    QFile metaFile(metaPath());
    if (metaFile.open(QIODevice::ReadOnly)) {
        const QJsonObject obj = QJsonDocument::fromJson(metaFile.readAll()).object();
        for (const QJsonValue &v : obj.value(QStringLiteral("ips")).toArray())
            g_material.ipAddresses.append(v.toString());
        for (const QJsonValue &v : obj.value(QStringLiteral("dns")).toArray())
            g_material.dnsNames.append(v.toString());
    }
    return true;
}

bool storeMaterial(const QByteArray &certPem, const QByteArray &keyPem, const QString &bindAddress,
                   QString *errorOut) {
    const QString dir = WebRtcTlsStore::storageDir();
    if (!QDir().mkpath(dir)) {
        if (errorOut) *errorOut = QStringLiteral("Could not create TLS storage directory");
        return false;
    }

    if (!writePemFile(certPath(), certPem) || !writePemFile(keyPath(), keyPem)) {
        if (errorOut) *errorOut = QStringLiteral("Could not write TLS certificate files");
        return false;
    }

    QJsonObject meta;
    QJsonArray ips{ QStringLiteral("127.0.0.1"), bindAddress };
    QJsonArray dns{ QStringLiteral("localhost"), QStringLiteral("cutwire.org") };
    meta.insert(QStringLiteral("ips"), ips);
    meta.insert(QStringLiteral("dns"), dns);

    QFile metaFile(metaPath());
    if (!metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || metaFile.write(QJsonDocument(meta).toJson(QJsonDocument::Compact)) <= 0) {
        if (errorOut) *errorOut = QStringLiteral("Could not write TLS metadata");
        return false;
    }

    g_material.cert = QSslCertificate(certPem, QSsl::Pem);
    g_material.key  = QSslKey(keyPem, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
    g_material.ipAddresses = { QStringLiteral("127.0.0.1"), bindAddress };
    g_material.dnsNames    = { QStringLiteral("localhost"), QStringLiteral("cutwire.org") };
    return !g_material.cert.isNull() && !g_material.key.isNull();
}

bool coversAddress(const QString &bindAddress) {
    if (bindAddress.isEmpty())
        return false;
    if (bindAddress == QStringLiteral("127.0.0.1"))
        return true;
    return g_material.ipAddresses.contains(bindAddress);
}

bool hasExpectedDnsNames() {
    return g_material.dnsNames.contains(QStringLiteral("cutwire.org"));
}

#endif // QT_NO_SSL
#endif // PRISM_HAVE_WEBRTC

} // namespace

bool WebRtcTlsStore::isAvailable() {
#ifdef PRISM_HAVE_WEBRTC
#ifndef QT_NO_SSL
    return true;
#else
    return false;
#endif
#else
    return false;
#endif
}

QString WebRtcTlsStore::storageDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return base + QStringLiteral("/webrtc-tls");
}

bool WebRtcTlsStore::ensureCertificate(const QString &bindAddress, QString *errorOut) {
#ifdef PRISM_HAVE_WEBRTC
#ifndef QT_NO_SSL
    if (bindAddress.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("Missing bind address for TLS certificate");
        return false;
    }

    if (QFile::exists(certPath()) && QFile::exists(keyPath())) {
        if (loadMaterialFromDisk(errorOut) && coversAddress(bindAddress) && hasExpectedDnsNames())
            return true;
    }

    QByteArray certPem;
    QByteArray keyPem;
    if (!generateSelfSigned(bindAddress, certPem, keyPem, errorOut))
        return false;
    return storeMaterial(certPem, keyPem, bindAddress, errorOut);
#else
    if (errorOut) *errorOut = QStringLiteral("Qt was built without SSL support");
    return false;
#endif
#else
    Q_UNUSED(bindAddress);
    if (errorOut) *errorOut = QStringLiteral("WebRTC is not enabled in this build");
    return false;
#endif
}

QSslConfiguration WebRtcTlsStore::sslConfiguration() {
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
#ifdef PRISM_HAVE_WEBRTC
#ifndef QT_NO_SSL
    if (!g_material.cert.isNull())
        config.setLocalCertificate(g_material.cert);
    if (!g_material.key.isNull())
        config.setPrivateKey(g_material.key);
    config.setProtocol(QSsl::TlsV1_2OrLater);
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
#endif
#endif
    return config;
}
