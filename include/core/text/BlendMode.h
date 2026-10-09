#pragma once

#include <QString>

namespace prism {

enum class BlendMode { Normal, Multiply, Screen, Overlay, Add, Darken, Lighten };

QString blendModeToString(BlendMode mode);
BlendMode blendModeFromString(const QString &mode);

} // namespace prism
