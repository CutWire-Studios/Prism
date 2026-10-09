#pragma once

#include "core/render/TextLayout.h"
#include "core/render/VectorPainter.h"
#include "core/text/TextAnimator.h"

#include <QList>
#include <QRectF>
#include <QString>

#include <memory>

// The Skia text painter. Layout is TextLayout's (QTextLayout pieces, bleed and cache keys); the
// animator engine's frame says where every fragment sits at this instant; the shading stack says
// how each is painted. Skia-free header.

namespace prism::skia {

struct TextPainterResult
{
    std::shared_ptr<const VectorPainter> painter; // null when there is nothing to draw
    QRectF rect;                                  // destination in canvas px, bleed included
    // Whole-block motion the painter leaves to its host: the compositor puts it on the GPU
    // layer, a CPU card has to apply it itself.
    textanim::BlockProps block;
};

struct TextPaintRequest
{
    std::shared_ptr<const text::FragmentSet> set;
    textanim::Frame frame;
    TextStyle style; // already resolvedAt() the instant
    QRectF layoutRect;
    double renderScale = 1.0;
    double timeSec = 0.0; // since the window start; drives time-based paints
    textanim::Bounds envelope;
    TextAnchorGrouping anchorGrouping = TextAnchorGrouping::Character;
    QPointF anchorAlignment;
};

TextPainterResult makeTextPainter(const TextPaintRequest &request);

void clearTextGeometryCache();

} // namespace prism::skia
