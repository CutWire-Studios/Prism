#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrlQuery>

namespace WebRtcPairing {

constexpr quint16 kDefaultHttpPort = 38472;
constexpr quint16 kDefaultSigPort  = 38471;

/// Pairing fields stored inside the base64 `d` query parameter.
QJsonObject makePayload(const QString &host, quint16 sigPort, const QString &token,
                        quint16 httpPort = kDefaultHttpPort);

/// Dual-purpose QR content: a URL that opens the browser test page; app data is in `?d=`.
QString toQrUrl(const QJsonObject &payload);

/// Parse `?d=<base64url(json)>` or legacy `?s=<token>&sig=<port>`.
bool decodeQuery(const QUrlQuery &query, QString &token, quint16 &sigPort);

} // namespace WebRtcPairing
