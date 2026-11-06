#pragma once

#include <QImage>
#include <QString>
#include <QColor>
#include "core/sources/SourceDescriptor.h"

/// Static thumbnail-generation helpers built on QImage/QPainter.
class ThumbHelper {
public:
    ThumbHelper() = delete;

    struct Names {
        Names() = delete;
        inline static constexpr const char *DesktopWindows = "monitor";
        inline static constexpr const char *Folder = "folder";
        inline static constexpr const char *Grain = "sparkles";
        inline static constexpr const char *GridView = "layout-grid";
        inline static constexpr const char *Link = "link";
        inline static constexpr const char *PhotoCamera = "camera";
        inline static constexpr const char *SelectWindow = "app-window";
        inline static constexpr const char *Sensors = "radio";
    };

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
