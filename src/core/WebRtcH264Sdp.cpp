#include "core/WebRtcH264Sdp.h"

#include <QRegularExpression>

namespace WebRtcH264Sdp {

namespace {

QByteArray decodeParameterSet(const QString &encoded) {
    const QString trimmed = encoded.trimmed();
    if (trimmed.isEmpty())
        return {};
    return QByteArray::fromBase64(trimmed.toLatin1());
}

} // namespace

bool parseSpropParameterSets(const QString &sdp, QByteArray *spsOut, QByteArray *ppsOut) {
    if (!spsOut || !ppsOut)
        return false;

    spsOut->clear();
    ppsOut->clear();

    static const QRegularExpression re(
        QStringLiteral("sprop-parameter-sets=([^\\s;\\r\\n]+)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = re.match(sdp);
    if (!match.hasMatch())
        return false;

    const QStringList parts = match.captured(1).split(QLatin1Char(','));
    if (parts.isEmpty())
        return false;

    *spsOut = decodeParameterSet(parts.at(0));
    if (parts.size() > 1)
        *ppsOut = decodeParameterSet(parts.at(1));

    return !spsOut->isEmpty() && !ppsOut->isEmpty();
}

QByteArray buildAvcExtradata(const QByteArray &sps, const QByteArray &pps) {
    if (sps.size() < 4 || pps.isEmpty())
        return {};

    QByteArray out;
    out.reserve(11 + sps.size() + pps.size());
    out.append(char(0x01));
    out.append(sps.at(1));
    out.append(sps.at(2));
    out.append(sps.at(3));
    out.append(char(0xFF)); // 4-byte NAL length size - 1
    out.append(char(0xE1)); // one SPS
    out.append(char((sps.size() >> 8) & 0xFF));
    out.append(char(sps.size() & 0xFF));
    out.append(sps);
    out.append(char(0x01)); // one PPS
    out.append(char((pps.size() >> 8) & 0xFF));
    out.append(char(pps.size() & 0xFF));
    out.append(pps);
    return out;
}

} // namespace WebRtcH264Sdp
