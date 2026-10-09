#pragma once

#include "core/render/VectorPainter.h"

#include <QImage>

namespace prism::skia {

// Paints into a transparent RGBA8888 premultiplied image of painter.size(). Any thread.
QImage rasterize(const VectorPainter &painter);

} // namespace prism::skia
