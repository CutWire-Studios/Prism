#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantList>
#include <QVector>

/// Minimal OSC 1.0 codec. Decodes messages and bundles (timetags ignored),
/// encodes messages with int32, float32 and string arguments.
namespace OscProtocol {

struct OscMessage {
    QString      address;
    QVariantList args;
};

/// Arg types i, f, d, s, h, T, F, N. Malformed packets decode to an empty list.
QVector<OscMessage> decode(const QByteArray &packet);

/// int/bool → i, float/double → f, anything else → s.
QByteArray encode(const OscMessage &message);

} // namespace OscProtocol
