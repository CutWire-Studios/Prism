#include "app/common/ThumbHelper.h"
#include "core/sources/ShaderSource.h"
#include "core/sources/ShapeSource.h"
#include "core/sources/SvgTemplateSource.h"
#include "core/sources/TextSource.h"
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QPainter>
#include <QStyleHints>
#include <QSvgRenderer>

namespace {
bool isDark() {
    return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
}

QColor placeholderFill() {
    return isDark() ? QColor("#0C0B11").lighter(115) : QColor("#F3F2F7").darker(104);
}

QColor textColor() { return isDark() ? QColor("#F2F1F6") : QColor("#1B1A22"); }

QColor textSecondaryColor() {
    QColor c = textColor();
    c.setAlphaF(isDark() ? 0.62 : 0.60);
    return c;
}

void drawIconCentered(QPainter &p, const QRectF &rect, const QString &name, int size, const QColor &color) {
    QFile f(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!f.open(QIODevice::ReadOnly))
        return;
    QByteArray svg = f.readAll();
    QByteArray stroke = "stroke=\"" + color.name(QColor::HexRgb).toLatin1() + "\"";
    if (color.alpha() < 255)
        stroke += " stroke-opacity=\"" + QByteArray::number(color.alphaF(), 'f', 3) + "\"";
    svg.replace("stroke=\"currentColor\"", stroke);
    svg.replace("stroke-width=\"2\"", "stroke-width=\"1.75\"");
    QSvgRenderer renderer(svg);
    const QPointF topLeft = rect.center() - QPointF(size / 2.0, size / 2.0);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p, QRectF(topLeft, QSizeF(size, size)));
}
}

QImage ThumbHelper::makeIconThumb(const QString &symbolName, int w, int h) {
    QImage pix(w, h, QImage::Format_ARGB32_Premultiplied);
    pix.fill(placeholderFill());
    QPainter p(&pix);
    drawIconCentered(p, pix.rect(), symbolName, 32, textSecondaryColor());
    return pix;
}

QImage ThumbHelper::makeCanvasThumb(const QString &label,
                                     SourceDescriptor::CanvasFill fill,
                                     const QColor &color,
                                     int w, int h) {
    QImage pix(w, h, QImage::Format_ARGB32_Premultiplied);
    if (fill == SourceDescriptor::CanvasFill::Color) {
        pix.fill(color);
        return pix;
    }

    pix.fill(placeholderFill());
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(textSecondaryColor());
    p.setBrush(Qt::NoBrush);
    p.drawRect(8, 8, w - 16, h - 16);
    p.setPen(textColor());
    p.drawText(pix.rect(), Qt::AlignCenter,
               fill == SourceDescriptor::CanvasFill::Transparent ? "TR" : label);
    return pix;
}

QImage ThumbHelper::makeShaderThumb(const QString &code, int w, int h) {
    ShaderSource src(code, QSize(w, h));
    if (!src.nextFrame() || !src.isReady())
        return makeIconThumb(ThumbHelper::Names::Grain, w, h);
    const uint8_t *data = src.frameData();
    QImage img(data, w, h, w * 3, QImage::Format_RGB888);
    return img.copy();
}

QImage ThumbHelper::makeTextThumb(const QString &textTemplate, const QColor &color, int w, int h) {
    QImage pix(w, h, QImage::Format_ARGB32_Premultiplied);
    pix.fill(placeholderFill());
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(color);
    QFont f;
    f.setPixelSize(11);
    p.setFont(f);
    const QString label = textTemplate.isEmpty() ? QStringLiteral("Text") : textTemplate;
    p.drawText(pix.rect().adjusted(6, 4, -6, -4),
               Qt::AlignCenter | Qt::TextWordWrap,
               label.length() > 40 ? label.left(37) + QStringLiteral("…") : label);
    return pix;
}

QImage ThumbHelper::makeTextThumb(const SourceDescriptor &desc, int w, int h) {
    return makeTextThumb(desc.textTemplate, TextSource::styleFromDescriptor(desc).primaryColor(), w, h);
}

QImage ThumbHelper::makeShapeThumb(const SourceDescriptor &desc, int w, int h) {
    QImage pix(w, h, QImage::Format_ARGB32_Premultiplied);
    pix.fill(placeholderFill());
    SourceDescriptor small = desc;
    small.canvasWidth = w * 4;
    small.canvasHeight = h * 4;
    const QImage shape = ShapeSource::renderDescriptor(small);
    QPainter p(&pix);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(pix.rect(), shape);
    return pix;
}

QImage ThumbHelper::makeSvgTemplateThumb(const SourceDescriptor &desc, int w, int h) {
    const QImage frame = SvgTemplateSource::renderDescriptor(desc);
    if (frame.isNull())
        return makeIconThumb(ThumbHelper::Names::GridView, w, h);
    QImage pix(w, h, QImage::Format_ARGB32_Premultiplied);
    pix.fill(placeholderFill());
    QPainter p(&pix);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QSize fit = frame.size().scaled(w - 8, h - 8, Qt::KeepAspectRatio);
    p.drawImage(QRect(QPoint((w - fit.width()) / 2, (h - fit.height()) / 2), fit), frame);
    return pix;
}
