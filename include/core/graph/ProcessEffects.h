#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

class MediaSource;
struct ResolvedLayer;
struct SourceEffectAux;

/// One decorator effect to apply to a layer's MediaSource, as resolved from the
/// node graph (ordered upstream → downstream).
struct SourceEffectRef {
    int effectId = -1;
    QJsonObject params;
    /// Runtime-only (not persisted): the streams wired to the effect's extra input ports, in port
    /// order. A null entry is an unconnected port. Defined in ClipGraph.h.
    QVector<std::shared_ptr<SourceEffectAux>> aux;
};

/// Builds the MediaSource for one resolved aux stream (null when it cannot be produced).
using AuxBuilder = std::function<std::unique_ptr<MediaSource>(const SourceEffectAux &)>;
/// Pushes changed params into an already-built aux source; false means it needs a rebuild.
using AuxUpdater = std::function<bool(MediaSource *, const SourceEffectAux &)>;

/// Everything the node editor, graph evaluator, and source builder need to know
/// about one process effect. Adding a new effect means adding one entry to the
/// registry in ProcessEffects.cpp (plus a MediaSource decorator class for
/// decorator-family effects).
struct ProcessEffectDescriptor {
    int     id = -1;          // persisted in saved projects; stable, append-only
    QString name;             // node body label
    QString menuLabel;        // "Add Process Node" menu entry
    QString slug;             // explicit lookup slug; empty means derived from name
    bool    available = true; // hides the menu entry only; never blocks project load
    QJsonObject defaultParams;

    /// Fold-family effects mutate the accumulated ResolvedLayer during graph
    /// evaluation (crop/flip). Null for decorator-family effects.
    std::function<void(ResolvedLayer &layer, const QJsonObject &params)> fold;

    /// Decorator-family effects wrap the layer's MediaSource at build time.
    /// Null for fold-family effects, and when the effect is compiled out (the
    /// effect ref still round-trips through save/load; wrapping is a no-op).
    std::function<std::unique_ptr<MediaSource>(std::unique_ptr<MediaSource> inner,
                                               const QJsonObject &params)> wrapSource;
    bool isDecorator = false; // true even when wrapSource is compiled out

    /// Text of the node's edit affordance; empty means the node has none.
    QString editLabel;

    /// Optional: overrides the node body's edit-button text with the current
    /// param state (e.g. "Vertical" instead of the static "Direction"). Falls
    /// back to editLabel when unset.
    std::function<QString(const QJsonObject &params)> dynamicLabel;
};

namespace ProcessEffects {

const QVector<ProcessEffectDescriptor> &all();
const ProcessEffectDescriptor *byId(int id);
/// Match numeric id, name, menu label, or slug ("chroma_key"). Case-insensitive.
const ProcessEffectDescriptor *byName(const QString &name);

/// Applies each decorator effect's wrapSource in order; unknown or compiled-out
/// effects pass through unchanged.
std::unique_ptr<MediaSource> applySourceEffects(std::unique_ptr<MediaSource> source,
                                                const QVector<SourceEffectRef> &effects,
                                                const AuxBuilder &auxBuilder = {});

/// Structure-only identity for deck-reuse keys: the ordered effect ids, no param values. The wiring of
/// aux inputs is not part of it (it needs the graph); the caller appends that.
QString sourceEffectsKey(const QVector<SourceEffectRef> &effects);

/// The param values of the same effects. Equal structure keys with different param keys
/// can be reconciled with updateSourceEffects instead of a rebuild.
QString sourceEffectsParamsKey(const QVector<SourceEffectRef> &effects);

/// Pushes each effect's params into the decorator chain built by applySourceEffects (`top` is the
/// outermost decorator). Returns the source below the chain, or null when any decorator cannot
/// take the update in place (caller must rebuild; some decorators may already have been updated).
MediaSource *updateSourceEffects(MediaSource *top, const QVector<SourceEffectRef> &effects,
                                 const AuxUpdater &auxUpdater = {});

} // namespace ProcessEffects
