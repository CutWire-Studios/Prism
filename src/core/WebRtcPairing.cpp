#include "core/WebRtcPairing.h"
#include <QJsonDocument>

namespace WebRtcPairing {

QJsonObject makePayload(const QString &host, quint16 sigPort, const QString &token, quint16 httpPort) {
    QJsonObject obj;
    obj.insert(QStringLiteral("v"), 1);
    obj.insert(QStringLiteral("app"), QStringLiteral("switchx"));
    obj.insert(QStringLiteral("host"), host);
    obj.insert(QStringLiteral("sig"), sigPort);
    obj.insert(QStringLiteral("http"), httpPort);
    obj.insert(QStringLiteral("token"), token);
    return obj;
}

QString toQrUrl(const QJsonObject &payload) {
    const QString host = payload.value(QStringLiteral("host")).toString();
    const quint16 http = static_cast<quint16>(
        payload.value(QStringLiteral("http")).toInt(kDefaultHttpPort));
    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QString b64 = QString::fromUtf8(
        json.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    return QStringLiteral("http://%1:%2/cam?d=%3").arg(host).arg(http).arg(b64);
}

bool decodeQuery(const QUrlQuery &query, QString &token, quint16 &sigPort) {
    const QString d = query.queryItemValue(QStringLiteral("d"));
    if (!d.isEmpty()) {
        const QByteArray json = QByteArray::fromBase64(d.toUtf8(), QByteArray::Base64UrlEncoding);
        if (json.isEmpty()) return false;
        const QJsonObject obj = QJsonDocument::fromJson(json).object();
        if (obj.value(QStringLiteral("app")).toString() != QStringLiteral("switchx"))
            return false;
        token = obj.value(QStringLiteral("token")).toString();
        sigPort = static_cast<quint16>(obj.value(QStringLiteral("sig")).toInt(kDefaultSigPort));
        return !token.isEmpty();
    }

    token = query.queryItemValue(QStringLiteral("s"));
    sigPort = static_cast<quint16>(query.queryItemValue(QStringLiteral("sig")).toUInt());
    if (sigPort == 0)
        sigPort = kDefaultSigPort;
    return !token.isEmpty();
}

} // namespace WebRtcPairing
