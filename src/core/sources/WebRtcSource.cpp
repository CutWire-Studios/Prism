#include "core/sources/WebRtcSource.h"
#include "core/webrtc/WebRtcManager.h"

WebRtcSource::WebRtcSource(const QString &sessionToken)
    : m_token(sessionToken)
{
#ifndef SWITCHX_HAVE_WEBRTC
    Q_UNUSED(sessionToken);
#else
    if (!sessionToken.isEmpty())
        WebRtcManager::instance().registerViewer(sessionToken);
#endif
}

WebRtcSource::~WebRtcSource() {
#ifdef SWITCHX_HAVE_WEBRTC
    if (!m_token.isEmpty())
        WebRtcManager::instance().unregisterViewer(m_token);
#endif
}

bool WebRtcSource::isAvailable() {
#ifdef SWITCHX_HAVE_WEBRTC
    return WebRtcManager::isAvailable();
#else
    return false;
#endif
}

bool WebRtcSource::nextFrame() {
#ifdef SWITCHX_HAVE_WEBRTC
    if (m_token.isEmpty()) return false;
    QImage img;
    uint64_t seq = 0;
    if (!WebRtcManager::instance().copyLatestFrame(m_token, img, seq, m_lastSeq))
        return false;
    m_frame = std::move(img);
    m_lastSeq = seq;
    return true;
#else
    return false;
#endif
}
