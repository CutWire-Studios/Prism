#include "nodes/NodeCatalog.h"

#include "core/graph/ClipGraph.h"

#include "core/audio/AudioRack.h"
#include "core/audio/dsp/PedalCatalog.h"

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
