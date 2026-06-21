#include "core/sources/ImageSource.h"
#include <QFileInfo>

bool ImageSource::load(const QString &filePath) {
    m_name  = QFileInfo(filePath).fileName();
    QImage loaded(filePath);
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
    const QString l = path.toLower();
    return l.endsWith(".png")  || l.endsWith(".jpg")  || l.endsWith(".jpeg") ||
           l.endsWith(".bmp")  || l.endsWith(".webp") || l.endsWith(".gif");
}
