#include "core/OverlayItem.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QDir>
#include <QFileInfo>
#include <QFile>

// ── OverlayItem ──────────────────────────────────────────────────────────────

QString OverlayItem::displayName() const {
    if (type == Type::Text)
        return "T: " + (content.length() > 22 ? content.left(22) + "…" : content);
    return "I: " + QFileInfo(content).fileName();
}

QJsonObject OverlayItem::toJson() const {
    QJsonObject o;
    o["type"]     = (type == Type::Text) ? "text" : "image";
    o["content"]  = content;
    o["x"]        = static_cast<double>(x);
    o["y"]        = static_cast<double>(y);
    o["w"]        = static_cast<double>(w);
    o["h"]        = static_cast<double>(h);
    o["opacity"]  = static_cast<double>(opacity);
    o["color"]    = color.name(QColor::HexArgb);
    o["fontSize"] = fontSize;
    o["visible"]  = visible;
    return o;
}

OverlayItem OverlayItem::fromJson(const QJsonObject &o) {
    OverlayItem item;
    item.type     = (o["type"].toString() == "image") ? Type::Image : Type::Text;
    item.content  = o["content"].toString();
    item.x        = static_cast<float>(o["x"].toDouble(0.5));
    item.y        = static_cast<float>(o["y"].toDouble(0.8));
    item.w        = static_cast<float>(o["w"].toDouble(0.5));
    item.h        = static_cast<float>(o["h"].toDouble(0.08));
    item.opacity  = static_cast<float>(o["opacity"].toDouble(1.0));
    item.color    = QColor(o["color"].toString("#ffffffff"));
    item.fontSize = o["fontSize"].toInt(28);
    item.visible  = o["visible"].toBool(true);
    return item;
}

// ── ClipSettings ─────────────────────────────────────────────────────────────

QJsonObject ClipSettings::toJson() const {
    QJsonObject o;
    o["startTime"] = startTime;
    o["endTime"]   = endTime;
    o["cropX"]     = static_cast<double>(cropX);
    o["cropY"]     = static_cast<double>(cropY);
    o["cropW"]     = static_cast<double>(cropW);
    o["cropH"]     = static_cast<double>(cropH);
    QJsonArray arr;
    for (const auto &ov : overlays)
        arr.append(ov.toJson());
    o["overlays"] = arr;
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
    for (const auto &v : o["overlays"].toArray())
        s.overlays.append(OverlayItem::fromJson(v.toObject()));
    return s;
}

QString ClipSettings::sidecarPath(const QString &clipPath) {
    QFileInfo fi(clipPath);
    return fi.absolutePath() + "/.switchx/" + fi.fileName() + ".json";
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
