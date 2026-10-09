#include "core/render/SkiaRender.h"

#include "core/render/SkiaRuntime.h"
#include "core/render/SkiaTextEffects.h"
#include "core/render/SkiaShapePainter.h"
#include "core/render/SkiaTextPainter.h"
#include "core/render/TextLayout.h"
#include "core/text/TextAnimationPreset.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include "include/core/SkSurfaceProps.h"
#include "include/effects/SkImageFilters.h"

#include <QtMath>

#include <algorithm>

namespace prism {

namespace {

constexpr TimeUs kOpenWindowUs = secondsToUs(24.0 * 3600.0);

QImage newCanvas(QSize size)
{
    QImage image(size, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

sk_sp<SkSurface> wrap(QImage &image)
{
    const SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
    return SkSurfaces::WrapPixels(SkImageInfo::Make(image.width(), image.height(), kRGBA_8888_SkColorType,
                                                    kPremul_SkAlphaType),
                                  image.bits(), image.bytesPerLine(), &props);
}


struct ResolvedText
{
    TextStyle style;
    ResolvedTextAnimation anim;
    std::shared_ptr<const text::FragmentSet> set;
};

ResolvedText resolveText(const TextStyle &style, const QString &text, QSize canvas, TimeUs atUs)
{
    ResolvedText r;
    r.style = style.isAnimated() ? style.resolvedAt(atUs) : style;
    r.anim = resolveTextAnimation(r.style.animation);
    r.set = text::fragmentsFor(text, r.style, canvas.width(), canvas.height(), 1.0, -1,
                               text::splitFor(r.anim.resolved, r.style));
    return r;
}

double slotSeconds(const TextAnimationSlot &slot, const QList<TextAnimator> &animators,
                   const text::FragmentSet &set)
{
    return usToSeconds(textanim::slotDurationUs(slot, animators, set.domains));
}

} // namespace

QImage renderText(const TextStyle &style, const QString &text, QSize canvas, const TextAnimClock &clock)
{
    if (canvas.isEmpty())
        return {};
    QImage image = newCanvas(canvas);
    if (text.isEmpty())
        return image;

    const TimeUs totalUs = std::max<TimeUs>(0, secondsToUs(clock.totalSec));
    const ResolvedText r = resolveText(style, text, canvas, totalUs);
    if (!r.set || r.set->frags.isEmpty())
        return image;

    const QRectF layoutRect(QPointF(0, 0), QSizeF(canvas));
    textanim::EvalContext ctx = text::evalContextFor(r.style, layoutRect, 1.0, 0, kOpenWindowUs, totalUs, -1);
    if (clock.phase == TextAnimClock::Phase::Out) {
        const TimeUs outUs = clock.outWindowSec > 0.0
            ? secondsToUs(clock.outWindowSec)
            : textanim::slotDurationUs(r.anim.set.out, r.anim.resolved.out, r.set->domains);
        const TimeUs elapsedUs = std::max<TimeUs>(0, secondsToUs(clock.phaseElapsedSec));
        ctx.windowDurationUs = totalUs + outUs + r.anim.set.out.delayUs - elapsedUs;
    }

    skia::TextPaintRequest request;
    request.style = r.style;
    request.set = r.set;
    request.frame = textanim::evaluateTextAnimation(r.anim.set, r.anim.resolved, r.set->infos, r.set->domains, ctx);
    request.envelope = textanim::animationBounds(r.anim.resolved, ctx);
    request.layoutRect = layoutRect;
    request.renderScale = 1.0;
    request.timeSec = clock.totalSec;
    request.anchorGrouping = r.anim.set.anchorGrouping;
    request.anchorAlignment = r.anim.set.anchorAlignment;

    const skia::TextPainterResult painted = skia::makeTextPainter(request);
    if (!painted.painter)
        return image;

    sk_sp<SkSurface> surface = wrap(image);
    if (!surface)
        return {};
    SkCanvas &sk = *surface->getCanvas();
    const textanim::BlockProps &b = painted.block;

    sk.save();
    const QPointF centre = layoutRect.center();
    sk.translate(centre.x() + b.dx, centre.y() + b.dy);
    sk.rotate(b.rotation);
    sk.scale(b.scale, b.scale);
    sk.translate(-centre.x(), -centre.y());
    sk.translate(painted.rect.x(), painted.rect.y());
    const bool layered = b.opacity < 1.0 || b.blurPx > 0.5;
    if (layered) {
        SkPaint layer;
        layer.setAlphaf(std::clamp(b.opacity, 0.0, 1.0));
        if (b.blurPx > 0.5)
            layer.setImageFilter(SkImageFilters::Blur(b.blurPx, b.blurPx, nullptr));
        sk.saveLayer(nullptr, &layer);
    }
    painted.painter->paint(sk);
    if (layered)
        sk.restore();
    sk.restore();
    return image;
}

QImage renderShape(const ShapeStyle &style, QSize canvas, QRectF bounds, double timeSec)
{
    if (canvas.isEmpty())
        return {};
    QImage image = newCanvas(canvas);

    skia::ShapePaintRequest request;
    request.style = style.isAnimated() ? style.resolvedAt(secondsToUs(timeSec)) : style;
    request.layoutRect = bounds;
    request.renderScale = 1.0;
    request.timeSec = timeSec;
    const skia::ShapePainterResult painted = skia::makeShapePainter(request);
    if (!painted.painter)
        return image;

    sk_sp<SkSurface> surface = wrap(image);
    if (!surface)
        return {};
    SkCanvas &sk = *surface->getCanvas();
    sk.translate(painted.rect.x(), painted.rect.y());
    painted.painter->paint(sk);
    return image;
}

double textAnimationInSeconds(const TextStyle &style, const QString &text, QSize canvas)
{
    const ResolvedText r = resolveText(style, text, canvas, 0);
    if (!r.set)
        return 0.0;
    return slotSeconds(r.anim.set.in, r.anim.resolved.in, *r.set) + usToSeconds(r.anim.set.in.delayUs);
}

double textAnimationOutSeconds(const TextStyle &style, const QString &text, QSize canvas)
{
    const ResolvedText r = resolveText(style, text, canvas, 0);
    if (!r.set)
        return 0.0;
    return slotSeconds(r.anim.set.out, r.anim.resolved.out, *r.set);
}

bool textTimeDrivenPaint(const TextStyle &style)
{
    for (const TextShadingLayer &layer : style.layers) {
        if (!layer.enabled)
            continue;
        if (layer.paint.kind == TextPaintKind::Gradient && !qFuzzyIsNull(layer.paint.gradient.offsetSpeed))
            return true;
        if (layer.paint.kind == TextPaintKind::Effect && skia::textEffectIsAnimated(layer.paint.effect))
            return true;
    }
    return false;
}

} // namespace prism
