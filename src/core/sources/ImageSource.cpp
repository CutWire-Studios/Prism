#include "core/sources/ImageSource.h"
#include "core/media/MediaFormats.h"
#include "core/media/StillImage.h"
#include <QFileInfo>

bool ImageSource::load(const QString &filePath) {
    m_name  = QFileInfo(filePath).fileName();
    QImage loaded = StillImage::decode(filePath);
    if (loaded.isNull()) return false;

    m_image = loaded.convertToFormat(QImage::Format_RGBA8888);
    return !m_image.isNull();
}

bool ImageSource::setImage(QImage image, const QString &displayName) {
    if (image.isNull()) return false;
    m_image = image.convertToFormat(QImage::Format_RGBA8888);
    m_name  = displayName.isEmpty() ? QStringLiteral("Frozen Frame") : displayName;
    return true;
}

bool ImageSource::isStaticImageFile(const QString &path) {
    return MediaFormats::isImagePath(path);
}
