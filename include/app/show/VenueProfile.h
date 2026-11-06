#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

/// Where the show is played: program resolution and scaling, which outputs are on, and the map
/// from the logical device names a show uses (audio ports, capture devices) to this venue's
/// hardware. The default lives in QSettings; a package may carry a venue.json that overrides it.
struct VenueProfile {
    static constexpr const char *kFileName = "venue.json";

    QStringList outputs;
    int width = 1280;
    int height = 720;
    QString scaleMode = QStringLiteral("stretch");
    QMap<QString, QString> deviceMap;

    /// The physical device for a logical name; unmapped names stand for themselves.
    QString resolveDevice(const QString &logical) const;

    QJsonObject toJson() const;
    static VenueProfile fromJson(const QJsonObject &o);

    static VenueProfile fromSettings();
    void saveToSettings() const;

    bool saveFile(const QString &path) const;
    /// False when the file is missing or not a JSON object.
    static bool loadFile(const QString &path, VenueProfile &out);
};
