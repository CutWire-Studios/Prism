#pragma once

#include <QPixmap>
#include <QString>

class ThumbnailExtractor {
public:
    static QPixmap extract(const QString &filePath, int width = 80, int height = 54);
};
