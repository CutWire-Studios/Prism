#pragma once

#include <QString>

namespace WebRtcCamPage {

/// Renders the browser phone-camera page. Pass sigPort=0 to use same-origin `/ws` (HTTPS).
/// When @p relayUrl is set, the page connects to the public relay instead.
QString html(const QString &token, quint16 sigPort = 0, const QString &relayUrl = {});

} // namespace WebRtcCamPage
