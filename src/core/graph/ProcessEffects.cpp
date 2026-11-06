#include "core/graph/ProcessEffects.h"
#include "core/sources/MediaSource.h"
#include "core/sources/EffectSources.h"
#ifdef PRISM_HAVE_SEGMENTATION
#include "core/sources/SegmentationSource.h"
#endif

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointF>

namespace {

// ── JSON helpers for polygon / corner params ────────────────────────────────
QVector<QPointF> pointsFromArray(const QJsonArray &arr) {
    QVector<QPointF> pts;
    for (const QJsonValue &v : arr) {
        const QJsonArray pair = v.toArray();
        if (pair.size() == 2)
            pts << QPointF(pair.at(0).toDouble(), pair.at(1).toDouble());
    }
    return pts;
}

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

} // namespace

namespace ProcessEffects {

const QVector<ProcessEffectDescriptor> &all() {
    static const QVector<ProcessEffectDescriptor> registry{
        makeCrop(), makeFlipH(), makeFlipV(), makeSegment(),
        makeFlip(), makeRotate(), makeOpacity(), makeChromaKey(),
        makeBlur(), makeSharpen(), makeKeystone(), makePolygonMask()};
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
        if (effectSlug(d.name) == slug || effectSlug(d.menuLabel) == slug)
            return &d;
    }
    return nullptr;
}

std::unique_ptr<MediaSource> applySourceEffects(std::unique_ptr<MediaSource> source,
                                                const QVector<SourceEffectRef> &effects) {
    for (const SourceEffectRef &ref : effects) {
        const ProcessEffectDescriptor *d = byId(ref.effectId);
        if (source && d && d->wrapSource)
            source = d->wrapSource(std::move(source), ref.params);
    }
    return source;
}

QString sourceEffectsKey(const QVector<SourceEffectRef> &effects) {
    QString key;
    for (const SourceEffectRef &ref : effects)
        key += QStringLiteral("%1{%2}").arg(ref.effectId)
                   .arg(QString::fromUtf8(QJsonDocument(ref.params).toJson(QJsonDocument::Compact)));
    return key;
}

} // namespace ProcessEffects
