#include "core/ImageSource.h"
#include <QFileInfo>

bool ImageSource::load(const QString &filePath) {
    m_name  = QFileInfo(filePath).fileName();
    m_image = QImage(filePath).convertToFormat(QImage::Format_RGB888);
    return !m_image.isNull();
}

bool ImageSource::isStaticImageFile(const QString &path) {
    const QString l = path.toLower();
    return l.endsWith(".png")  || l.endsWith(".jpg")  || l.endsWith(".jpeg") ||
           l.endsWith(".bmp")  || l.endsWith(".webp") || l.endsWith(".gif");
}
