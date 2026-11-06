#include "core/project/ClipSettings.h"
#include <QJsonDocument>
#include <QDir>
#include <QFileInfo>
#include <QFile>

// ── ClipSettings ─────────────────────────────────────────────────────────────

QJsonObject ClipSettings::toJson() const {
    QJsonObject o;
    o["startTime"] = startTime;
    o["endTime"]   = endTime;
    o["cropX"]     = static_cast<double>(cropX);
    o["cropY"]     = static_cast<double>(cropY);
    o["cropW"]     = static_cast<double>(cropW);
    o["cropH"]     = static_cast<double>(cropH);
    return o;
}

ClipSettings ClipSettings::fromJson(const QJsonObject &o) {
    ClipSettings s;
    s.startTime = o["startTime"].toDouble(0.0);
    s.endTime   = o["endTime"].toDouble(-1.0);
    s.cropX     = static_cast<float>(o["cropX"].toDouble(0.0));
    s.cropY     = static_cast<float>(o["cropY"].toDouble(0.0));
    s.cropW     = static_cast<float>(o["cropW"].toDouble(1.0));
    s.cropH     = static_cast<float>(o["cropH"].toDouble(1.0));
    return s;
}

QString ClipSettings::sidecarPath(const QString &clipPath) {
    QFileInfo fi(clipPath);
    return fi.absolutePath() + "/.prism/" + fi.fileName() + ".json";
}

void ClipSettings::saveFor(const QString &clipPath) const {
    QString path = sidecarPath(clipPath);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
}

ClipSettings ClipSettings::loadFor(const QString &clipPath) {
    QFile f(sidecarPath(clipPath));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return fromJson(doc.object());
}
