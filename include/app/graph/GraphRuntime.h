#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <QtNumeric>
#include <memory>

#include "core/graph/ClipGraph.h"
#include "core/scripting/ScriptOutput.h"

class BpmClock;
class QQuickImageProvider;
class QTimer;
struct NodeThumbStore;

/// Runtime half of the node graph: everything ClipNodeEditor did beyond holding the model.
/// Owns the per-node script engines (Script / Audio Analyzer), merges script outputs
/// into DataIn feeds, drives audio-analyzer nodes from the
/// decks, and caches node thumbnails for QML (image://nodethumb/<id>).
/// It follows the ClipGraph's nodeAdded/nodeRemoved/nodeChanged/graphReset signals to
/// create, update and destroy its per-node runtime objects.
class GraphRuntime : public QObject {
    Q_OBJECT
public:
    /// @p thumbs is shared between the runtimes of a show so one image provider serves every scene.
    explicit GraphRuntime(ClipGraph *graph, QObject *parent = nullptr,
                          std::shared_ptr<NodeThumbStore> thumbs = {});
    static std::shared_ptr<NodeThumbStore> createThumbStore();
    /// An inactive runtime stops polling and pauses its Lua scripts and expressions; activating restarts them.
    void setActive(bool active);
    bool isActive() const;
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
    static QQuickImageProvider *createImageProvider(std::shared_ptr<NodeThumbStore> thumbs);

    // ── Script / data runtime ─────────────────────────────────────────────────
    /// One poll step (script-node input feeds, merged feeds). Driven by an
    /// internal timer; public for tests.
    void pollDataConsumers();
    QString mergeScriptInputs(const QVector<NodeId> &producers, uint &verSum) const;
    std::shared_ptr<ScriptOutput> scriptOutputForDataNode(NodeId dataNodeId) const;
    /// What ClipNodeEditor::wireTextScriptBinding installed as ClipCard::scriptBindingProvider.
    ScriptBinding scriptBindingFor(NodeId dataNodeId) const;
    /// "Run now" on a Script node. False when the node is not a Script.
    bool runScript(NodeId scriptNodeId);
    /// Last run's error of a Script node, empty when it succeeded.
    QString scriptError(NodeId scriptNodeId) const;
    /// The node's print/prism.log output, newest last (at most ScriptRuntime::kLogLines).
    QStringList scriptLog(NodeId scriptNodeId) const;
    /// Error of the last evaluation of node @p id's expression for param @p name, empty when fine.
    QString expressionError(NodeId id, const QString &name) const;
    /// Waits (pumping events) until script threads from deleted runtimes or nodes have ended.
    static bool waitForRetiredScripts(int timeoutMs);

    // ── Modulators ────────────────────────────────────────────────────────────
    /// The global tempo clock; its bpm and beat position feed Modulator nodes, expressions and Lua.
    void setBpmClock(BpmClock *clock);
    /// Last value of a Modulator node (0 when unknown); refreshed by pollDataConsumers.
    double modulatorValue(NodeId id) const;

    // ── Audio analyzers ─────────────────────────────────────────────────────────
    void feedLiveAudioForProducer(NodeId producerId, const QByteArray &pcm);
    void syncAudioAnalyzerNode(NodeId scriptNodeId, double playbackTime, bool playing, double speed);
    void syncAllAudioAnalyzers(NodeId liveClip, double timeLive, bool playingLive, double speedLive,
                             NodeId stagingClip, double timeStaging, bool playingStaging, double speedStaging);

signals:
    /// A script node's error or log, or an expression error, changed. id 0 means re-read everything.
    void scriptStatusChanged(NodeId id);
    /// id 0 means every thumbnail was dropped (graph reset).
    void thumbnailChanged(NodeId id);

private:
    void requestThumbnail(NodeId id);
    void writeParam(NodeId id, const QString &name, const QVariant &value);
    void applyScriptWrites();
    struct Impl;
    std::unique_ptr<Impl> d;
    ClipGraph *m_graph;
};
