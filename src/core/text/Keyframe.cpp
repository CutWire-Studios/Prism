#include "core/text/Keyframe.h"

#include <QJsonArray>

namespace prism {

QJsonObject keyframesToJson(const KeyframeTrack<double> &track)
{
    QJsonArray keyframes;
    for (auto it = track.keyframes().constBegin(); it != track.keyframes().constEnd(); ++it) {
        const Keyframe<double> &key = it.value();
        QJsonObject object{
            {QStringLiteral("timeUs"), static_cast<double>(it.key())},
            {QStringLiteral("value"), key.value},
        };
        if (!qFuzzyIsNull(key.inDx) || !qFuzzyIsNull(key.inDy) || !qFuzzyIsNull(key.outDx)
            || !qFuzzyIsNull(key.outDy)) {
            object.insert(QStringLiteral("inDx"), key.inDx);
            object.insert(QStringLiteral("inDy"), key.inDy);
            object.insert(QStringLiteral("outDx"), key.outDx);
            object.insert(QStringLiteral("outDy"), key.outDy);
        }
        if (key.corner)
            object.insert(QStringLiteral("corner"), true);
        if (key.hold)
            object.insert(QStringLiteral("hold"), true);
        keyframes.append(object);
    }
    QJsonObject out{{QStringLiteral("keyframes"), keyframes}};
    if (!track.enabled())
        out.insert(QStringLiteral("enabled"), false);
    return out;
}

KeyframeTrack<double> keyframesFromJson(const QJsonObject &object)
{
    KeyframeTrack<double> track;
    track.setEnabled(object.value(QStringLiteral("enabled")).toBool(true));

    for (const QJsonValue &value : object.value(QStringLiteral("keyframes")).toArray()) {
        const QJsonObject keyframe = value.toObject();
        Keyframe<double> key;
        key.value = keyframe.value(QStringLiteral("value")).toDouble(1.0);
        key.inDx = keyframe.value(QStringLiteral("inDx")).toDouble(0.0);
        key.inDy = keyframe.value(QStringLiteral("inDy")).toDouble(0.0);
        key.outDx = keyframe.value(QStringLiteral("outDx")).toDouble(0.0);
        key.outDy = keyframe.value(QStringLiteral("outDy")).toDouble(0.0);
        key.corner = keyframe.value(QStringLiteral("corner")).toBool(false);
        key.hold = keyframe.value(QStringLiteral("hold")).toBool(false);
        track.setKeyframe(static_cast<TimeUs>(keyframe.value(QStringLiteral("timeUs")).toDouble()),
                          key);
    }
    return track;
}

} // namespace prism
