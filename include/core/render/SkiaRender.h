#pragma once

#include "core/text/ShapeStyle.h"
#include "core/text/TextStyle.h"

#include <QByteArray>
#include <QImage>
#include <QRectF>
#include <QSize>
#include <QString>

// CPU entry points for Skia text and shape drawing. No Skia types here; only SkiaRender.cpp and
// the other Skia*.cpp files include Skia headers.

namespace prism {

// Where a text source is in its animation. The In slot plays from totalSec = 0, Loop is driven by
// totalSec, and the Out slot plays towards the end of its window; Hold shows the settled pose.
//   In:   totalSec == phaseElapsedSec while the In slot is running.
//   Hold: totalSec must be at least the In slot's duration (see textAnimationInSeconds).
//   Out:  phaseElapsedSec counts from the start of the Out playout; outWindowSec is how long it
//         runs (<= 0 uses the Out slot's own duration), so the Out slot ends at
//         phaseElapsedSec == outWindowSec.
struct TextAnimClock
{
    enum class Phase { In, Hold, Out };

    Phase phase = Phase::Hold;
    double phaseElapsedSec = 0.0;
    double totalSec = 0.0;
    double outWindowSec = 0.0;
};

// Renders into a canvas-sized Format_RGBA8888_Premultiplied image. The text is laid out in the
// whole canvas (wrap width and alignment follow the style); pixelSize is in canvas pixels.
QImage renderText(const TextStyle &style, const QString &text, QSize canvas, const TextAnimClock &clock);

// Renders a shape filling @p bounds (canvas pixels) into a canvas-sized
// Format_RGBA8888_Premultiplied image. @p timeSec drives time-based paints and keyframes.
QImage renderShape(const ShapeStyle &style, QSize canvas, QRectF bounds, double timeSec);

// Rasterizes an SVG document at its intrinsic size (the width/height attributes, else the viewBox)
// times @p scale into a Format_RGBA8888_Premultiplied image; null when the bytes are not an SVG.
// <text> resolves families through the system font manager. The parsed document is cached by
// content, so re-rendering unchanged bytes skips the parse.
QImage renderSvg(const QByteArray &svg, double scale = 1.0);

// Intrinsic size of the document in SVG user units; empty when it does not parse.
QSizeF svgIntrinsicSize(const QByteArray &svg);

// Length in seconds of the style's In / Out slot for this text and canvas; 0 when the slot is off.
double textAnimationInSeconds(const TextStyle &style, const QString &text, QSize canvas);
double textAnimationOutSeconds(const TextStyle &style, const QString &text, QSize canvas);

// A paint that moves on its own (gradient offsetSpeed, a time-driven shader effect); such a style
// needs a fresh render every frame even when settled.
bool textTimeDrivenPaint(const TextStyle &style);

} // namespace prism
