#pragma once

#include "core/sources/MediaSource.h"
#include "core/sources/SourceDescriptor.h"
#include "core/text/ShapeStyle.h"
#include <QElapsedTimer>
#include <QImage>

/// Renders a prism::ShapeStyle into a canvas-sized RGBA frame. The shape fills the canvas inset by
/// the style's stroke/shadow bleed; it only re-renders while the style is keyframed or time-driven.
class ShapeSource : public MediaSource {
public:
    explicit ShapeSource(const SourceDescriptor &desc);

    /// Parses / serializes SourceDescriptor::shapeStyleJson (empty or invalid JSON gives the default shape).
    static prism::ShapeStyle styleFromDescriptor(const SourceDescriptor &desc);
    static QString styleToJson(const prism::ShapeStyle &style);

    /// Renders the style of @p desc at @p timeSec. Shared with thumbnails.
    static QImage renderDescriptor(const SourceDescriptor &desc, double timeSec = 0.0);

    Type type() const override { return Type::Shape; }
    bool isReady() const override { return !m_image.isNull(); }
    QSize frameSize() const override { return m_image.size(); }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_image.constBits());
    }
    bool nextFrame() override;
    QString displayName() const override { return m_displayName; }
    bool hasAlpha() const override { return true; }

private:
    SourceDescriptor m_desc;
    prism::ShapeStyle m_style;
    QImage m_image;
    QString m_displayName = QStringLiteral("Shape");
    QElapsedTimer m_clock;
    bool m_timeDriven = false;
    bool m_dirty = true;
};
