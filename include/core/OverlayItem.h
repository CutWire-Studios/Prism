#pragma once
#include <QString>
#include <QColor>
#include <QList>
#include <QJsonObject>

struct OverlayItem {
    enum class Type { Text, Image };

    Type    type     = Type::Text;
    QString content;               // text string  OR  image file path
    float   x        = 0.5f;      // anchor position, normalized [0,1]
    float   y        = 0.8f;
    float   w        = 0.5f;      // size, normalized [0,1]
    float   h        = 0.08f;
    float   opacity  = 1.0f;
    QColor  color    = Qt::white;
    int     fontSize = 28;
    bool    visible  = true;

    QString displayName() const;
    QJsonObject toJson() const;
    static OverlayItem fromJson(const QJsonObject &obj);
};

struct ClipSettings {
    double startTime = 0.0;
    double endTime   = -1.0;

    // Spatial crop: normalized [0,1]. Default = full frame.
    float cropX = 0.0f, cropY = 0.0f;
    float cropW = 1.0f, cropH = 1.0f;

    QList<OverlayItem> overlays;

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
