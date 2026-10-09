#include "core/text/VectorSlotValue.h"

#include <QJsonArray>

namespace prism {

QString vectorSlotTypeToString(VectorSlotValue::Type type)
{
    switch (type) {
    case VectorSlotValue::Type::Color:
        return QStringLiteral("color");
    case VectorSlotValue::Type::Scalar:
        return QStringLiteral("scalar");
    case VectorSlotValue::Type::Vec2:
        return QStringLiteral("vec2");
    case VectorSlotValue::Type::Text:
        return QStringLiteral("text");
    case VectorSlotValue::Type::Image:
        return QStringLiteral("image");
    }
    return QStringLiteral("scalar");
}

VectorSlotValue::Type vectorSlotTypeFromString(const QString &type)
{
    if (type == QStringLiteral("color"))
        return VectorSlotValue::Type::Color;
    if (type == QStringLiteral("vec2"))
        return VectorSlotValue::Type::Vec2;
    if (type == QStringLiteral("text"))
        return VectorSlotValue::Type::Text;
    if (type == QStringLiteral("image"))
        return VectorSlotValue::Type::Image;
    return VectorSlotValue::Type::Scalar;
}

VectorSlotValue VectorSlotValue::fromColor(const QColor &c)
{
    VectorSlotValue v;
    v.type = Type::Color;
    v.color = c;
    return v;
}

VectorSlotValue VectorSlotValue::fromScalar(double s)
{
    VectorSlotValue v;
    v.type = Type::Scalar;
    v.scalar = s;
    return v;
}

VectorSlotValue VectorSlotValue::fromVec2(const QPointF &p)
{
    VectorSlotValue v;
    v.type = Type::Vec2;
    v.vec2 = p;
    return v;
}

VectorSlotValue VectorSlotValue::fromText(const QString &t)
{
    VectorSlotValue v;
    v.type = Type::Text;
    v.text = t;
    return v;
}

VectorSlotValue VectorSlotValue::fromImage(const QString &path)
{
    VectorSlotValue v;
    v.type = Type::Image;
    v.image = path;
    return v;
}

QJsonObject VectorSlotValue::toJson() const
{
    QJsonObject o{{QStringLiteral("type"), vectorSlotTypeToString(type)}};
    switch (type) {
    case Type::Color:
        o.insert(QStringLiteral("value"), color.name(QColor::HexArgb));
        break;
    case Type::Scalar:
        o.insert(QStringLiteral("value"), scalar);
        break;
    case Type::Vec2:
        o.insert(QStringLiteral("value"), QJsonArray{vec2.x(), vec2.y()});
        break;
    case Type::Text:
        o.insert(QStringLiteral("value"), text);
        break;
    case Type::Image:
        o.insert(QStringLiteral("value"), image);
        break;
    }
    return o;
}

VectorSlotValue VectorSlotValue::fromJson(const QJsonObject &o)
{
    VectorSlotValue v;
    v.type = vectorSlotTypeFromString(o.value(QStringLiteral("type")).toString());
    const QJsonValue value = o.value(QStringLiteral("value"));
    switch (v.type) {
    case Type::Color:
        v.color = QColor(value.toString());
        break;
    case Type::Scalar:
        v.scalar = value.toDouble();
        break;
    case Type::Vec2: {
        const QJsonArray a = value.toArray();
        v.vec2 = QPointF(a.at(0).toDouble(), a.at(1).toDouble());
        break;
    }
    case Type::Text:
        v.text = value.toString();
        break;
    case Type::Image:
        v.image = value.toString();
        break;
    }
    return v;
}

bool VectorSlotValue::operator==(const VectorSlotValue &other) const
{
    if (type != other.type)
        return false;
    switch (type) {
    case Type::Color:
        return color == other.color;
    case Type::Scalar:
        return scalar == other.scalar;
    case Type::Vec2:
        return vec2 == other.vec2;
    case Type::Text:
        return text == other.text;
    case Type::Image:
        return image == other.image;
    }
    return false;
}


} // namespace prism
