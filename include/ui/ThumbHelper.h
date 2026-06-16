#pragma once

#include <QPixmap>
#include <QString>
#include <QColor>
#include "core/SourceDescriptor.h"

/// Static thumbnail-generation helpers extracted from MainWindow.
/// These are pure utility functions with no Qt widget dependencies
/// except QPixmap/QPainter.
class ThumbHelper {
public:
    ThumbHelper() = delete;

    static QPixmap makeIconThumb(const QString &glyph, int w = 110, int h = 65);

    static QPixmap makeCanvasThumb(const QString &label,
                                   SourceDescriptor::CanvasFill fill,
                                   const QColor &color = Qt::white,
                                   int w = 110, int h = 65);

    static QPixmap makeShaderThumb(const QString &code, int w = 110, int h = 65);

    static QPixmap makeHtmlThumb(const QString &html, const QString &filePath,
                                 int w = 110, int h = 65);
};
