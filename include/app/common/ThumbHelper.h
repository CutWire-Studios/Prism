#pragma once

#include <QImage>
#include <QString>
#include <QColor>
#include "core/sources/SourceDescriptor.h"

/// Static thumbnail-generation helpers extracted from MainWindow.
/// These are pure utility functions with no Qt widget dependencies
/// except QImage/QPainter.
class ThumbHelper {
public:
    ThumbHelper() = delete;

    static QImage makeIconThumb(const QString &symbolName, int w = 110, int h = 65);

    static QImage makeCanvasThumb(const QString &label,
                                   SourceDescriptor::CanvasFill fill,
                                   const QColor &color = Qt::white,
                                   int w = 110, int h = 65);

    static QImage makeShaderThumb(const QString &code, int w = 110, int h = 65);

    static QImage makeTextThumb(const QString &textTemplate,
                                 const QColor &color = Qt::white,
                                 int w = 110, int h = 65);

    /// Text thumbnail tinted with the style's fill colour.
    static QImage makeTextThumb(const SourceDescriptor &desc, int w = 110, int h = 65);

    static QImage makeShapeThumb(const SourceDescriptor &desc, int w = 110, int h = 65);

    static QImage makeSvgTemplateThumb(const SourceDescriptor &desc, int w = 110, int h = 65);
};
