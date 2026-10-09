#pragma once

#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QString>

namespace prism {

struct VectorSlotValue
{
    enum class Type { Color, Scalar, Vec2, Text, Image };

    Type type = Type::Scalar;
    QColor color;
    double scalar = 0.0;
    QPointF vec2;
    QString text;
    QString image;

    static VectorSlotValue fromColor(const QColor &c);
    static VectorSlotValue fromScalar(double v);
    static VectorSlotValue fromVec2(const QPointF &p);
    static VectorSlotValue fromText(const QString &t);
    static VectorSlotValue fromImage(const QString &path);

    QJsonObject toJson() const;
    static VectorSlotValue fromJson(const QJsonObject &o);
    bool operator==(const VectorSlotValue &other) const;
    bool operator!=(const VectorSlotValue &other) const { return !(*this == other); }
};

QString vectorSlotTypeToString(VectorSlotValue::Type type);
VectorSlotValue::Type vectorSlotTypeFromString(const QString &type);

} // namespace prism
