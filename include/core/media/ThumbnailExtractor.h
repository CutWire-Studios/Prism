#pragma once

#include <QPixmap>
#include <QString>

/// Stateless helpers to produce a small preview QPixmap for a media file
/// (first video frame, or the image itself for static images).
class ThumbnailExtractor {
public:
    static QPixmap extract(const QString &filePath, int width = 80, int height = 54);
    static bool isStaticImageFile(const QString &path);
};
