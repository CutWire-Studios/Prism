#include "app/editors/StyleLayers.h"

#include "core/text/BlendMode.h"
#include "core/text/TextShading.h"
#include "core/text/TextStyle.h"

#include <QJsonValue>
#include <algorithm>

using namespace prism;

namespace {

QString paintLabel(TextPaintKind kind)
{
    switch (kind) {
    case TextPaintKind::Solid: return StyleLayers::tr("Solid");
    case TextPaintKind::Gradient: return StyleLayers::tr("Gradient");
    case TextPaintKind::Texture: return StyleLayers::tr("Texture");
    case TextPaintKind::Effect: return StyleLayers::tr("Effect");
    }
    return {};
}

QString layerText(const TextShadingLayer &l)
{
    QString text = shadingLayerKindLabel(l.kind);
    if (l.kind == TextLayerKind::Fill || l.kind == TextLayerKind::Stroke)
        text += QStringLiteral(" · ") + paintLabel(l.paint.kind);
    return text;
}

int decimalsFor(double step)
{
    int d = 0;
    while (step < 0.999 && d < 4) {
        step *= 10;
        ++d;
    }
    return d;
}

TextShaderEffect effectWithDefaults(const TextEffectSpec &spec)
{
    TextShaderEffect e;
    e.id = spec.id;
    for (const TextAnimParamSpec &p : spec.params)
        e.params.insert(p.id, p.defaultValue);
    return e;
}

QVariantList toVariantList(const QList<TextShadingLayer> &layers)
{
    return StyleLayers::normalizedLayers(layers).toVariantList();
}

QList<TextShadingLayer> fromVariantList(const QVariantList &layers)
{
    return StyleLayers::parseLayers(QJsonArray::fromVariantList(layers));
}

} // namespace

StyleLayers::StyleLayers(QObject *parent)
    : QObject(parent)
{
}

QJsonObject StyleLayers::mergePatch(QJsonObject base, const QJsonObject &patch)
{
    for (auto it = patch.constBegin(); it != patch.constEnd(); ++it) {
        const QJsonValue current = base.value(it.key());
        if (it->isObject() && current.isObject())
            base.insert(it.key(), mergePatch(current.toObject(), it->toObject()));
        else
            base.insert(it.key(), *it);
    }
    return base;
}

QJsonObject StyleLayers::normalizedLayer(const TextShadingLayer &l)
{
    QJsonObject o = textShadingLayerToJson(l);
    o.insert(QStringLiteral("strokeAlign"), strokeAlignToString(l.strokeAlign));
    o.insert(QStringLiteral("dash"), strokeDashToString(l.dash));
    o.insert(QStringLiteral("dashOffset"), l.dashOffset);
    o.insert(QStringLiteral("sketchLength"), l.sketchLength);
    o.insert(QStringLiteral("sketchDeviation"), l.sketchDeviation);
    o.insert(QStringLiteral("sketchSeed"), l.sketchSeed);
    o.insert(QStringLiteral("knockout"), l.knockout);
    o.insert(QStringLiteral("trimStart"), l.trimStart);
    o.insert(QStringLiteral("trimEnd"), l.trimEnd);
    o.insert(QStringLiteral("extrudeSteps"), l.extrudeSteps);
    o.insert(QStringLiteral("extrudeAngle"), l.extrudeAngle);
    o.insert(QStringLiteral("extrudeDarken"), l.extrudeDarken);

    // textPaintToJson only writes the payload of the active kind; the editors bind to all three.
    QJsonObject paint = o.value(QStringLiteral("paint")).toObject();
    TextPaint gradient = l.paint;
    gradient.kind = TextPaintKind::Gradient;
    paint.insert(QStringLiteral("gradient"), textPaintToJson(gradient).value(QStringLiteral("gradient")));
    TextPaint texture = l.paint;
    texture.kind = TextPaintKind::Texture;
    paint.insert(QStringLiteral("texture"), textPaintToJson(texture).value(QStringLiteral("texture")));
    TextPaint effect = l.paint;
    effect.kind = TextPaintKind::Effect;
    paint.insert(QStringLiteral("effect"), textPaintToJson(effect).value(QStringLiteral("effect")));
    o.insert(QStringLiteral("paint"), paint);
    return o;
}

QJsonArray StyleLayers::normalizedLayers(const QList<TextShadingLayer> &layers)
{
    QJsonArray out;
    for (const TextShadingLayer &l : layers)
        out.append(normalizedLayer(l));
    return out;
}

QList<TextShadingLayer> StyleLayers::parseLayers(const QJsonArray &layers)
{
    QList<TextShadingLayer> out;
    for (const QJsonValue &v : layers)
        out.append(textShadingLayerFromJson(v.toObject()));
    return out;
}

QString StyleLayers::prettyId(const QString &id)
{
    QString out;
    for (int i = 0; i < id.size(); ++i) {
        const QChar c = id.at(i);
        if (i == 0) {
            out += c.toUpper();
        } else if (c.isUpper()) {
            out += QLatin1Char(' ');
            out += c.toLower();
        } else {
            out += c;
        }
    }
    return out;
}

QVariantList StyleLayers::specList(const QList<TextAnimParamSpec> &specs, bool effectRules)
{
    QVariantList out;
    for (const TextAnimParamSpec &s : specs) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), s.id);
        m.insert(QStringLiteral("label"), s.label);
        m.insert(QStringLiteral("unit"), s.unit);
        m.insert(QStringLiteral("type"), textAnimParamTypeToString(s.type));
        m.insert(QStringLiteral("default"), slotValueToJson(s.defaultValue).toVariant());
        switch (s.type) {
        case TextAnimParamSpec::Type::Scalar: {
            const double step = effectRules ? (s.step > 0.0 ? s.step : 0.01)
                                            : (s.step > 0.0 ? s.step : (s.max - s.min) / 100.0);
            int decimals = 2;
            if (effectRules) {
                if (s.step >= 1.0)
                    decimals = 0;
                else if (s.step < 0.01)
                    decimals = 3;
            } else {
                decimals = decimalsFor(step);
            }
            m.insert(QStringLiteral("min"), s.min);
            m.insert(QStringLiteral("max"), s.max);
            m.insert(QStringLiteral("step"), step);
            m.insert(QStringLiteral("decimals"), decimals);
            break;
        }
        case TextAnimParamSpec::Type::Vec2: {
            const bool ranged = s.min < s.max;
            m.insert(QStringLiteral("min"), ranged ? s.min : -10000.0);
            m.insert(QStringLiteral("max"), ranged ? s.max : 10000.0);
            m.insert(QStringLiteral("step"), s.step > 0.0 ? s.step : 1.0);
            m.insert(QStringLiteral("decimals"), 2);
            break;
        }
        case TextAnimParamSpec::Type::Enum: {
            QVariantList options;
            for (const QString &v : s.enumValues) {
                options.append(QVariantMap{{QStringLiteral("value"), v},
                                           {QStringLiteral("label"), effectRules ? v : prettyId(v)}});
            }
            m.insert(QStringLiteral("options"), options);
            break;
        }
        case TextAnimParamSpec::Type::Color:
        case TextAnimParamSpec::Type::Bool:
        case TextAnimParamSpec::Type::Text:
            break;
        }
        out.append(m);
    }
    return out;
}

QVariantMap StyleLayers::plainValues(const QMap<QString, VectorSlotValue> &values)
{
    QVariantMap m;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        m.insert(it.key(), slotValueToJson(it.value()).toVariant());
    return m;
}

QVariantList StyleLayers::add(const QVariantList &layers, const QString &kind) const
{
    QList<TextShadingLayer> list = fromVariantList(layers);
    const QString id = mintTextLayerId(list);
    const TextLayerKind k = textLayerKindFromString(kind);
    TextShadingLayer layer;
    switch (k) {
    case TextLayerKind::Fill: layer = solidFillLayer(Qt::white, id); break;
    case TextLayerKind::Stroke: layer = strokeLayer(4.0, Qt::white, id); break;
    case TextLayerKind::Shadow: layer = shadowLayer(QColor(0, 0, 0, 200), 6.0, 6.0, 12.0, 0.8, id); break;
    case TextLayerKind::Glow: layer = glowLayer(QColor(0, 180, 255), 16.0, 0.9, id); break;
    case TextLayerKind::Extrude:
        layer = solidFillLayer(QColor(30, 30, 30), id);
        layer.kind = TextLayerKind::Extrude;
        layer.width = 10.0;
        break;
    }
    list.append(layer);
    return toVariantList(list);
}

QVariantList StyleLayers::duplicateAt(const QVariantList &layers, int index) const
{
    QList<TextShadingLayer> list = fromVariantList(layers);
    if (index < 0 || index >= list.size())
        return layers;
    TextShadingLayer copy = list[index];
    copy.id = mintTextLayerId(list);
    list.insert(index + 1, copy);
    return toVariantList(list);
}

QVariantList StyleLayers::removeAt(const QVariantList &layers, int index) const
{
    QList<TextShadingLayer> list = fromVariantList(layers);
    if (index < 0 || index >= list.size())
        return layers;
    list.removeAt(index);
    return toVariantList(list);
}

QVariantList StyleLayers::moveAt(const QVariantList &layers, int index, int delta) const
{
    QList<TextShadingLayer> list = fromVariantList(layers);
    const int to = index + delta;
    if (index < 0 || index >= list.size() || to < 0 || to >= list.size())
        return layers;
    list.swapItemsAt(index, to);
    return toVariantList(list);
}

QVariantList StyleLayers::patchAt(const QVariantList &layers, int index, const QVariantMap &patch) const
{
    QList<TextShadingLayer> list = fromVariantList(layers);
    if (index < 0 || index >= list.size())
        return layers;
    const QJsonObject merged = mergePatch(normalizedLayer(list[index]), QJsonObject::fromVariantMap(patch));
    list[index] = textShadingLayerFromJson(merged);
    return toVariantList(list);
}

QVariantList StyleLayers::setPaintKindAt(const QVariantList &layers, int index, const QString &paintKind) const
{
    QList<TextShadingLayer> list = fromVariantList(layers);
    if (index < 0 || index >= list.size())
        return layers;
    TextShadingLayer &l = list[index];
    l.paint.kind = textPaintKindFromString(paintKind);
    if (l.paint.kind == TextPaintKind::Effect && l.paint.effect.id.isEmpty()) {
        const auto &specs = textShaderEffectSpecs();
        if (!specs.isEmpty())
            l.paint.effect = effectWithDefaults(specs.first());
    }
    return toVariantList(list);
}

QVariantList StyleLayers::setEffectAt(const QVariantList &layers, int index, const QString &effectId) const
{
    QList<TextShadingLayer> list = fromVariantList(layers);
    const TextEffectSpec *spec = textShaderEffectSpec(effectId);
    if (index < 0 || index >= list.size() || !spec)
        return layers;
    list[index].paint.effect = effectWithDefaults(*spec);
    return toVariantList(list);
}

QVariantList StyleLayers::setEffectParamAt(const QVariantList &layers, int index, const QString &paramId,
                                           const QVariant &value) const
{
    QList<TextShadingLayer> list = fromVariantList(layers);
    if (index < 0 || index >= list.size())
        return layers;
    TextShadingLayer &l = list[index];
    const TextEffectSpec *spec = textShaderEffectSpec(l.paint.effect.id);
    if (!spec)
        return layers;
    for (const TextAnimParamSpec &p : spec->params) {
        if (p.id == paramId) {
            l.paint.effect.params.insert(paramId, paramValueFromJson(p.type, QJsonValue::fromVariant(value)));
            break;
        }
    }
    return toVariantList(list);
}

QVariantList StyleLayers::addGradientStopAt(const QVariantList &layers, int index, double pos) const
{
    QList<TextShadingLayer> list = fromVariantList(layers);
    if (index < 0 || index >= list.size())
        return layers;
    TextGradient &g = list[index].paint.gradient;

    QList<TextGradientStop> sorted = g.stops;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.pos < b.pos; });
    QColor c = sorted.isEmpty() ? QColor(Qt::white) : sorted.first().color;
    for (int i = 0; i + 1 < sorted.size(); ++i) {
        if (pos >= sorted[i].pos && pos <= sorted[i + 1].pos) {
            const double span = sorted[i + 1].pos - sorted[i].pos;
            const double f = span > 0 ? (pos - sorted[i].pos) / span : 0.0;
            const QColor a = sorted[i].color, b = sorted[i + 1].color;
            c = QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * f, a.greenF() + (b.greenF() - a.greenF()) * f,
                                 a.blueF() + (b.blueF() - a.blueF()) * f, a.alphaF() + (b.alphaF() - a.alphaF()) * f);
            break;
        }
        if (pos > sorted[i + 1].pos)
            c = sorted[i + 1].color;
    }
    g.stops.append({pos, c});
    return toVariantList(list);
}

QVariantList StyleLayers::removeGradientStopAt(const QVariantList &layers, int index, int stop) const
{
    QList<TextShadingLayer> list = fromVariantList(layers);
    if (index < 0 || index >= list.size())
        return layers;
    TextGradient &g = list[index].paint.gradient;
    if (g.stops.size() <= 2 || stop < 0 || stop >= g.stops.size())
        return layers;
    g.stops.removeAt(stop);
    return toVariantList(list);
}

QVariantList StyleLayers::setGradientStopAt(const QVariantList &layers, int index, int stop, double pos,
                                            const QString &color) const
{
    QList<TextShadingLayer> list = fromVariantList(layers);
    if (index < 0 || index >= list.size())
        return layers;
    TextGradient &g = list[index].paint.gradient;
    if (stop < 0 || stop >= g.stops.size())
        return layers;
    g.stops[stop].pos = pos;
    g.stops[stop].color = QColor(color);
    return toVariantList(list);
}

QVariantList StyleLayers::options(const QString &name) const
{
    const auto opt = [](const QString &value, const QString &label) {
        return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("label"), label}};
    };
    QVariantList out;
    if (name == QLatin1String("paintKind")) {
        for (TextPaintKind k : {TextPaintKind::Solid, TextPaintKind::Gradient, TextPaintKind::Texture, TextPaintKind::Effect})
            out.append(opt(textPaintKindToString(k), paintLabel(k)));
    } else if (name == QLatin1String("blend")) {
        for (BlendMode b : {BlendMode::Normal, BlendMode::Multiply, BlendMode::Screen, BlendMode::Overlay, BlendMode::Add,
                            BlendMode::Darken, BlendMode::Lighten}) {
            QString label = blendModeToString(b);
            label[0] = label[0].toUpper();
            out.append(opt(blendModeToString(b), label));
        }
    } else if (name == QLatin1String("strokeAlign")) {
        out.append(opt(strokeAlignToString(StrokeAlign::Center), tr("Center")));
        out.append(opt(strokeAlignToString(StrokeAlign::Outside), tr("Outside")));
        out.append(opt(strokeAlignToString(StrokeAlign::Inside), tr("Inside")));
    } else if (name == QLatin1String("dash")) {
        out.append(opt(strokeDashToString(StrokeDash::Solid), tr("Solid")));
        out.append(opt(strokeDashToString(StrokeDash::Dash), tr("Dash")));
        out.append(opt(strokeDashToString(StrokeDash::Dot), tr("Dot")));
        out.append(opt(strokeDashToString(StrokeDash::DashDot), tr("Dash dot")));
    } else if (name == QLatin1String("scope")) {
        out.append(opt(textLayerScopeToString(TextLayerScope::All), tr("All text")));
        out.append(opt(textLayerScopeToString(TextLayerScope::Base), tr("Base only")));
        out.append(opt(textLayerScopeToString(TextLayerScope::Accent), tr("Accent only")));
    } else if (name == QLatin1String("gradientKind")) {
        out.append(opt(textGradientKindToString(TextGradientKind::Linear), tr("Linear")));
        out.append(opt(textGradientKindToString(TextGradientKind::Radial), tr("Radial")));
        out.append(opt(textGradientKindToString(TextGradientKind::Sweep), tr("Sweep")));
    } else if (name == QLatin1String("gradientSpace")) {
        out.append(opt(textGradientSpaceToString(TextGradientSpace::Block), tr("Block")));
        out.append(opt(textGradientSpaceToString(TextGradientSpace::Line), tr("Line")));
        out.append(opt(textGradientSpaceToString(TextGradientSpace::Word), tr("Word")));
        out.append(opt(textGradientSpaceToString(TextGradientSpace::Glyph), tr("Glyph")));
        out.append(opt(textGradientSpaceToString(TextGradientSpace::AccentRun), tr("Accent run")));
    } else if (name == QLatin1String("layerKind")) {
        for (TextLayerKind k : {TextLayerKind::Fill, TextLayerKind::Stroke, TextLayerKind::Shadow, TextLayerKind::Glow,
                                TextLayerKind::Extrude})
            out.append(opt(textLayerKindToString(k), shadingLayerKindLabel(k)));
    } else if (name == QLatin1String("effect")) {
        for (const TextEffectSpec &spec : textShaderEffectSpecs())
            out.append(opt(spec.id, spec.label));
    }
    return out;
}

QString StyleLayers::layerLabel(const QVariantMap &layer) const
{
    return layerText(textShadingLayerFromJson(QJsonObject::fromVariantMap(layer)));
}

QString StyleLayers::localPath(const QUrl &url) const
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

QVariantList StyleLayers::effectParams(const QString &effectId) const
{
    const TextEffectSpec *spec = textShaderEffectSpec(effectId);
    return spec ? specList(spec->params, true) : QVariantList();
}

QVariantMap StyleLayers::effectValues(const QVariantList &layers, int index) const
{
    const QList<TextShadingLayer> list = fromVariantList(layers);
    if (index < 0 || index >= list.size())
        return {};
    const TextShadingLayer &l = list[index];
    const TextEffectSpec *spec = textShaderEffectSpec(l.paint.effect.id);
    if (!spec)
        return {};
    QMap<QString, VectorSlotValue> values;
    for (const TextAnimParamSpec &p : spec->params)
        values.insert(p.id, l.paint.effect.params.value(p.id, p.defaultValue));
    return plainValues(values);
}
