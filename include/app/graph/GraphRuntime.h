#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QtNumeric>
#include <memory>

#include "core/graph/ClipGraph.h"
#include "core/scripting/ScriptOutput.h"

class QQuickImageProvider;
class QTimer;

/// Runtime half of the node graph: everything ClipNodeEditor did beyond holding the model.
/// Owns the per-node script engines (Script / Audio Script / Trigger), merges script outputs
/// into DataIn feeds, applies switcher script results, drives audio-script nodes from the
/// decks, and caches node thumbnails for QML (image://nodethumb/<id>).
/// It follows the ClipGraph's nodeAdded/nodeRemoved/nodeChanged/graphReset signals to
/// create, update and destroy its per-node runtime objects.
class GraphRuntime : public QObject {
    Q_OBJECT
public:
    explicit GraphRuntime(ClipGraph *graph, QObject *parent = nullptr);
    ~GraphRuntime() override;

    ClipGraph *graph() const { return m_graph; }

    // ── Node creation (ClipNodeEditor::addClipNode / addSourceNode) ───────────
    /// Probes the file for audio, creates the Input node (graph->addInputNode) and caches the
    /// thumbnail (the supplied one, else ThumbnailExtractor). NaN pos = staggered placement.
    /// Returns 0 on failure.
    NodeId addClipNode(const QString &path, const QImage &thumbnail = {},
                       const QPointF &pos = QPointF(qQNaN(), qQNaN()));
    NodeId addSourceNode(const SourceDescriptor &desc, const QImage &thumbnail = {},
                         const QPointF &pos = QPointF(qQNaN(), qQNaN()));

    // ── Thumbnails ────────────────────────────────────────────────────────────
    QImage thumbnail(NodeId id) const;
    void setThumbnail(NodeId id, const QImage &image);
    /// Bumped on every setThumbnail; QML appends it as a cache-buster (?v=<rev>).
    int thumbnailRevision(NodeId id) const;
    /// Generates thumbnails on the thread pool for every Input node that lacks one.
    void generateMissingThumbnails();
    /// For image://nodethumb/<id>. Ownership passes to the caller (QQmlEngine::addImageProvider).
    /// The provider keeps working only while this GraphRuntime is alive and reads it thread-safely.
    QQuickImageProvider *createImageProvider();

    // ── Script / data runtime ─────────────────────────────────────────────────
    /// One poll step (script-node input feeds, merged feeds, switcher scripts). Driven by an
    /// internal timer; public for tests.
    void pollDataConsumers();
    QString mergeScriptInputs(const QVector<NodeId> &producers, uint &verSum) const;
    std::shared_ptr<ScriptOutput> scriptOutputForDataNode(NodeId dataNodeId) const;
    /// What ClipNodeEditor::wireTextScriptBinding installed as ClipCard::scriptBindingProvider.
    ScriptBinding scriptBindingFor(NodeId dataNodeId) const;
    /// "Run now" on a Script node / "Pulse now" on a Trigger node. False when the node is not that kind.
    bool runScript(NodeId scriptNodeId);
    bool pulseTrigger(NodeId triggerNodeId);

    // ── Audio scripts ─────────────────────────────────────────────────────────
    void feedLiveAudioForProducer(NodeId producerId, const QByteArray &pcm);
    void syncAudioScriptNode(NodeId scriptNodeId, double playbackTime, bool playing, double speed);
    void syncAllAudioScripts(NodeId liveClip, double timeLive, bool playingLive, double speedLive,
                             NodeId stagingClip, double timeStaging, bool playingStaging, double speedStaging);

signals:
    /// id 0 means every thumbnail was dropped (graph reset).
    void thumbnailChanged(NodeId id);

private:
    void requestThumbnail(NodeId id);
    struct Impl;
    std::unique_ptr<Impl> d;
    ClipGraph *m_graph;
};
