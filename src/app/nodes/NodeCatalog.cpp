#include "nodes/NodeCatalog.h"

#include "core/graph/ClipGraph.h"

#include "core/effects/ShaderEffects.h"
#include "core/audio/AudioRack.h"
#include "core/audio/dsp/PedalCatalog.h"

#include <algorithm>
#include <QJsonObject>
#include <QSettings>

NodeCatalog::NodeCatalog(QObject *parent)
    : QObject(parent)
{
}

QVariantList NodeCatalog::processEffects() const
{
    QVariantList out;
    for (const ProcessEffectDescriptor &d : ProcessEffects::all()) {
        if (!d.available)
            continue;
        out.append(QVariantMap{
            {"id", d.id},
            {"name", d.name},
            {"menuLabel", d.menuLabel},
            {"hasEditor", !d.editLabel.isEmpty()},
            {"editLabel", d.editLabel},
        });
    }
    return out;
}

QVariantList NodeCatalog::processEffectParams(int id) const
{
    QVariantList out;
    const ShaderEffectSpec *spec = ShaderEffects::byId(id);
    if (!spec)
        return out;
    static const char *const kTypes[] = {"float", "int", "bool", "choice", "color", "point", "file"};
    for (const ShaderParam &p : spec->params) {
        QVariant def = p.def;
        if (p.type == ShaderParam::Bool)
            def = p.def != 0.0;
        else if (p.type == ShaderParam::Color)
            def = QVariantList{p.defColor.redF(), p.defColor.greenF(), p.defColor.blueF(), p.defColor.alphaF()};
        else if (p.type == ShaderParam::Point)
            def = QVariantList{p.defPoint.x(), p.defPoint.y()};
        else if (p.type == ShaderParam::File)
            def = QString();
        out.append(QVariantMap{
            {"name", p.name},
            {"label", p.label},
            {"type", QString::fromLatin1(kTypes[p.type])},
            {"min", p.min},
            {"max", p.max},
            {"step", p.step},
            {"def", def},
            {"choices", p.choices},
            {"filter", p.fileFilter},
        });
    }
    return out;
}

QVariantList NodeCatalog::audioEffects() const
{
    QVariantList out;
    for (const AudioEffectDescriptor &d : AudioEffects::all()) {
        if (!d.available)
            continue;
        out.append(QVariantMap{
            {"id", d.id},
            {"name", d.name},
            {"menuLabel", d.menuLabel},
            {"hasEditor", !d.editLabel.isEmpty()},
            {"editLabel", d.editLabel},
        });
    }
    return out;
}

QString NodeCatalog::effectLabel(int nodeType, const QVariantMap &params) const
{
    const int effect = params.value(QStringLiteral("effect")).toInt();
    const QJsonObject nested = QJsonObject::fromVariantMap(params.value(QStringLiteral("params")).toMap());
    if (nodeType == int(ClipGraph::NodeType::Process)) {
        const ProcessEffectDescriptor *d = ProcessEffects::byId(effect);
        if (!d)
            return {};
        return d->dynamicLabel ? d->dynamicLabel(nested) : d->editLabel;
    }
    if (nodeType == int(ClipGraph::NodeType::AudioEffect)) {
        const AudioEffectDescriptor *d = AudioEffects::byId(effect);
        if (!d)
            return {};
        return d->dynamicLabel ? d->dynamicLabel(nested) : d->editLabel;
    }
    return {};
}

QStringList NodeCatalog::pedalChain(const QVariantMap &params) const
{
    const auto rack = prism::audiofx::fromJson(
        QJsonObject::fromVariantMap(params.value(QStringLiteral("params")).toMap()));
    QStringList names;
    for (const prism::audiofx::RackItem &item : rack.items) {
        if (item.isSplit()) {
            names << tr("Split");
            continue;
        }
        const QString &type = item.pedal()->type;
        const auto *spec = prism::audiofx::pedalSpec(type.toStdString());
        names << (spec ? QString::fromUtf8(spec->label) : type);
    }
    return names;
}

bool NodeCatalog::effectHasEditor(int nodeType, int effect) const
{
    if (nodeType == int(ClipGraph::NodeType::Process)) {
        const ProcessEffectDescriptor *d = ProcessEffects::byId(effect);
        return d && !d->editLabel.isEmpty();
    }
    if (nodeType == int(ClipGraph::NodeType::AudioEffect)) {
        const AudioEffectDescriptor *d = AudioEffects::byId(effect);
        return d && !d->editLabel.isEmpty();
    }
    return false;
}

bool NodeCatalog::portsCompatible(int a, int b) const
{
    return ClipGraph::portsCompatible(ClipGraph::PortKind(a), ClipGraph::PortKind(b));
}

bool NodeCatalog::scriptingAvailable() const
{
#ifdef PRISM_HAVE_LUA
    return true;
#else
    return false;
#endif
}

QStringList NodeCatalog::recentNodeKeys() const
{
    return QSettings().value(QStringLiteral("palette/recent")).toStringList();
}

void NodeCatalog::noteNodeAdded(const QString &key)
{
    QStringList recent = recentNodeKeys();
    recent.removeAll(key);
    recent.prepend(key);
    while (recent.size() > 5)
        recent.removeLast();
    QSettings().setValue(QStringLiteral("palette/recent"), recent);
}

// ── Quick-add catalogue ─────────────────────────────────────────────────────

namespace {

using PK = ClipGraph::PortKind;
using NT = ClipGraph::NodeType;

QList<int> kinds(std::initializer_list<PK> l)
{
    QList<int> out;
    for (PK k : l)
        out.append(int(k));
    return out;
}

struct BuiltinFx {
    int id;
    const char *slug;
    const char *category;
    const char *description;
    const char *keywords;
};

const BuiltinFx *builtinFx(int id)
{
    static const BuiltinFx table[] = {
        {0, "crop", QT_TRANSLATE_NOOP("NodeCatalog", "Transform"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Cut the picture down to a rectangle."), "trim cut region"},
        {1, "flip_h", QT_TRANSLATE_NOOP("NodeCatalog", "Transform"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Mirror the picture left to right."), "mirror horizontal"},
        {2, "flip_v", QT_TRANSLATE_NOOP("NodeCatalog", "Transform"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Turn the picture upside down."), "mirror vertical"},
        {3, "remove_bg", QT_TRANSLATE_NOOP("NodeCatalog", "Keying"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Cut a person out of their background automatically."),
         "background matte cutout segment"},
        {4, "flip", QT_TRANSLATE_NOOP("NodeCatalog", "Transform"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Mirror the picture horizontally, vertically or both."), "mirror"},
        {5, "rotate", QT_TRANSLATE_NOOP("NodeCatalog", "Transform"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Turn the picture by an angle."), "turn spin"},
        {6, "opacity", QT_TRANSLATE_NOOP("NodeCatalog", "Color"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Make the picture more or less see-through."), "transparency alpha fade"},
        {7, "chroma_key", QT_TRANSLATE_NOOP("NodeCatalog", "Keying"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Make a green or blue screen transparent."), "green greenscreen keyer"},
        {8, "blur", QT_TRANSLATE_NOOP("NodeCatalog", "Blur"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Soften the whole picture."), "soften gaussian"},
        {9, "sharpen", QT_TRANSLATE_NOOP("NodeCatalog", "Blur"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Bring out fine detail and edges."), "crisp detail"},
        {10, "keystone", QT_TRANSLATE_NOOP("NodeCatalog", "Distort"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Fix perspective by dragging the picture's corners."), "perspective warp skew"},
        {11, "poly_mask", QT_TRANSLATE_NOOP("NodeCatalog", "Keying"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Show only the area inside a shape you draw."), "mask polygon"},
    };
    for (const BuiltinFx &b : table)
        if (b.id == id)
            return &b;
    return nullptr;
}

QList<QuickAddEntry> buildCatalogue()
{
    QList<QuickAddEntry> out;
    const auto add = [&out](QString id, QString name, QString category, QString description,
                            const QString &keywords, NT type, QList<int> in, QList<int> outK,
                            QVariantMap params = {}, QString source = {}, bool unique = false) {
        QuickAddEntry e;
        e.id = std::move(id);
        e.name = std::move(name);
        e.category = std::move(category);
        e.description = std::move(description);
        e.keywords = keywords.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        e.nodeType = int(type);
        e.inKinds = std::move(in);
        e.outKinds = std::move(outK);
        e.params = std::move(params);
        e.source = std::move(source);
        e.unique = unique;
        out.append(std::move(e));
    };

    const QString srcCat = NodeCatalog::tr("Sources");
    const auto src = [&](const QString &kind, const QString &name, const QString &description,
                         const QString &keywords) {
        add(QStringLiteral("source:") + kind, name, srcCat, description, keywords, NT::Input, {},
            kinds({PK::ChainOut, PK::AudioControllerOut}), {}, kind);
    };
    src(QStringLiteral("file"), NodeCatalog::tr("Media File"),
        NodeCatalog::tr("Play a video, image or audio file from disk."),
        QStringLiteral("mp4 clip movie video mov image photo picture audio mp3 wav import"));
    src(QStringLiteral("slideshow"), NodeCatalog::tr("Slideshow"),
        NodeCatalog::tr("Cycle through the pictures in a folder."),
        QStringLiteral("images photos pictures folder gallery"));
    src(QStringLiteral("svgTemplate"), NodeCatalog::tr("SVG Template"),
        NodeCatalog::tr("A vector graphic with fields you can fill in, like a lower third."),
        QStringLiteral("svg graphic lower third title"));
    src(QStringLiteral("camera"), NodeCatalog::tr("Camera"),
        NodeCatalog::tr("Live video from a webcam or capture card."),
        QStringLiteral("webcam cam capture card video input"));
    src(QStringLiteral("screen"), NodeCatalog::tr("Screen Capture"),
        NodeCatalog::tr("Show everything on one of your displays."),
        QStringLiteral("display desktop monitor"));
    src(QStringLiteral("window"), NodeCatalog::tr("Window Capture"),
        NodeCatalog::tr("Show a single application window."), QStringLiteral("app application"));
    src(QStringLiteral("canvas"), NodeCatalog::tr("Canvas"),
        NodeCatalog::tr("A blank picture in a solid colour, transparent or checkered."),
        QStringLiteral("solid color background checkered"));
    src(QStringLiteral("shader"), NodeCatalog::tr("Shader"),
        NodeCatalog::tr("A picture drawn by a GLSL fragment shader you write."),
        QStringLiteral("glsl generator effect code"));
    src(QStringLiteral("text"), NodeCatalog::tr("Text"),
        NodeCatalog::tr("Styled text, such as a title or caption."),
        QStringLiteral("title caption label subtitle"));
    src(QStringLiteral("shape"), NodeCatalog::tr("Shape"),
        NodeCatalog::tr("A rectangle, circle or other vector shape."),
        QStringLiteral("rectangle circle box"));
    src(QStringLiteral("scene"), NodeCatalog::tr("Scene"),
        NodeCatalog::tr("Show another scene's finished picture inside this one."),
        QStringLiteral("nested embed picture in picture"));
    src(QStringLiteral("url"), NodeCatalog::tr("Video URL"),
        NodeCatalog::tr("Play a stream or video from a web address."),
        QStringLiteral("stream http rtmp hls rtsp link network"));
    src(QStringLiteral("ndi"), NodeCatalog::tr("NDI Source"),
        NodeCatalog::tr("Receive video from another computer over NDI."),
        QStringLiteral("network newtek"));
    src(QStringLiteral("webrtc"), NodeCatalog::tr("Phone Camera (WebRTC)"),
        NodeCatalog::tr("Use your phone's camera as a video source."),
        QStringLiteral("phone mobile smartphone"));

    const QString fxCat = NodeCatalog::tr("Video FX");
    for (const ProcessEffectDescriptor &d : ProcessEffects::all()) {
        if (!d.available)
            continue;
        QString slug = d.slug;
        QString category = fxCat;
        QString description;
        QString keywords;
        if (const ShaderEffectSpec *spec = ShaderEffects::byId(d.id)) {
            category = spec->category;
            description = spec->description;
        } else if (const BuiltinFx *b = builtinFx(d.id)) {
            slug = QLatin1String(b->slug);
            category = NodeCatalog::tr(b->category);
            description = NodeCatalog::tr(b->description);
            keywords = QLatin1String(b->keywords);
        }
        if (slug.isEmpty())
            slug = QString::number(d.id);
        if (description.isEmpty())
            description = NodeCatalog::tr("A video effect.");
        add(QStringLiteral("process:") + slug, d.menuLabel, category, description, keywords, NT::Process,
            kinds({PK::ChainIn}), kinds({PK::ChainOut}), {{"effect", d.id}});
    }

    add(QStringLiteral("layer"), NodeCatalog::tr("Layer"), NodeCatalog::tr("Compositing"),
        NodeCatalog::tr("Stack several pictures into one, with position and order."),
        QStringLiteral("composite overlay stack pip"), NT::Layer, kinds({PK::ChainIn}),
        kinds({PK::ChainOut}));

    for (const AudioEffectDescriptor &d : AudioEffects::all()) {
        if (!d.available)
            continue;
        add(QStringLiteral("audiofx:") + QString::number(d.id), d.menuLabel, NodeCatalog::tr("Audio"),
            NodeCatalog::tr("A chain of audio effects such as reverb, EQ and compression."),
            QStringLiteral("pedalboard effects reverb eq compressor"), NT::AudioEffect,
            kinds({PK::AudioEffectIn}), kinds({PK::AudioEffectOut}), {{"effect", d.id}});
    }
    add(QStringLiteral("audio:mixer"), NodeCatalog::tr("Mixer"), NodeCatalog::tr("Audio"),
        NodeCatalog::tr("Combine several audio streams with a volume for each."),
        QStringLiteral("mix volume sum"), NT::AudioMixer, kinds({PK::AudioMixerIn}),
        kinds({PK::AudioMixerOut}));
    add(QStringLiteral("audio:output"), NodeCatalog::tr("Audio Output"), NodeCatalog::tr("Audio"),
        NodeCatalog::tr("Send sound to a speaker or audio device."), QStringLiteral("speaker master device"),
        NT::MasterAudioOutput, kinds({PK::MasterAudioIn}), {}, {}, {}, true);
    add(QStringLiteral("audio:mic"), NodeCatalog::tr("Mic Input"), NodeCatalog::tr("Audio"),
        NodeCatalog::tr("Sound from a microphone or line input."), QStringLiteral("microphone audio input"),
        NT::MasterAudioInput, {}, kinds({PK::MasterAudioInputOut}));
    add(QStringLiteral("audio:capture"), NodeCatalog::tr("Audio Capture"), NodeCatalog::tr("Audio"),
        NodeCatalog::tr("Record whatever this computer is playing."), QStringLiteral("system desktop loopback"),
        NT::MasterAudioCapture, {}, kinds({PK::MasterAudioInputOut}));

#ifdef PRISM_HAVE_LUA
    const QString logicCat = NodeCatalog::tr("Logic");
    add(QStringLiteral("script"), NodeCatalog::tr("Script"), logicCat,
        NodeCatalog::tr("Run a Lua script that reacts to events and sends out values."),
        QStringLiteral("lua code program timer pulse trigger clock"), NT::Script, kinds({PK::DataIn}),
        kinds({PK::ScriptOut}));
    add(QStringLiteral("audio_analyzer"), NodeCatalog::tr("Audio Analyzer"), logicCat,
        NodeCatalog::tr("Turn sound into numbers such as level, bass and beat."),
        QStringLiteral("fft beat spectrum dsp"), NT::AudioAnalyzer, kinds({PK::AudioIn}),
        kinds({PK::ScriptOut}));

    struct Mod {
        const char *slug;
        const char *name;
        const char *description;
        const char *keywords;
    };
    static const Mod mods[] = {
        {"lfo", QT_TRANSLATE_NOOP("NodeCatalog", "LFO"),
         QT_TRANSLATE_NOOP("NodeCatalog", "A repeating wave that moves a parameter up and down in time."),
         "oscillator sine wave tempo bpm"},
        {"envelope", QT_TRANSLATE_NOOP("NodeCatalog", "Envelope Follower"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Follows how loud a sound is to move a parameter."),
         "audio level loudness"},
        {"map_range", QT_TRANSLATE_NOOP("NodeCatalog", "Map Range"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Rescale another value into a new minimum and maximum."),
         "scale remap curve clamp"},
        {"smooth", QT_TRANSLATE_NOOP("NodeCatalog", "Smooth"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Make another value glide instead of jump."), "lag slew"},
        {"random", QT_TRANSLATE_NOOP("NodeCatalog", "Random"),
         QT_TRANSLATE_NOOP("NodeCatalog", "A new random value on every tick or beat."), "noise steps"},
        {"trigger", QT_TRANSLATE_NOOP("NodeCatalog", "Trigger"),
         QT_TRANSLATE_NOOP("NodeCatalog", "A short pulse when another value goes over a threshold."), "pulse"},
        {"counter", QT_TRANSLATE_NOOP("NodeCatalog", "Counter"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Step through a set of values each time it is triggered."), "steps count"},
        {"toggle", QT_TRANSLATE_NOOP("NodeCatalog", "Toggle"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Flip between on and off each time it is triggered."), "switch flip"},
        {"math", QT_TRANSLATE_NOOP("NodeCatalog", "Math"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Combine two values by adding, multiplying and similar."),
         "add multiply subtract"},
        {"control_in", QT_TRANSLATE_NOOP("NodeCatalog", "Control In"),
         QT_TRANSLATE_NOOP("NodeCatalog", "Turns an incoming OSC control into a value you can link to parameters."),
         "osc midi external controller"},
    };
    int kind = 0;
    for (const Mod &m : mods) {
        add(QStringLiteral("modulator:") + QLatin1String(m.slug), NodeCatalog::tr(m.name),
            NodeCatalog::tr("Modulators"), NodeCatalog::tr(m.description),
            QStringLiteral("modulator link ") + QLatin1String(m.keywords), NT::Modulator, {},
            kinds({PK::ScriptOut}), {{"kind", kind++}});
    }
#endif
    return out;
}

bool wordStartsWith(const QString &text, const QString &q)
{
    for (int i = 0; i < text.size(); ++i) {
        if (!text.at(i).isLetterOrNumber() || (i > 0 && text.at(i - 1).isLetterOrNumber()))
            continue;
        if (QStringView(text).mid(i).startsWith(q))
            return true;
    }
    return false;
}

} // namespace

const QList<QuickAddEntry> &NodeCatalog::quickAddCatalogue()
{
    static const QList<QuickAddEntry> catalogue = buildCatalogue();
    return catalogue;
}

int NodeCatalog::quickAddScore(const QString &query, const QuickAddEntry &e)
{
    const QString q = query.trimmed().toLower();
    if (q.isEmpty())
        return 0;
    const QString name = e.name.toLower();
    if (name.startsWith(q))
        return 1000;
    if (wordStartsWith(name, q))
        return 800;
    if (name.contains(q))
        return 600;
    if (e.category.toLower().contains(q))
        return 400;
    for (const QString &k : e.keywords)
        if (k.toLower().startsWith(q))
            return 300;
    for (const QString &k : e.keywords)
        if (k.toLower().contains(q))
            return 250;
    if (e.description.toLower().contains(q))
        return 200;
    return -1;
}

QList<QuickAddEntry> NodeCatalog::quickAddSearch(const QString &query, int fromPortKind, bool fromOutput)
{
    struct Hit {
        const QuickAddEntry *entry;
        int score;
        int index;
    };
    QList<Hit> hits;
    const QList<QuickAddEntry> &all = quickAddCatalogue();
    for (int i = 0; i < all.size(); ++i) {
        const QuickAddEntry &e = all.at(i);
        if (fromPortKind >= 0) {
            const QList<int> &ports = fromOutput ? e.inKinds : e.outKinds;
            bool ok = false;
            for (int k : ports)
                ok = ok || ClipGraph::portsCompatible(PK(fromPortKind), PK(k));
            if (!ok)
                continue;
        }
        const int score = quickAddScore(query, e);
        if (score >= 0)
            hits.append({&e, score, i});
    }

    QList<QuickAddEntry> out;
    if (query.trimmed().isEmpty()) {
        QStringList order;
        for (const Hit &h : std::as_const(hits))
            if (!order.contains(h.entry->category))
                order.append(h.entry->category);
        for (const QString &category : std::as_const(order))
            for (const Hit &h : std::as_const(hits))
                if (h.entry->category == category)
                    out.append(*h.entry);
        return out;
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.entry->name.size() != b.entry->name.size())
            return a.entry->name.size() < b.entry->name.size();
        return a.index < b.index;
    });
    for (const Hit &h : std::as_const(hits))
        out.append(*h.entry);
    return out;
}

QVariantList NodeCatalog::quickAddEntries(const QString &query, int fromPortKind, bool fromOutput) const
{
    const auto toList = [](const QList<int> &l) {
        QVariantList v;
        for (int i : l)
            v.append(i);
        return v;
    };
    QVariantList out;
    for (const QuickAddEntry &e : quickAddSearch(query, fromPortKind, fromOutput)) {
        out.append(QVariantMap{
            {"id", e.id},
            {"name", e.name},
            {"category", e.category},
            {"description", e.description},
            {"nodeType", e.nodeType},
            {"params", e.params},
            {"inKinds", toList(e.inKinds)},
            {"outKinds", toList(e.outKinds)},
            {"source", e.source},
            {"unique", e.unique},
        });
    }
    return out;
}
