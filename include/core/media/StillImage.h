#pragma once

#include <QImage>
#include <QString>

// One decode path for still images and single video frames.
//
// QImageReader is tried first (with EXIF auto-rotation). FFmpeg is the fallback:
// HEIC/AVIF have no Qt plugin in the official kits, and a kit built without
// qtimageformats loses webp and tiff. libavcodec already carries those decoders.
namespace StillImage {

// Decoded image, or a null QImage. maxWidth/maxHeight bound the decode (fit,
// never upscale); 0 means unbounded. Orientation is applied.
QImage decode(const QString &path, int maxWidth = 0, int maxHeight = 0);

// One frame of a video (or any FFmpeg-readable file) at `seconds`, with the
// stream's display-matrix rotation applied. Returned as RGBA8888.
QImage decodeVideoFrame(const QString &path, double seconds,
                        int maxWidth = 0, int maxHeight = 0);

} // namespace StillImage
