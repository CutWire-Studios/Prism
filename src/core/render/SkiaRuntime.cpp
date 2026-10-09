#include "core/render/SkiaRuntime.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"
#include "include/core/SkSurfaceProps.h"

namespace prism::skia {

QImage rasterize(const VectorPainter &painter)
{
    const QSize size = painter.size();
    if (size.isEmpty())
        return {};

    QImage image(size, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    // Grayscale AA only: LCD coverage assumes an opaque background.
    const SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(
        SkImageInfo::Make(size.width(), size.height(), kRGBA_8888_SkColorType, kPremul_SkAlphaType),
        image.bits(), image.bytesPerLine(), &props);
    if (!surface)
        return {};
    painter.paint(*surface->getCanvas());
    return image;
}

} // namespace prism::skia
