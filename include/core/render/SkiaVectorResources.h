#pragma once

#include <QImage>
#include <QString>

#include "include/core/SkImage.h"
#include "modules/skresources/include/SkResources.h"

// Resource loading for SVG documents. Skia's own codecs are built for PNG only, so
// every image goes through QImage (JPEG, WebP, HEIC via the same plugins the rest of Prism uses)
// and is handed back as a raster SkImage.

namespace prism::skia {

sk_sp<SkImage> imageFromQImage(const QImage &image);

// Still image asset for an image slot or an external asset; null when the file does not decode.
sk_sp<skresources::ImageAsset> imageAssetFromFile(const QString &path);

// Resolves a document's external assets: `data:` URIs inline, everything else relative to
// baseDir (the file the document was imported from; empty for inline documents, which can then
// only carry data URIs). Fonts come from systemFontMgr().
sk_sp<skresources::ResourceProvider> makeVectorResourceProvider(const QString &baseDir);

} // namespace prism::skia
