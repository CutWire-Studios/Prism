#include "ui/common/ThumbHelper.h"
#include "ui/common/Icons.h"
#include "core/sources/ShaderSource.h"
#include "core/sources/ShapeSource.h"
#include "core/sources/SvgTemplateSource.h"
#include "core/sources/TextSource.h"
#include "ui/common/Theme.h"
#include <QPainter>
#include <QFont>

namespace {
QColor placeholderFill() {
    const QColor &bg = Theme::instance().tokens().bgBase;
    return Theme::instance().isDark() ? bg.lighter(115) : bg.darker(104);
}
}

QPixmap ThumbHelper::makeIconThumb(const QString &symbolName, int w, int h) {
    const auto &t = Theme::instance().tokens();
    QPixmap pix(w, h);
    pix.fill(placeholderFill());
    QPainter p(&pix);
    p.setPen(t.textSecondary);
    Icons::drawCentered(p, pix.rect(), symbolName.toUtf8().constData(), 32,
                                  t.textSecondary);
    return pix;
}

QPixmap ThumbHelper::makeCanvasThumb(const QString &label,
                                     SourceDescriptor::CanvasFill fill,
                                     const QColor &color,
                                     int w, int h) {
    QPixmap pix(w, h);
    if (fill == SourceDescriptor::CanvasFill::Color) {
        pix.fill(color);
        return pix;
    }

    const auto &t = Theme::instance().tokens();
    pix.fill(placeholderFill());
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(t.textSecondary);
    p.setBrush(Qt::NoBrush);
    p.drawRect(8, 8, w - 16, h - 16);
    p.setPen(t.text);
    p.drawText(pix.rect(), Qt::AlignCenter,
               fill == SourceDescriptor::CanvasFill::Transparent ? "TR" : label);
    return pix;
}

QPixmap ThumbHelper::makeShaderThumb(const QString &code, int w, int h) {
    ShaderSource src(code, QSize(w, h));
    if (!src.nextFrame() || !src.isReady())
        return makeIconThumb(Icons::Names::Grain, w, h);
    const uint8_t *data = src.frameData();
    QImage img(data, w, h, w * 3, QImage::Format_RGB888);
    return QPixmap::fromImage(img.copy());
}

QPixmap ThumbHelper::makeTextThumb(const QString &textTemplate, const QColor &color, int w, int h) {
    QPixmap pix(w, h);
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

QPixmap ThumbHelper::makeTextThumb(const SourceDescriptor &desc, int w, int h) {
    return makeTextThumb(desc.textTemplate, TextSource::styleFromDescriptor(desc).primaryColor(), w, h);
}

QPixmap ThumbHelper::makeShapeThumb(const SourceDescriptor &desc, int w, int h) {
    QPixmap pix(w, h);
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

QPixmap ThumbHelper::makeSvgTemplateThumb(const SourceDescriptor &desc, int w, int h) {
    const QImage frame = SvgTemplateSource::renderDescriptor(desc);
    if (frame.isNull())
        return makeIconThumb(Icons::Names::GridView, w, h);
    QPixmap pix(w, h);
    pix.fill(placeholderFill());
    QPainter p(&pix);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QSize fit = frame.size().scaled(w - 8, h - 8, Qt::KeepAspectRatio);
    p.drawImage(QRect(QPoint((w - fit.width()) / 2, (h - fit.height()) / 2), fit), frame);
    return pix;
}
