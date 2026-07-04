#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

class MediaSource;
class QImage;
class QWidget;
struct ResolvedLayer;

/// One decorator effect to apply to a layer's MediaSource, as resolved from the
/// node graph (ordered upstream → downstream).
struct SourceEffectRef {
    int effectId = -1;
    QJsonObject params;
};

/// Everything the node editor, graph evaluator, and source builder need to know
/// about one process effect. Adding a new effect means adding one entry to the
/// registry in ProcessEffects.cpp (plus a MediaSource decorator class for
/// decorator-family effects, and optionally an edit dialog).
struct ProcessEffectDescriptor {
    int     id = -1;          // persisted in saved projects; stable, append-only
    QString name;             // node body label
    QString menuLabel;        // "Add Process Node" menu entry
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

    /// Optional parameter editor; null means the node has no edit affordances.
    /// Shows @p referenceFrame (may be null) as preview, mutates @p params and
    /// returns true on accept.
    QString editLabel;
    std::function<bool(QWidget *parent, QJsonObject &params,
                       const QImage &referenceFrame)> editDialog;
};

namespace ProcessEffects {

const QVector<ProcessEffectDescriptor> &all();
const ProcessEffectDescriptor *byId(int id);

/// Applies each decorator effect's wrapSource in order; unknown or compiled-out
/// effects pass through unchanged.
std::unique_ptr<MediaSource> applySourceEffects(std::unique_ptr<MediaSource> source,
                                                const QVector<SourceEffectRef> &effects);

/// Identity string for deck-reuse keys: same effects + params ⇒ same key.
QString sourceEffectsKey(const QVector<SourceEffectRef> &effects);

} // namespace ProcessEffects
