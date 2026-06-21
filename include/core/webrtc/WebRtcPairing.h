#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrlQuery>

namespace WebRtcPairing {

constexpr quint16 kDefaultHttpPort = 38472;
constexpr quint16 kDefaultSigPort  = 38471;

/// Default public signaling relay (https://roboti.qzz.io).
inline constexpr char kDefaultRelayUrl[] = "wss://roboti.qzz.io/ws";

/// Pairing fields stored inside the base64 `d` query parameter.
/// When @p relayUrl is set (e.g. kDefaultRelayUrl), the phone connects via the public relay.
QJsonObject makePayload(const QString &host, quint16 sigPort, const QString &token,
                        quint16 httpPort = kDefaultHttpPort,
                        const QString &relayUrl = {});

/// Dual-purpose QR content: a URL that opens the browser test page; app data is in `?d=`.
QString toQrUrl(const QJsonObject &payload);

/// Parse `?d=<base64url(json)>` or legacy `?s=<token>&sig=<port>`.
bool decodeQuery(const QUrlQuery &query, QString &token, quint16 &sigPort);

} // namespace WebRtcPairing
