#pragma once

#include <QSize>
#include <QtGlobal>

class SkCanvas;

namespace prism::skia {

// Something that draws itself with Skia onto a layer-sized canvas: a text block, a shape, a
// an SVG document. This header is deliberately Skia-free so Skia's include root and compile
// defines stay out of the rest of the build.
//
// Painters capture everything they need by value, so they can be built on one thread and
// painted on another.
class VectorPainter
{
public:
    virtual ~VectorPainter() = default;

    // Layer target size in device pixels.
    virtual QSize size() const = 0;

    // A stable hash of everything that changes pixels, so static content is drawn once and then
    // served from a GPU-resident cache. 0 means "redraw every frame" — the right answer for
    // anything animated, which would otherwise thrash the cache with one-off entries.
    virtual quint64 cacheKey() const = 0;

    // The canvas is premultiplied RGBA, cleared to transparent, origin at the layer's top-left,
    // one unit per device pixel.
    virtual void paint(SkCanvas &canvas) const = 0;
};

} // namespace prism::skia
