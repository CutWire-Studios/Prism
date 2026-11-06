#pragma once
#include <QString>
#include <QJsonObject>

struct ClipSettings {
    double startTime = 0.0;
    double endTime   = -1.0;

    // Spatial crop: normalized [0,1]. Default = full frame.
    float cropX = 0.0f, cropY = 0.0f;
    float cropW = 1.0f, cropH = 1.0f;

    bool hasCrop() const {
        return !(cropX == 0.f && cropY == 0.f && cropW == 1.f && cropH == 1.f);
    }

    QJsonObject toJson() const;
    static ClipSettings fromJson(const QJsonObject &obj);

    void    saveFor(const QString &clipPath) const;
    static ClipSettings loadFor(const QString &clipPath);

private:
    static QString sidecarPath(const QString &clipPath);
};
