#pragma once

#include <QByteArray>
#include <QString>

namespace WebRtcH264Sdp {

/// Parse base64 SPS/PPS from an SDP offer/answer (`sprop-parameter-sets=`).
bool parseSpropParameterSets(const QString &sdp, QByteArray *spsOut, QByteArray *ppsOut);

/// Build FFmpeg AVCC extradata from raw SPS/PPS NAL payloads (no start codes).
QByteArray buildAvcExtradata(const QByteArray &sps, const QByteArray &pps);

} // namespace WebRtcH264Sdp
