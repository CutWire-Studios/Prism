#include "core/media/MediaFormats.h"

#include <QFileInfo>

namespace MediaFormats {

// Containers FFmpeg demuxes. Several of these (mpegts, mpeg) probe rather than
// match on suffix, so FFmpeg's own extension table can't be used here.
const QStringList &videoExtensions() {
    static const QStringList extensions = {
        "mp4", "m4v", "mov", "3gp", "3g2",
        "mkv", "webm",
        "avi", "wmv", "asf", "divx",
        "flv", "f4v",
        "mpg", "mpeg", "m2v", "ts", "m2ts", "mts", "m2t", "vob",
        "ogv", "rm", "rmvb",
        "mxf", "dv", "y4m",
    };
    return extensions;
}

const QStringList &audioExtensions() {
    static const QStringList extensions = {
        "mp3", "wav", "aac", "flac", "ogg", "m4a", "opus", "wma", "aiff", "aif",
    };
    return extensions;
}

// heic/heif/avif (and tiff/webp on kits without qtimageformats) are decoded by
// FFmpeg — see StillImage.h.
const QStringList &imageExtensions() {
    static const QStringList extensions = {
        "png", "jpg", "jpeg", "gif", "webp", "bmp", "tiff", "tif",
        "heic", "heif", "avif",
    };
    return extensions;
}

static QString suffixOf(const QString &path) {
    return QFileInfo(path).suffix().toLower();
}

bool isVideoPath(const QString &path) { return videoExtensions().contains(suffixOf(path)); }
bool isAudioPath(const QString &path) { return audioExtensions().contains(suffixOf(path)); }
bool isImagePath(const QString &path) { return imageExtensions().contains(suffixOf(path)); }

bool isMediaPath(const QString &path) {
    const QString s = suffixOf(path);
    return videoExtensions().contains(s) || imageExtensions().contains(s)
        || audioExtensions().contains(s);
}

QStringList nameFilters(bool video, bool image, bool audio) {
    QStringList out;
    auto add = [&out](const QStringList &exts) {
        for (const QString &e : exts)
            out << QStringLiteral("*.") + e;
    };
    if (video) add(videoExtensions());
    if (image) add(imageExtensions());
    if (audio) add(audioExtensions());
    return out;
}

QString globPattern(bool video, bool image, bool audio) {
    return nameFilters(video, image, audio).join(' ');
}

} // namespace MediaFormats
