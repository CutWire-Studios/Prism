#include "core/sources/ShapeSource.h"
#include "core/render/SkiaRender.h"
#include "core/render/SkiaShapePainter.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>
#include <algorithm>

namespace {

QSize canvasFor(const SourceDescriptor &desc) {
    return QSize(desc.canvasWidth > 0 ? desc.canvasWidth : 1280,
                 desc.canvasHeight > 0 ? desc.canvasHeight : 720);
}

QImage renderStyle(const prism::ShapeStyle &style, QSize canvas, double timeSec) {
    const double limit = std::min(canvas.width(), canvas.height()) / 4.0;
    const double inset = std::min(std::ceil(prism::skia::shapeBleedFor(style)), limit);
    const QRectF bounds = QRectF(QPointF(0, 0), QSizeF(canvas)).adjusted(inset, inset, -inset, -inset);
    return prism::renderShape(style, canvas, bounds, timeSec).convertToFormat(QImage::Format_RGBA8888);
}

} // namespace

prism::ShapeStyle ShapeSource::styleFromDescriptor(const SourceDescriptor &desc) {
    return prism::shapeStyleFromJson(QJsonDocument::fromJson(desc.shapeStyleJson.toUtf8()).object());
}

QString ShapeSource::styleToJson(const prism::ShapeStyle &style) {
    return QString::fromUtf8(QJsonDocument(prism::shapeStyleToJson(style)).toJson(QJsonDocument::Compact));
}

QImage ShapeSource::renderDescriptor(const SourceDescriptor &desc, double timeSec) {
    return renderStyle(styleFromDescriptor(desc), canvasFor(desc), timeSec);
}

ShapeSource::ShapeSource(const SourceDescriptor &desc)
    : m_desc(desc)
    , m_style(styleFromDescriptor(desc))
{
    if (!desc.displayName.isEmpty())
        m_displayName = desc.displayName;
    m_timeDriven = m_style.isAnimated() || prism::shapeTimeDrivenPaint(m_style);
    m_clock.start();
    m_image = renderStyle(m_style, canvasFor(m_desc), 0.0);
}

bool ShapeSource::nextFrame() {
    if (!m_timeDriven) {
        const bool first = m_dirty;
        m_dirty = false;
        return first;
    }
    m_image = renderStyle(m_style, canvasFor(m_desc), m_clock.elapsed() / 1000.0);
    return true;
}
