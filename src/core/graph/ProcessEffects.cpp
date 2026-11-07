#include "core/graph/ProcessEffects.h"
#include "core/graph/ClipGraph.h"
#include "core/sources/MediaSource.h"
#include "core/sources/EffectSources.h"
#include "core/effects/ShaderEffects.h"
#include "core/effects/ShaderEffectSource.h"
#ifdef PRISM_HAVE_SEGMENTATION
#include "core/sources/SegmentationSource.h"
#endif

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointF>

namespace {

// ── JSON helpers for polygon / corner params ────────────────────────────────
QVector<QPointF> pointsFromArray(const QJsonArray &arr) { return effectPointsFromJson(arr); }

QVector<QPointF> cornersFromParams(const QJsonObject &p) {
    QVector<QPointF> c = pointsFromArray(p["corners"].toArray());
    if (c.size() != 4) c = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    return c;
}

// ── Crop (fold) ──────────────────────────────────────────────────────────────
ProcessEffectDescriptor makeCrop() {
    ProcessEffectDescriptor d;
    d.id = 0;
    d.name = "Crop";
    d.menuLabel = "Crop";
    d.defaultParams = QJsonObject{{"x", 0.0}, {"y", 0.0}, {"w", 1.0}, {"h", 1.0}};
    d.isDecorator = true;
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<CropSource>(std::move(inner),
                                            (float)p["x"].toDouble(0.0),
                                            (float)p["y"].toDouble(0.0),
                                            (float)p["w"].toDouble(1.0),
                                            (float)p["h"].toDouble(1.0));
    };
    d.editLabel = "Edit Crop";
    return d;
}

// ── Legacy separate flips (id 1/2): kept for project load, hidden from menu ───
ProcessEffectDescriptor makeFlipH() {
    ProcessEffectDescriptor d;
    d.id = 1;
    d.name = "Flip H";
    d.menuLabel = "Flip Horizontal";
    d.available = false;   // superseded by the combined Flip node
    d.isDecorator = true;
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<FlipSource>(std::move(inner), true, false);
    };
    return d;
}

ProcessEffectDescriptor makeFlipV() {
    ProcessEffectDescriptor d;
    d.id = 2;
    d.name = "Flip V";
    d.menuLabel = "Flip Vertical";
    d.available = false;   // superseded by the combined Flip node
    d.isDecorator = true;
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<FlipSource>(std::move(inner), false, true);
    };
    return d;
}

// ── Remove Background (decorator) ────────────────────────────────────────────
ProcessEffectDescriptor makeSegment() {
    ProcessEffectDescriptor d;
    d.id = 3;
    d.name = "Remove BG";
    d.menuLabel = "Remove Background";
    d.isDecorator = true;
#ifdef PRISM_HAVE_SEGMENTATION
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<SegmentationSource>(std::move(inner));
    };
#else
    d.available = false;
#endif
    return d;
}

// ── Flip (fold, combined direction) ──────────────────────────────────────────
ProcessEffectDescriptor makeFlip() {
    ProcessEffectDescriptor d;
    d.id = 4;
    d.name = "Flip";
    d.menuLabel = "Flip";
    d.defaultParams = QJsonObject{{"direction", 1}};   // 0=H, 1=V, 2=Both
    d.isDecorator = true;
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        const int dir = p["direction"].toInt(1);
        return std::make_unique<FlipSource>(std::move(inner),
                                            dir == 0 || dir == 2,
                                            dir == 1 || dir == 2);
    };
    d.editLabel = "Direction";
    d.dynamicLabel = [](const QJsonObject &p) {
        switch (p["direction"].toInt(1)) {
        case 0: return QStringLiteral("Horizontal");
        case 2: return QStringLiteral("Both");
        default: return QStringLiteral("Vertical");
        }
    };
    return d;
}

// ── Rotate (decorator) ───────────────────────────────────────────────────────
ProcessEffectDescriptor makeRotate() {
    ProcessEffectDescriptor d;
    d.id = 5;
    d.name = "Rotate";
    d.menuLabel = "Rotate";
    d.isDecorator = true;
    d.defaultParams = QJsonObject{{"angle", 90}};
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<RotateSource>(std::move(inner), p["angle"].toInt(90));
    };
    d.editLabel = "Angle";
    return d;
}

// ── Opacity (decorator) ──────────────────────────────────────────────────────
ProcessEffectDescriptor makeOpacity() {
    ProcessEffectDescriptor d;
    d.id = 6;
    d.name = "Opacity";
    d.menuLabel = "Opacity";
    d.isDecorator = true;
    d.defaultParams = QJsonObject{{"opacity", 1.0}};
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<OpacitySource>(std::move(inner), p["opacity"].toDouble(1.0));
    };
    d.editLabel = "Opacity";
    return d;
}

// ── Chroma Key (decorator) ───────────────────────────────────────────────────
ProcessEffectDescriptor makeChromaKey() {
    ProcessEffectDescriptor d;
    d.id = 7;
    d.name = "Chroma Key";
    d.menuLabel = "Chroma Key";
    d.isDecorator = true;
    d.defaultParams = QJsonObject{{"r", 0}, {"g", 255}, {"b", 0},
                                  {"threshold", 0.25}, {"smoothness", 0.10}};
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        const QColor key(p["r"].toInt(0), p["g"].toInt(255), p["b"].toInt(0));
        return std::make_unique<ChromaKeySource>(std::move(inner), key,
                                                 p["threshold"].toDouble(0.25),
                                                 p["smoothness"].toDouble(0.10));
    };
    d.editLabel = "Settings";
    return d;
}

// ── Blur (decorator) ─────────────────────────────────────────────────────────
ProcessEffectDescriptor makeBlur() {
    ProcessEffectDescriptor d;
    d.id = 8;
    d.name = "Blur";
    d.menuLabel = "Blur";
    d.isDecorator = true;
    d.defaultParams = QJsonObject{{"radius", 6}};
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        // GPU proof-of-concept: blur runs as two fragment-shader passes in an
        // offscreen FBO and stays GPU-resident (glTexture()) for the compositor.
        return std::make_unique<GpuBlurSource>(std::move(inner), p["radius"].toInt(6));
    };
    d.editLabel = "Radius";
    return d;
}

// ── Sharpen (decorator) ──────────────────────────────────────────────────────
ProcessEffectDescriptor makeSharpen() {
    ProcessEffectDescriptor d;
    d.id = 9;
    d.name = "Sharpen";
    d.menuLabel = "Sharpen";
    d.isDecorator = true;
    d.defaultParams = QJsonObject{{"amount", 1.0}};
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<SharpenSource>(std::move(inner), p["amount"].toDouble(1.0));
    };
    d.editLabel = "Amount";
    return d;
}

// ── Keystone / Perspective (decorator) ───────────────────────────────────────
ProcessEffectDescriptor makeKeystone() {
    ProcessEffectDescriptor d;
    d.id = 10;
    d.name = "Keystone";
    d.menuLabel = "Keystone / Perspective";
    d.isDecorator = true;
    d.defaultParams = QJsonObject{
        {"corners", QJsonArray{QJsonArray{0.0, 0.0}, QJsonArray{1.0, 0.0},
                               QJsonArray{1.0, 1.0}, QJsonArray{0.0, 1.0}}}};
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<KeystoneSource>(std::move(inner), cornersFromParams(p));
    };
    d.editLabel = "Edit Corners";
    return d;
}

// ── Polygonal masking (decorator) ────────────────────────────────────────────
ProcessEffectDescriptor makePolygonMask() {
    ProcessEffectDescriptor d;
    d.id = 11;
    d.name = "Poly Mask";
    d.menuLabel = "Polygonal Masking";
    d.isDecorator = true;
    d.defaultParams = QJsonObject{{"points", QJsonArray{}}, {"invert", false}};
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<PolygonMaskSource>(std::move(inner),
                                                   pointsFromArray(p["points"].toArray()),
                                                   p["invert"].toBool(false));
    };
    d.editLabel = "Edit Mask";
    return d;
}

// ── Data-driven shader effects (ids >= 12, declared in effects.json) ─────────
ProcessEffectDescriptor makeShaderEffect(const ShaderEffectSpec &spec) {
    ProcessEffectDescriptor d;
    d.id = spec.id;
    d.name = spec.name;
    d.menuLabel = spec.name;
    d.slug = spec.slug;
    d.isDecorator = true;
    d.defaultParams = spec.defaultParams();
    d.wrapSource = [id = spec.id](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        const ShaderEffectSpec *s = ShaderEffects::byId(id);
        if (!s) return inner;
        return std::make_unique<ShaderEffectSource>(std::move(inner), *s, p);
    };
    return d;
}

// ── Shader (decorator running user GLSL; ids 12..66 belong to effects.json) ──
ProcessEffectDescriptor makeShader() {
    ProcessEffectDescriptor d;
    d.id = ProcessEffects::kShaderEffectId;
    d.name = "Shader";
    d.menuLabel = "Shader";
    d.slug = "shader";
    d.isDecorator = true;
    d.defaultParams = ShaderEffects::defaultParamValues(
        ShaderEffects::parseHeaderParams(ShaderEffects::defaultCustomCode()));
    d.wrapSource = [](std::unique_ptr<MediaSource> inner, const QJsonObject &p)
        -> std::unique_ptr<MediaSource> {
        return std::make_unique<ShaderEffectSource>(std::move(inner), ShaderEffects::defaultCustomCode(), p);
    };
    d.editLabel = "Edit Shader";
    return d;
}

} // namespace

namespace ProcessEffects {

const QVector<ProcessEffectDescriptor> &all() {
    static const QVector<ProcessEffectDescriptor> registry = [] {
        QVector<ProcessEffectDescriptor> r{
            makeCrop(), makeFlipH(), makeFlipV(), makeSegment(),
            makeFlip(), makeRotate(), makeOpacity(), makeChromaKey(),
            makeBlur(), makeSharpen(), makeKeystone(), makePolygonMask()};
        for (const ShaderEffectSpec &spec : ShaderEffects::all())
            r.append(makeShaderEffect(spec));
        r.append(makeShader());
        return r;
    }();
    return registry;
}

const ProcessEffectDescriptor *byId(int id) {
    for (const ProcessEffectDescriptor &d : all())
        if (d.id == id) return &d;
    return nullptr;
}

static QString effectSlug(const QString &s) {
    QString out;
    for (QChar c : s.trimmed().toLower()) {
        if (c.isLetterOrNumber())
            out += c;
        else if (!out.isEmpty() && out.back() != QLatin1Char('_'))
            out += QLatin1Char('_');
    }
    while (out.endsWith(QLatin1Char('_')))
        out.chop(1);
    return out;
}

const ProcessEffectDescriptor *byName(const QString &name) {
    const QString key = name.trimmed();
    if (key.isEmpty())
        return nullptr;
    bool ok = false;
    const int id = key.toInt(&ok);
    if (ok)
        return byId(id);
    const QString slug = effectSlug(key);
    for (const ProcessEffectDescriptor &d : all()) {
        if (d.name.compare(key, Qt::CaseInsensitive) == 0)
            return &d;
        if (d.menuLabel.compare(key, Qt::CaseInsensitive) == 0)
            return &d;
        if (!d.slug.isEmpty() && d.slug == slug)
            return &d;
        if (effectSlug(d.name) == slug || effectSlug(d.menuLabel) == slug)
            return &d;
    }
    return nullptr;
}

std::unique_ptr<MediaSource> applySourceEffects(std::unique_ptr<MediaSource> source,
                                                const QVector<SourceEffectRef> &effects,
                                                const AuxBuilder &auxBuilder) {
    for (const SourceEffectRef &ref : effects) {
        const ProcessEffectDescriptor *d = byId(ref.effectId);
        if (source && d && d->wrapSource) {
            if (ref.effectId == kShaderEffectId) {
                auto shader = std::make_unique<ShaderEffectSource>(std::move(source), ref.code, ref.params);
                shader->setNodeId(ref.nodeId);
                source = std::move(shader);
            } else {
                source = d->wrapSource(std::move(source), ref.params);
            }
        }
        if (auxBuilder && !ref.aux.isEmpty()) {
            if (auto *ses = dynamic_cast<ShaderEffectSource *>(source.get())) {
                for (int i = 0; i < ref.aux.size(); ++i)
                    if (ref.aux[i]) ses->setAux(i, auxBuilder(*ref.aux[i]));
            }
        }
    }
    return source;
}

QString sourceEffectsKey(const QVector<SourceEffectRef> &effects) {
    QString key;
    for (const SourceEffectRef &ref : effects) {
        key += QStringLiteral("%1;").arg(ref.effectId);
        if (ref.effectId == kShaderEffectId)
            key += QStringLiteral("%1:").arg(qHash(ref.code));
    }
    return key;
}

QString sourceEffectsParamsKey(const QVector<SourceEffectRef> &effects) {
    QString key;
    for (const SourceEffectRef &ref : effects)
        key += QString::fromUtf8(QJsonDocument(ref.params).toJson(QJsonDocument::Compact))
               + QLatin1Char(';');
    return key;
}

MediaSource *updateSourceEffects(MediaSource *top, const QVector<SourceEffectRef> &effects,
                                 const AuxUpdater &auxUpdater) {
    MediaSource *s = top;
    for (int i = effects.size() - 1; i >= 0; --i) {
        const ProcessEffectDescriptor *d = byId(effects[i].effectId);
        if (!d || !d->wrapSource) continue;
        if (effects[i].effectId == kShaderEffectId) {
            auto *shader = dynamic_cast<ShaderEffectSource *>(s);
            if (!shader || shader->code() != effects[i].code) return nullptr;
        }
        if (!s || !s->updateParams(effects[i].params)) return nullptr;
        if (auxUpdater) {
            auto *ses = dynamic_cast<ShaderEffectSource *>(s);
            for (int j = 0; ses && j < effects[i].aux.size(); ++j) {
                MediaSource *a = ses->auxSource(j);
                if (a && effects[i].aux[j] && !auxUpdater(a, *effects[i].aux[j])) return nullptr;
            }
        }
        s = s->inner();
    }
    return s;
}

} // namespace ProcessEffects
