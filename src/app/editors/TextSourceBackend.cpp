#include "app/editors/TextSourceBackend.h"

#include "app/AppController.h"
#include "app/common/ThumbHelper.h"
#include "app/editors/StyleLayers.h"
#include "core/graph/ClipGraphModels.h"
#include "core/render/SkiaRender.h"
#include "core/sources/TextSource.h"
#include "core/text/TextAnimationPreset.h"
#include "core/text/TextLook.h"
#include "core/text/TextParamSpec.h"
#include "core/text/TextPresetStore.h"

#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMutexLocker>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <cmath>

using namespace prism;

namespace {

QString displayValue(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::String: return v.toString();
    case QJsonValue::Bool: return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        const double d = v.toDouble();
        const qint64 i = static_cast<qint64>(d);
        if (static_cast<double>(i) == d)
            return QString::number(i);
        return QString::number(d);
    }
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    default:
        return QStringLiteral("—");
    }
}

// Heuristic scan of Lua source for `return { key = …, … }` table keys, so variables can be offered
// before the script has produced any output.
QStringList luaReturnTableKeys(const QString &code)
{
    QStringList keys;
    static const QRegularExpression returnRx(QStringLiteral("\\breturn\\s*\\{"));
    static const QRegularExpression identRx(QStringLiteral("[a-zA-Z_][a-zA-Z0-9_]*"));

    QRegularExpressionMatchIterator blocks = returnRx.globalMatch(code);
    while (blocks.hasNext()) {
        int i = blocks.next().capturedEnd();
        int depth = 1;
        bool expectKey = true;
        while (i < code.size() && depth > 0) {
            const QChar c = code.at(i);
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                const QChar quote = c;
                ++i;
                while (i < code.size() && code.at(i) != quote) {
                    if (code.at(i) == QLatin1Char('\\'))
                        ++i;
                    ++i;
                }
                ++i;
                expectKey = false;
                continue;
            }
            if (c == QLatin1Char('-') && i + 1 < code.size() && code.at(i + 1) == QLatin1Char('-')) {
                while (i < code.size() && code.at(i) != QLatin1Char('\n'))
                    ++i;
                continue;
            }
            if (c == QLatin1Char('{')) {
                ++depth;
                ++i;
                continue;
            }
            if (c == QLatin1Char('}')) {
                --depth;
                ++i;
                continue;
            }
            if (depth == 1) {
                if (c == QLatin1Char(',') || c == QLatin1Char(';')) {
                    expectKey = true;
                    ++i;
                    continue;
                }
                if (expectKey && (c.isLetter() || c == QLatin1Char('_'))) {
                    const QRegularExpressionMatch m = identRx.match(
                        code, i, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
                    int j = m.capturedEnd();
                    while (j < code.size() && code.at(j).isSpace())
                        ++j;
                    if (j < code.size() && code.at(j) == QLatin1Char('=')
                        && (j + 1 >= code.size() || code.at(j + 1) != QLatin1Char('='))) {
                        keys << m.captured();
                        i = j + 1;
                        expectKey = false;
                        continue;
                    }
                    expectKey = false;
                    i = m.capturedEnd();
                    continue;
                }
                if (!c.isSpace())
                    expectKey = false;
            }
            ++i;
        }
    }
    keys.removeDuplicates();
    return keys;
}

QImage cardImage(const QImage &img, const QSize &size)
{
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x24, 0x24, 0x24));
    p.drawRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), 6, 6);
    if (!img.isNull())
        p.drawImage(QRect(QPoint(0, 0), size), img);
    return out;
}

QSize canvasForCard(const QSize &size, int height)
{
    return QSize(qMax(1, height * size.width() / qMax(1, size.height())), height);
}

QImage packThumb(const TextPreset &preset, const QSize &size)
{
    const QSize canvas(1080, qMax(1, 1080 * size.height() / qMax(1, size.width())));
    const QString text = preset.sampleText.isEmpty() ? QStringLiteral("Your text here") : preset.sampleText;
    TextAnimClock clock;
    clock.totalSec = textAnimationInSeconds(preset.style, text, canvas);
    return cardImage(renderText(preset.style, text, canvas, clock), size);
}

QImage lookThumb(const QString &id, const QString &family, int weight, bool italic, const QSize &size)
{
    TextStyle s;
    s.fontFamily = family.isEmpty() ? QStringLiteral("Inter") : family;
    s.fontWeight = weight;
    s.italic = italic;
    s.pixelSize = 58;
    s.wordWrap = false;
    applyTextLook(s, id, {});
    return cardImage(renderText(s, QStringLiteral("Aa"), canvasForCard(size, 150), {}), size);
}

QImage animThumb(int slot, const TextAnimationPreset &preset, const QSize &size)
{
    TextStyle s;
    s.pixelSize = 58;
    TextAnimationSlot anim;
    anim.presetId = preset.id;
    (slot == 0 ? s.animation.in : slot == 1 ? s.animation.out : s.animation.loop) = anim;
    const QSize canvas = canvasForCard(size, 150);
    const QString text = preset.sampleText.isEmpty() ? QStringLiteral("Your text") : preset.sampleText;
    TextAnimClock clock;
    if (slot == 0) {
        const double len = textAnimationInSeconds(s, text, canvas);
        clock.totalSec = len;
        if (len > 0) {
            clock.phase = TextAnimClock::Phase::In;
            clock.phaseElapsedSec = clock.totalSec = len * 0.55;
        }
    } else if (slot == 1) {
        const double len = textAnimationOutSeconds(s, text, canvas);
        if (len > 0) {
            clock.phase = TextAnimClock::Phase::Out;
            clock.phaseElapsedSec = clock.totalSec = len * 0.45;
            clock.outWindowSec = len;
        }
    } else {
        clock.totalSec = 0.5;
    }
    return cardImage(renderText(s, text, canvas, clock), size);
}

QVariantMap galleryItem(const QString &id, const QString &label, const QImage &image)
{
    return QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("label"), label}, {QStringLiteral("image"), image}};
}

const TextAnimationSlot &slotOf(const TextStyle &s, int slot)
{
    return slot == 0 ? s.animation.in : slot == 1 ? s.animation.out : s.animation.loop;
}

} // namespace

TextSourceBackend::TextSourceBackend(QObject *parent)
    : QObject(parent)
{
    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(25);
    connect(m_renderTimer, &QTimer::timeout, this, &TextSourceBackend::updatePreview);

    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(33);
    connect(m_playTimer, &QTimer::timeout, this, &TextSourceBackend::renderPlayFrame);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(300);
    connect(m_pollTimer, &QTimer::timeout, this, &TextSourceBackend::onPollScriptOutput);
}

void TextSourceBackend::open(ClipGraphController *graph, quint64 nodeId)
{
    if (!graph || !graph->graph())
        return;
    m_graph = graph;
    m_nodeId = nodeId;

    const SourceDescriptor initial = graph->graph()->sourceDescriptor(nodeId);
    if (initial.kind == SourceDescriptor::Kind::Text) {
        m_template = initial.textTemplate;
        m_style = TextSource::styleFromDescriptor(initial);
        if (initial.canvasWidth > 0)
            m_canvasW = initial.canvasWidth;
        if (initial.canvasHeight > 0)
            m_canvasH = initial.canvasHeight;
    } else {
        m_template = QStringLiteral("Hello");
        m_style = TextStyle();
    }

    ScriptBinding binding;
    if (auto *app = AppController::instance())
        binding = app->scriptBindingFor(nodeId);
    setScriptBinding(binding);

    m_durDirty = true;
    emit templateChanged();
    rebuildAnimList();
    styleEdited();
}

QVariantMap TextSourceBackend::style() const
{
    return styleJson().toVariantMap();
}

QJsonObject TextSourceBackend::styleJson() const
{
    QJsonObject json = textStyleToJson(m_style);
    json.insert(QStringLiteral("layers"), StyleLayers::normalizedLayers(m_style.layers));
    return json;
}

QString TextSourceBackend::templateHtml() const
{
    static const QRegularExpression tokenRx(QStringLiteral("\\{([a-zA-Z_][a-zA-Z0-9_]*)\\}"));
    const bool connected = m_binding.connected();
    const QStringList names = knownVariableNames();
    const QSet<QString> known(names.cbegin(), names.cend());

    QString html;
    int last = 0;
    QRegularExpressionMatchIterator it = tokenRx.globalMatch(m_template);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        html += m_template.mid(last, m.capturedStart() - last).toHtmlEscaped();
        const QColor c = !connected ? m_mutedColor
                       : known.contains(m.captured(1)) ? m_knownColor
                       : m_unknownColor;
        html += QStringLiteral("<span style=\"color:%1; font-weight:600;\">%2</span>")
                    .arg(c.name(QColor::HexRgb), m.captured(0).toHtmlEscaped());
        last = m.capturedEnd();
    }
    html += m_template.mid(last).toHtmlEscaped();
    return QStringLiteral("<span style=\"white-space:pre-wrap;\">%1</span>").arg(html);
}

QString TextSourceBackend::scriptHint() const
{
    if (!m_binding.connected())
        return tr("Wire a Script node's ScriptOut port to this clip's DataIn port, then the script's "
                  "variables appear here as blocks. Click or drag a block into the template; "
                  "{placeholders} are replaced with live values.");
    if (knownVariableNames().isEmpty())
        return tr("The connected script hasn't produced any variables yet. "
                  "Press \"Run script\" or wait for its next run.");
    return {};
}

QVariantList TextSourceBackend::scriptVariables() const
{
    QVariantList out;
    for (const QString &name : knownVariableNames())
        out.append(QVariantMap{{QStringLiteral("name"), name},
                               {QStringLiteral("value"), displayValue(m_vars.value(name))}});
    return out;
}

void TextSourceBackend::syncLookSpecs()
{
    if (m_style.lookId == m_lookSpecsId)
        return;
    m_lookSpecsId = m_style.lookId;
    const TextLook *look = textLookForId(m_lookSpecsId);
    m_lookSpecs = look && !look->params.isEmpty() ? StyleLayers::specList(look->params, false) : QVariantList();
    emit lookSpecsChanged();
}

QVariantMap TextSourceBackend::lookValues() const
{
    const TextLook *look = textLookForId(m_style.lookId);
    if (!look)
        return {};
    QMap<QString, VectorSlotValue> values;
    for (const TextAnimParamSpec &spec : look->params)
        values.insert(spec.id, m_style.lookParams.value(spec.id, spec.defaultValue));
    return StyleLayers::plainValues(values);
}

void TextSourceBackend::setAnimSlot(int slot)
{
    if (slot < 0 || slot > 2 || slot == m_animSlot)
        return;
    m_animSlot = slot;
    rebuildAnimList();
}

void TextSourceBackend::setAnimCategory(const QString &category)
{
    if (category == m_animCategory)
        return;
    m_animCategory = category;
    rebuildAnimList();
}

QString TextSourceBackend::animPresetId() const
{
    return slotOf(m_style, m_animSlot).presetId;
}

QStringList TextSourceBackend::fontFamilies() const
{
    return QFontDatabase::families();
}

QVariantList TextSourceBackend::options(const QString &name) const
{
    const auto opt = [](const QString &value, const QString &label) {
        return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("label"), label}};
    };
    QVariantList out;
    if (name == QLatin1String("weight")) {
        const QList<QPair<int, QString>> weights = {{100, tr("Thin")}, {200, tr("ExtraLight")}, {300, tr("Light")},
                                                    {400, tr("Regular")}, {500, tr("Medium")}, {600, tr("SemiBold")},
                                                    {700, tr("Bold")}, {800, tr("ExtraBold")}, {900, tr("Black")}};
        for (const auto &w : weights)
            out.append(opt(QString::number(w.first), w.second));
    } else if (name == QLatin1String("align")) {
        out.append(opt(textAlignToString(TextAlign::Left), tr("Left")));
        out.append(opt(textAlignToString(TextAlign::Center), tr("Center")));
        out.append(opt(textAlignToString(TextAlign::Right), tr("Right")));
    } else if (name == QLatin1String("valign")) {
        out.append(opt(textVAlignToString(TextVAlign::Top), tr("Top")));
        out.append(opt(textVAlignToString(TextVAlign::Middle), tr("Middle")));
        out.append(opt(textVAlignToString(TextVAlign::Bottom), tr("Bottom")));
    } else if (name == QLatin1String("accentRule")) {
        out.append(opt(wordAccentRuleToString(WordAccentRule::None), tr("None")));
        out.append(opt(wordAccentRuleToString(WordAccentRule::FirstWord), tr("First word")));
        out.append(opt(wordAccentRuleToString(WordAccentRule::LastWord), tr("Last word")));
        out.append(opt(wordAccentRuleToString(WordAccentRule::EveryOther), tr("Every other word")));
        out.append(opt(wordAccentRuleToString(WordAccentRule::EveryNth), tr("Every Nth word")));
        out.append(opt(wordAccentRuleToString(WordAccentRule::LongestWord), tr("Longest word")));
        out.append(opt(wordAccentRuleToString(WordAccentRule::RandomStable), tr("Random words")));
    }
    return out;
}

void TextSourceBackend::setTemplate(const QString &text)
{
    m_template = text;
    m_durDirty = true;
    emit templateChanged();
    scheduleRender();
}

void TextSourceBackend::setHighlightColors(const QColor &known, const QColor &unknown, const QColor &muted)
{
    m_knownColor = known;
    m_unknownColor = unknown;
    m_mutedColor = muted;
    emit templateChanged();
}

void TextSourceBackend::edit(const QVariantMap &patch)
{
    QJsonObject merged = StyleLayers::mergePatch(styleJson(), QJsonObject::fromVariantMap(patch));
    if (!patch.contains(QStringLiteral("packId")))
        merged.insert(QStringLiteral("packId"), QString());
    // A deliberately empty stack survives; the JSON reader would otherwise restore a white fill.
    const QList<TextShadingLayer> layers = m_style.layers;
    m_style = textStyleFromJson(merged);
    if (!patch.contains(QStringLiteral("layers")))
        m_style.layers = layers;
    styleEdited();
}

void TextSourceBackend::setLayers(const QVariantList &layers)
{
    m_style.layers = StyleLayers::parseLayers(QJsonArray::fromVariantList(layers));
    m_style.lookId.clear();
    m_style.lookParams.clear();
    styleEdited();
}

void TextSourceBackend::applyLook(const QString &id)
{
    if (!applyTextLook(m_style, id, {}))
        return;
    m_style.packId.clear();
    styleEdited();
}

void TextSourceBackend::setLookParam(const QString &id, const QVariant &value)
{
    const QString lookId = m_style.lookId;
    const TextLook *look = textLookForId(lookId);
    if (!look)
        return;
    const TextAnimParamSpec *spec = nullptr;
    for (const TextAnimParamSpec &p : look->params) {
        if (p.id == id)
            spec = &p;
    }
    if (!spec)
        return;
    QMap<QString, VectorSlotValue> params = m_style.lookParams;
    params.insert(id, paramValueFromJson(spec->type, QJsonValue::fromVariant(value)));
    applyTextLook(m_style, lookId, params);
    m_style.packId.clear();
    styleEdited();
}

void TextSourceBackend::applyStylePack(const QString &id)
{
    const auto st = textStyleForPresetId(id);
    if (!st)
        return;
    m_style = *st;
    m_style.packId = id;
    m_durDirty = true;
    emit styleChanged();
    syncLookSpecs();
    syncAnimTab();
    scheduleRender();
}

void TextSourceBackend::refreshPacks()
{
    const QString family = m_style.fontFamily;
    const int weight = m_style.fontWeight;
    const bool italic = m_style.italic;

    QVariantList user;
    for (const TextPreset &p : TextPresetStore::instance().presets()) {
        user.append(galleryItem(p.id, p.label, cachedThumb(QStringLiteral("pack|%1").arg(p.id), [p] {
            return packThumb(p, QSize(120, 54));
        })));
    }
    QVariantList builtin;
    for (const TextPreset &p : textPresets()) {
        builtin.append(galleryItem(p.id, p.label, cachedThumb(QStringLiteral("pack|%1").arg(p.id), [p] {
            return packThumb(p, QSize(120, 54));
        })));
    }
    QVariantList looks;
    for (const TextLook &look : textLooks()) {
        const QString key = QStringLiteral("look|%1|%2|%3|%4").arg(look.id, family).arg(weight).arg(italic);
        looks.append(galleryItem(look.id, look.label, cachedThumb(key, [id = look.id, family, weight, italic] {
            return lookThumb(id, family, weight, italic, QSize(72, 40));
        })));
    }
    m_userPacks = user;
    m_builtinPacks = builtin;
    m_lookThumbs = looks;
    emit packsChanged();
}

bool TextSourceBackend::saveStylePreset(const QString &name)
{
    const QString label = name.trimmed();
    if (label.isEmpty())
        return false;
    QString sample = m_template.simplified().left(40);
    if (sample.isEmpty())
        sample = tr("Your text here");
    const QString id = TextPresetStore::instance().add(label, m_style, sample);
    if (id.isEmpty())
        return false;
    m_style.packId = id;
    refreshPacks();
    emit styleChanged();
    return true;
}

QString TextSourceBackend::packLabel(const QString &id) const
{
    for (const QVariant &v : m_userPacks) {
        const QVariantMap item = v.toMap();
        if (item.value(QStringLiteral("id")).toString() == id)
            return item.value(QStringLiteral("label")).toString();
    }
    return {};
}

void TextSourceBackend::renamePack(const QString &id, const QString &label)
{
    const QString name = label.trimmed();
    if (name.isEmpty())
        return;
    if (TextPresetStore::instance().rename(id, name))
        refreshPacks();
}

void TextSourceBackend::deletePack(const QString &id)
{
    TextPresetStore::instance().remove(id);
    if (m_style.packId == id)
        m_style.packId.clear();
    refreshPacks();
    emit styleChanged();
}

void TextSourceBackend::applyAnimationPreset(const QString &presetId)
{
    TextAnimationSlot fresh;
    fresh.presetId = presetId;
    slotRef(m_animSlot) = fresh;
    styleEdited();
}

void TextSourceBackend::setAnimParam(const QString &id, const QVariant &value)
{
    const auto preset = TextAnimationPresetCatalog::instance().presetForId(slotOf(m_style, m_animSlot).presetId);
    if (!preset)
        return;
    const TextAnimParamSpec *spec = preset->param(id);
    if (!spec)
        return;
    slotRef(m_animSlot).params.insert(id, paramValueFromJson(spec->type, QJsonValue::fromVariant(value)));
    styleEdited();
}

void TextSourceBackend::setPlaying(bool on)
{
    if (on == m_playing)
        return;
    m_playing = on;
    if (on) {
        refreshDurations();
        m_playClock.restart();
        m_playTimer->start();
        renderPlayFrame();
    } else {
        m_playTimer->stop();
        renderHeld();
    }
    emit playingChanged();
}

void TextSourceBackend::runScript()
{
    if (m_binding.requestRun)
        m_binding.requestRun();
}

bool TextSourceBackend::commit()
{
    if (!m_graph || !m_graph->graph() || m_template.trimmed().isEmpty())
        return false;
    const SourceDescriptor desc = currentDescriptor();
    if (m_nodeId == 0) {
        auto *app = AppController::instance();
        return app && app->addSourceFromDescriptor(desc, ThumbHelper::makeTextThumb(desc)) != 0;
    }
    return m_graph->setSourceDescriptor(m_nodeId, desc);
}

void TextSourceBackend::styleEdited()
{
    m_durDirty = true;
    emit styleChanged();
    syncLookSpecs();
    syncAnimTab();
    scheduleRender();
}

QImage TextSourceBackend::cachedThumb(const QString &key, const std::function<QImage()> &render)
{
    const auto hit = m_thumbCache.constFind(key);
    if (hit != m_thumbCache.constEnd())
        return hit.value();
    const QImage image = render();
    m_thumbCache.insert(key, image);
    return image;
}

void TextSourceBackend::rebuildAnimList()
{
    const TextAnimSlotKind kind = m_animSlot == 0 ? TextAnimSlotKind::In
                                : m_animSlot == 1 ? TextAnimSlotKind::Out : TextAnimSlotKind::Loop;
    const QList<TextAnimationPreset> presets = TextAnimationPresetCatalog::instance().presetsFor(kind);

    QStringList seen;
    for (const TextAnimationPreset &p : presets) {
        if (!p.category.isEmpty() && !seen.contains(p.category))
            seen << p.category;
    }
    QVariantList categories{QVariantMap{{QStringLiteral("value"), QString()}, {QStringLiteral("label"), tr("All")}}};
    for (const QString &category : seen)
        categories.append(QVariantMap{{QStringLiteral("value"), category},
                                      {QStringLiteral("label"), StyleLayers::prettyId(category)}});
    m_animCategories = categories;
    m_animCategoriesVisible = seen.size() > 1;
    if (!m_animCategory.isEmpty() && !seen.contains(m_animCategory))
        m_animCategory.clear();

    QVariantList items{galleryItem(QString(), tr("None"), QImage())};
    for (const TextAnimationPreset &p : presets) {
        if (!m_animCategory.isEmpty() && p.category != m_animCategory)
            continue;
        const int slot = m_animSlot;
        items.append(galleryItem(p.id, p.label, cachedThumb(QStringLiteral("anim|%1|%2").arg(slot).arg(p.id), [slot, p] {
            return animThumb(slot, p, QSize(120, 68));
        })));
    }
    m_animPresets = items;
    syncAnimTab();
    emit animListChanged();
}

void TextSourceBackend::syncAnimTab()
{
    const TextAnimationSlot &slot = slotOf(m_style, m_animSlot);
    const auto preset = TextAnimationPresetCatalog::instance().presetForId(slot.presetId);
    const QString specsKey = QStringLiteral("%1|%2").arg(m_animSlot).arg(slot.presetId);

    m_animCustom = slot.presetId.isEmpty() && !slot.animators.isEmpty();
    m_animNote = m_animCustom ? tr("Custom animation set outside the editor. Preset controls are disabled.")
                              : QString();
    m_animValues.clear();
    m_animDisabled.clear();
    if (specsKey != m_animSpecsKey) {
        m_animSpecsKey = specsKey;
        m_animSpecs.clear();
        if (preset) {
            QList<TextAnimParamSpec> specs;
            for (const TextAnimParamSpec &s : preset->params) {
                if ((s.id == QLatin1String("unit") && preset->flag(QStringLiteral("unitLocked")))
                    || (s.id == QLatin1String("order") && preset->flag(QStringLiteral("orderLocked")))
                    || (s.id == QLatin1String("duration") && preset->flag(QStringLiteral("durationLocked")))
                    || (s.id == QLatin1String("ease") && preset->flag(QStringLiteral("easeLocked"))))
                    continue;
                specs.append(s);
            }
            m_animSpecs = StyleLayers::specList(specs, false);
        }
        emit animSpecsChanged();
    }
    if (preset) {
        QMap<QString, VectorSlotValue> values = preset->defaultParams();
        for (auto it = slot.params.cbegin(); it != slot.params.cend(); ++it)
            values.insert(it.key(), it.value());

        m_animValues = StyleLayers::plainValues(values);
        const bool block = values.value(QStringLiteral("unit"), VectorSlotValue::fromText(QStringLiteral("block")))
                               .text == QLatin1String("block");
        if (block)
            m_animDisabled = {QStringLiteral("stagger"), QStringLiteral("order")};
    }
    emit animChanged();
}

void TextSourceBackend::scheduleRender()
{
    if (m_playing)
        return;
    if (!m_renderTimer->isActive())
        m_renderTimer->start();
}

void TextSourceBackend::updatePreview()
{
    if (m_playing)
        renderPlayFrame();
    else
        renderHeld();
}

void TextSourceBackend::renderHeld()
{
    m_preview = TextSource::renderDescriptor(currentDescriptor(), resolvedText());
    emit previewChanged();
}

void TextSourceBackend::renderPlayFrame()
{
    using Phase = TextAnimClock::Phase;
    if (m_durDirty)
        refreshDurations();
    constexpr double kHold = 1.0;
    constexpr double kGap = 0.3;
    const double cycle = m_inSec + kHold + m_outSec + (m_outSec > 0 ? kGap : 0.0);
    const double t = std::fmod(m_playClock.elapsed() / 1000.0, cycle);

    TextAnimClock clock;
    clock.totalSec = t;
    if (t < m_inSec) {
        clock.phase = Phase::In;
        clock.phaseElapsedSec = t;
    } else if (t >= m_inSec + kHold && m_outSec > 0) {
        clock.phase = Phase::Out;
        clock.phaseElapsedSec = qMin(t - m_inSec - kHold, m_outSec);
        clock.outWindowSec = m_outSec;
    }
    m_preview = renderText(m_style, resolvedText(), QSize(m_canvasW, m_canvasH), clock)
                    .convertToFormat(QImage::Format_RGBA8888);
    emit previewChanged();
}

void TextSourceBackend::refreshDurations()
{
    const QString text = resolvedText();
    const QSize canvas(m_canvasW, m_canvasH);
    m_inSec = textAnimationInSeconds(m_style, text, canvas);
    m_outSec = textAnimationOutSeconds(m_style, text, canvas);
    m_durDirty = false;
}

QString TextSourceBackend::resolvedText() const
{
    const QString tmpl = m_template.trimmed();
    return m_binding.connected() ? TextSource::substitutePlaceholders(tmpl, m_vars) : tmpl;
}

SourceDescriptor TextSourceBackend::currentDescriptor() const
{
    SourceDescriptor desc;
    desc.kind = SourceDescriptor::Kind::Text;
    desc.textTemplate = m_template.trimmed();
    desc.textStyleJson = TextSource::styleToJson(m_style);
    desc.canvasWidth = m_canvasW;
    desc.canvasHeight = m_canvasH;

    const QString flat = desc.textTemplate.simplified();
    desc.displayName = flat.length() > 24 ? flat.left(21) + QStringLiteral("…") : flat;
    return desc;
}

TextAnimationSlot &TextSourceBackend::slotRef(int slot)
{
    return slot == 0 ? m_style.animation.in : slot == 1 ? m_style.animation.out : m_style.animation.loop;
}

void TextSourceBackend::setScriptBinding(const ScriptBinding &binding)
{
    m_binding = binding;
    m_vars = QJsonObject();
    m_declaredVars.clear();
    if (!m_binding.connected()) {
        m_pollTimer->stop();
        emit scriptChanged();
        emit templateChanged();
        updatePreview();
        return;
    }

    m_declaredVars = luaReturnTableKeys(m_binding.code);
    {
        QMutexLocker lock(&m_binding.output->mutex);
        const QJsonDocument doc = QJsonDocument::fromJson(m_binding.output->json.toUtf8());
        if (doc.isObject())
            m_vars = doc.object();
    }
    m_lastSeenVersion = m_binding.output->version.load(std::memory_order_acquire);
    m_pollTimer->start();
    emit scriptChanged();
    emit templateChanged();
    updatePreview();
}

QStringList TextSourceBackend::knownVariableNames() const
{
    QStringList names = m_declaredVars;
    const QStringList jsonKeys = m_vars.keys();
    for (const QString &key : jsonKeys) {
        if (!names.contains(key))
            names << key;
    }
    return names;
}

void TextSourceBackend::onPollScriptOutput()
{
    if (!m_binding.connected())
        return;
    const uint ver = m_binding.output->version.load(std::memory_order_acquire);
    if (ver == m_lastSeenVersion)
        return;
    m_lastSeenVersion = ver;

    QString json;
    {
        QMutexLocker lock(&m_binding.output->mutex);
        json = m_binding.output->json;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    m_vars = doc.isObject() ? doc.object() : QJsonObject();

    m_durDirty = true;
    emit scriptChanged();
    emit templateChanged();
    updatePreview();
}
