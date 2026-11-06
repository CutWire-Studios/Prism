#include "app/show/VenueProfile.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

QString VenueProfile::resolveDevice(const QString &logical) const
{
    return deviceMap.value(logical, logical);
}

QJsonObject VenueProfile::toJson() const
{
    QJsonObject map;
    for (auto it = deviceMap.cbegin(); it != deviceMap.cend(); ++it)
        map.insert(it.key(), it.value());
    return {
        {QStringLiteral("outputs"), QJsonArray::fromStringList(outputs)},
        {QStringLiteral("resolution"), QJsonObject{{QStringLiteral("w"), width}, {QStringLiteral("h"), height}}},
        {QStringLiteral("scaleMode"), scaleMode},
        {QStringLiteral("deviceMap"), map},
    };
}

VenueProfile VenueProfile::fromJson(const QJsonObject &o)
{
    VenueProfile v;
    for (const QJsonValue &out : o.value(QStringLiteral("outputs")).toArray())
        v.outputs << out.toString();
    const QJsonObject res = o.value(QStringLiteral("resolution")).toObject();
    if (res.value(QStringLiteral("w")).toInt() >= 160 && res.value(QStringLiteral("h")).toInt() >= 90) {
        v.width = res.value(QStringLiteral("w")).toInt();
        v.height = res.value(QStringLiteral("h")).toInt();
    }
    const QString mode = o.value(QStringLiteral("scaleMode")).toString();
    if (mode == QLatin1String("fit") || mode == QLatin1String("crop"))
        v.scaleMode = mode;
    const QJsonObject map = o.value(QStringLiteral("deviceMap")).toObject();
    for (auto it = map.begin(); it != map.end(); ++it)
        v.deviceMap.insert(it.key(), it.value().toString());
    return v;
}

VenueProfile VenueProfile::fromSettings()
{
    QSettings s;
    const QByteArray raw = s.value(QStringLiteral("venue/profile")).toByteArray();
    return raw.isEmpty() ? VenueProfile{} : fromJson(QJsonDocument::fromJson(raw).object());
}

void VenueProfile::saveToSettings() const
{
    QSettings().setValue(QStringLiteral("venue/profile"),
                         QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
}

bool VenueProfile::saveFile(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return true;
}

bool VenueProfile::loadFile(const QString &path, VenueProfile &out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false;
    out = fromJson(doc.object());
    return true;
}
