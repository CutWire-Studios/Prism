#pragma once

#include <functional>
#include <memory>
#include "core/sources/MediaSource.h"
#include "core/sources/SourceDescriptor.h"
#include "app/graph/GraphRuntime.h"
#include "core/graph/ClipGraph.h"
#include "core/render/Compositor.h"

/// Factory that creates MediaSource instances from a SourceDescriptor.
/// Centralises the duplicated switch-statements that existed both in
/// DeckController::assignNodeToDeck() and the node-chain builder.
class SourceFactory {
public:
    SourceFactory() = delete;

    /// Builds the final composite of a scene as a source; null when the scene is missing or empty.
    /// The app installs it so Scene sources can reach the show without core depending on it.
    using SceneResolver = std::function<std::unique_ptr<MediaSource>(const QString &sceneId)>;
    static void setSceneResolver(SceneResolver resolver);
    /// Pushes changed effect params into an already-built scene content tree (the one the resolver
    /// returned for `sceneId`). False means it cannot be done in place.
    using SceneParamUpdater = std::function<bool(const QString &sceneId, MediaSource *content)>;
    static void setSceneParamUpdater(SceneParamUpdater updater);
    /// Scene sources nested deeper than this render empty.
    static constexpr int kMaxSceneDepth = 4;

    /// What a Scene input node (see SourceDescriptor::Kind::SceneInput) shows when no deck binds it:
    /// the app installs a builder for the Live scene's output and a matching in-place param updater.
    /// With no content the node presents a transparent frame.
    using SceneInputBuilder = std::function<std::unique_ptr<MediaSource>()>;
    using SceneInputUpdater = std::function<bool(MediaSource *content)>;
    static void setSceneInputFallback(SceneInputBuilder builder, SceneInputUpdater updater);

    /// While alive, Scene input nodes built through create() take the builder's source directly as
    /// their output (the deck then paces it, so transport still reaches the wrapped chain), and
    /// updateLayerParams() hands the updater that source to refresh. Scopes nest; the inner one wins.
    class SceneInputScope {
    public:
        SceneInputScope(SceneInputBuilder builder, SceneInputUpdater updater);
        ~SceneInputScope();
        SceneInputScope(const SceneInputScope &) = delete;
        SceneInputScope &operator=(const SceneInputScope &) = delete;

    private:
        SceneInputBuilder m_prevBuilder;
        SceneInputUpdater m_prevUpdater;
    };

    /// Create a ready-to-play MediaSource from a descriptor.
    /// Returns nullptr if the descriptor cannot produce a source
    /// (e.g. transparent canvas, bad file path).
    static std::unique_ptr<MediaSource> create(const SourceDescriptor &desc);

    /// Build a NodeChainSource entry for one resolved layer.
    static Compositor::NodeChainSource makeLayerEntry(const ClipGraph::ResolvedLayer &layer,
                                                      ClipGraph *graph, GraphRuntime *runtime);

    /// Build the MediaSource for one resolved layer, applying its ordered effects.
    /// Handles both ordinary input layers and flattened Layer-node composites
    /// (recursively). Returns nullptr if the source cannot be produced.
    static std::unique_ptr<MediaSource> buildLayerSource(const ClipGraph::ResolvedLayer &layer,
                                                         ClipGraph *graph, GraphRuntime *runtime);

    /// Pushes a layer's current effect params (and those of any nested scene) into `top`, the
    /// source buildLayerSource produced for the same layer structure. False means rebuild.
    static bool updateLayerParams(MediaSource *top, const ClipGraph::ResolvedLayer &layer,
                                  ClipGraph *graph);

    /// Callbacks that build / update the aux streams wired to a shader effect's extra input ports.
    static AuxBuilder auxBuilder(ClipGraph *graph, GraphRuntime *runtime);
    static AuxUpdater auxUpdater(ClipGraph *graph);
    /// Points every Shader process node in the decorator chain @p top at its data feed.
    static void attachEffectData(MediaSource *top, GraphRuntime *runtime);

    /// Build the full deck stream (index 0 = bottom/deck-primary, 1..N = overlays).
    static std::vector<Compositor::NodeChainSource>
    buildStream(const ClipGraph::ResolvedStream &stream, ClipGraph *graph, GraphRuntime *runtime);
};
