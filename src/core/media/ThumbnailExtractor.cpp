#include "core/media/ThumbnailExtractor.h"
#include "core/media/MediaFormats.h"
#include "core/media/StillImage.h"
#include <QImage>

extern "C" {
#include <libavformat/avformat.h>
}

bool ThumbnailExtractor::isStaticImageFile(const QString &path) {
    return MediaFormats::isImagePath(path);
}

QPixmap ThumbnailExtractor::extract(const QString &filePath, int width, int height) {
    if (isStaticImageFile(filePath)) {
        const QImage img = StillImage::decode(filePath);
        if (img.isNull())
            return {};
        return QPixmap::fromImage(img.scaledToWidth(width, Qt::SmoothTransformation));
    }

    // Seek to ~10% into the file for a more representative frame.
    double at = 0.0;
    AVFormatContext *fmt = nullptr;
    const QByteArray utf8 = filePath.toUtf8();
    if (avformat_open_input(&fmt, utf8.constData(), nullptr, nullptr) == 0) {
        if (avformat_find_stream_info(fmt, nullptr) >= 0 && fmt->duration > 0)
            at = double(fmt->duration) / AV_TIME_BASE * 0.1;
        avformat_close_input(&fmt);
    }

    const QImage frame = StillImage::decodeVideoFrame(filePath, at, width * 2, height * 2);
    if (frame.isNull())
        return {};
    return QPixmap::fromImage(frame.scaled(width, height, Qt::IgnoreAspectRatio,
                                           Qt::SmoothTransformation));
}
