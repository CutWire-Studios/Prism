#pragma once

#include <QPixmap>
#include <QString>
#include <QColor>
#include "core/sources/SourceDescriptor.h"

/// Static thumbnail-generation helpers extracted from MainWindow.
/// These are pure utility functions with no Qt widget dependencies
/// except QPixmap/QPainter.
class ThumbHelper {
public:
    ThumbHelper() = delete;

    static QPixmap makeIconThumb(const QString &symbolName, int w = 110, int h = 65);

    static QPixmap makeCanvasThumb(const QString &label,
                                   SourceDescriptor::CanvasFill fill,
                                   const QColor &color = Qt::white,
                                   int w = 110, int h = 65);

    static QPixmap makeShaderThumb(const QString &code, int w = 110, int h = 65);

    static QPixmap makeTextThumb(const QString &textTemplate,
                                 const QColor &color = Qt::white,
                                 int w = 110, int h = 65);

    /// Text thumbnail tinted with the style's fill colour.
    static QPixmap makeTextThumb(const SourceDescriptor &desc, int w = 110, int h = 65);

    static QPixmap makeShapeThumb(const SourceDescriptor &desc, int w = 110, int h = 65);

    static QPixmap makeSvgTemplateThumb(const SourceDescriptor &desc, int w = 110, int h = 65);
};
