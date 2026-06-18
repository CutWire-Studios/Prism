#pragma once

#include <QString>

namespace WebRtcCamPage {

/// Renders the browser phone-camera page. Pass sigPort=0 to use same-origin `/ws` (HTTPS).
QString html(const QString &token, quint16 sigPort = 0);

} // namespace WebRtcCamPage
