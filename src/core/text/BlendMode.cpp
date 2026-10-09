#include "core/text/BlendMode.h"

namespace prism {

QString blendModeToString(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Normal:
        return QStringLiteral("normal");
    case BlendMode::Multiply:
        return QStringLiteral("multiply");
    case BlendMode::Screen:
        return QStringLiteral("screen");
    case BlendMode::Overlay:
        return QStringLiteral("overlay");
    case BlendMode::Add:
        return QStringLiteral("add");
    case BlendMode::Darken:
        return QStringLiteral("darken");
    case BlendMode::Lighten:
        return QStringLiteral("lighten");
    }
    return QStringLiteral("normal");
}

BlendMode blendModeFromString(const QString &mode)
{
    if (mode == QStringLiteral("multiply"))
        return BlendMode::Multiply;
    if (mode == QStringLiteral("screen"))
        return BlendMode::Screen;
    if (mode == QStringLiteral("overlay"))
        return BlendMode::Overlay;
    if (mode == QStringLiteral("add"))
        return BlendMode::Add;
    if (mode == QStringLiteral("darken"))
        return BlendMode::Darken;
    if (mode == QStringLiteral("lighten"))
        return BlendMode::Lighten;
    return BlendMode::Normal;
}

} // namespace prism
