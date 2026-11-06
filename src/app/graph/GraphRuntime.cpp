#include "app/graph/GraphRuntime.h"

#include "core/media/AudioAnalyzer.h"
#include "core/media/MediaProbe.h"
#include "core/media/ThumbnailExtractor.h"
#include "core/project/ClipManager.h"
#include "core/scripting/ScriptRuntime.h"
#include "core/media/AudioLoopbackEnumerator.h"

#include <QDate>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QtQuick/QQuickImageProvider>

#include <algorithm>
#include <cmath>
#include <map>

namespace {

using NT = ClipGraph::NodeType;

constexpr int kPollIntervalMs = 33;

struct ThumbState {
    QMutex mutex;
    QHash<NodeId, QImage> images;
    QHash<NodeId, int> revisions;
};

class NodeThumbProvider : public QQuickImageProvider {
public:
    explicit NodeThumbProvider(std::shared_ptr<ThumbState> state)
        : QQuickImageProvider(QQuickImageProvider::Image), m_state(std::move(state)) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override {
        QString key = id;
        const int q = key.indexOf(QLatin1Char('?'));
        if (q >= 0)
            key.truncate(q);
        bool ok = false;
        const NodeId nodeId = key.toULongLong(&ok);

        QImage image;
        if (ok) {
            QMutexLocker lock(&m_state->mutex);
            image = m_state->images.value(nodeId);
        }
        if (image.isNull()) {
            image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
        }
        if (size)
            *size = image.size();
        if (requestedSize.isValid() && requestedSize != image.size())
            return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return image;
    }

private:
    std::shared_ptr<ThumbState> m_state;
};

void publishJson(ScriptOutput &out, const QJsonObject &obj) {
    const QString json = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    {
        QMutexLocker lock(&out.mutex);
        out.json = json;
    }
    out.version.fetch_add(1, std::memory_order_release);
}

AudioAnalyzerConfig analyzerConfigFrom(const QVariantMap &p) {
    AudioAnalyzerConfig c;
    c.fftSize = p.value("fftSize", c.fftSize).toInt();
    c.binCount = p.value("binCount", c.binCount).toInt();
    c.smoothing = p.value("smoothing", double(c.smoothing)).toFloat();
    c.beatSensitivity = p.value("beatSensitivity", double(c.beatSensitivity)).toFloat();
    c.lowMidHz = p.value("lowMidHz", double(c.lowMidHz)).toFloat();
    c.midHighHz = p.value("midHighHz", double(c.midHighHz)).toFloat();
    return c;
}

bool sameConfig(const AudioAnalyzerConfig &a, const AudioAnalyzerConfig &b) {
    return a.fftSize == b.fftSize && a.binCount == b.binCount && a.smoothing == b.smoothing
        && a.beatSensitivity == b.beatSensitivity && a.lowMidHz == b.lowMidHz
        && a.midHighHz == b.midHighHz;
}

struct ScriptNode {
    enum class TrigMode { Interval = 0, TimeOfDay = 1, Manual = 2 };

    NT type = NT::Script;
    std::shared_ptr<ScriptOutput> output = std::make_shared<ScriptOutput>();

    // Lua script
    QThread *thread = nullptr;
    ScriptRuntime *runtime = nullptr;
    QString code;
    int triggerMode = 0;
    int intervalMs = 1000;

    // Audio script
    std::unique_ptr<AudioAnalyzer> analyzer;
    AudioAnalyzerConfig config;
    QString audioPath;
    bool hasAudio = false;
    bool liveSource = false;
    QElapsedTimer clock;
    qint64 lastMs = 0;
    qint64 lastSignalMs = 0;

    // Trigger
    std::unique_ptr<QTimer> trigTimer;
    TrigMode trigMode = TrigMode::Interval;
    int trigInterval = 1000;
    QTime trigTime = QTime(12, 0);
    qint64 tickCount = 0;
    QDate lastFireDate;

    explicit ScriptNode(NT t) : type(t) {
        if (type == NT::Script) {
            thread = new QThread();
            runtime = new ScriptRuntime(output);
            runtime->moveToThread(thread);
            thread->start();
        } else if (type == NT::AudioScript) {
            analyzer = std::make_unique<AudioAnalyzer>();
            clock.start();
        } else if (type == NT::Trigger) {
            trigTimer = std::make_unique<QTimer>();
            QObject::connect(trigTimer.get(), &QTimer::timeout, trigTimer.get(), [this]() { onTrigTick(); });
            applyTiming();
        }
    }

    ~ScriptNode() {
        if (runtime && thread) {
            if (thread->isRunning()) {
                QMetaObject::invokeMethod(runtime, "shutdown", Qt::BlockingQueuedConnection);
                QMetaObject::invokeMethod(runtime, "deleteLater", Qt::BlockingQueuedConnection);
                thread->quit();
                thread->wait();
            } else {
                delete runtime;
            }
            runtime = nullptr;
        }
        delete thread;
        thread = nullptr;
    }

    void applyScript(const QString &c, int mode, int interval) {
        code = c;
        triggerMode = mode;
        intervalMs = interval;
        if (runtime) {
            QMetaObject::invokeMethod(runtime, "applySettings", Qt::QueuedConnection,
                                      Q_ARG(QString, code), Q_ARG(int, triggerMode),
                                      Q_ARG(int, intervalMs));
        }
    }

    void setInputJson(const QString &json) {
        if (runtime)
            QMetaObject::invokeMethod(runtime, "setInput", Qt::QueuedConnection, Q_ARG(QString, json));
    }

    void runNow() {
        if (runtime)
            QMetaObject::invokeMethod(runtime, "runNow", Qt::QueuedConnection);
    }

    // ── Audio script ──
    void setAnalyzerConfig(const AudioAnalyzerConfig &c) {
        config = c;
        if (analyzer)
            analyzer->setConfig(config);
    }

    void publishOutput() {
        if (!analyzer || !hasAudio)
            return;
        QJsonObject obj;
        obj["low"] = analyzer->lowBand();
        obj["mid"] = analyzer->midBand();
        obj["high"] = analyzer->highBand();
        obj["level"] = analyzer->level();
        obj["beat"] = analyzer->beatPulse();
        obj["hasAudio"] = analyzer->hasData();
        QJsonArray spectrum;
        for (float v : analyzer->spectrum())
            spectrum.append(std::clamp(static_cast<double>(v), 0.0, 1.0));
        obj["spectrum"] = spectrum;
        publishJson(*output, obj);
    }

    void publishSilentOutput() {
        QJsonObject obj;
        obj["low"] = 0.0;
        obj["mid"] = 0.0;
        obj["high"] = 0.0;
        obj["level"] = 0.0;
        obj["beat"] = 0.0;
        obj["hasAudio"] = false;
        QJsonArray spectrum;
        for (int i = 0; i < config.binCount; ++i)
            spectrum.append(0.0);
        obj["spectrum"] = spectrum;
        publishJson(*output, obj);
    }

    void setAudioSourcePath(const QString &path) {
        if (path == audioPath)
            return;
        liveSource = false;
        audioPath = path;
        if (audioPath.isEmpty()) {
            hasAudio = false;
            publishSilentOutput();
            return;
        }
        if (analyzer)
            analyzer->setConfig(config);
        if (analyzer && analyzer->open(audioPath)) {
            hasAudio = true;
            lastMs = clock.elapsed();
        } else {
            hasAudio = false;
            publishSilentOutput();
        }
    }

    void setAudioSyncState(double playbackTime, bool playing, double speed) {
        if (liveSource) {
            if (!hasAudio || !analyzer)
                return;
            const qint64 nowMs = clock.elapsed();
            const double dt = (lastMs > 0) ? (nowMs - lastMs) * 0.001 : 0.0;
            lastMs = nowMs;
            if (dt > 0.0 && !playing)
                analyzer->decay(dt);
            if (dt > 0.0)
                publishOutput();
            return;
        }
        if (!hasAudio || !analyzer)
            return;
        analyzer->setPlaybackSpeed(speed);
        const double drift = std::abs(analyzer->currentTime() - playbackTime);
        if (drift > 0.08)
            analyzer->seek(playbackTime);
        const qint64 nowMs = clock.elapsed();
        const double dt = (lastMs > 0) ? (nowMs - lastMs) * 0.001 : 0.0;
        lastMs = nowMs;
        if (dt > 0.0) {
            if (playing)
                analyzer->advance(dt);
            else
                analyzer->decay(dt);
        }
        publishOutput();
    }

    void setLiveSource(bool live) {
        if (liveSource == live)
            return;
        liveSource = live;
        audioPath.clear();
        hasAudio = false;
        if (live) {
            if (!analyzer)
                analyzer = std::make_unique<AudioAnalyzer>();
            analyzer->setConfig(config);
            hasAudio = analyzer->beginLive();
            lastMs = clock.elapsed();
            lastSignalMs = lastMs;
        } else if (analyzer) {
            analyzer->close();
        }
        if (!hasAudio)
            publishSilentOutput();
    }

    void feedLivePcm(const QByteArray &pcm) {
        if (!liveSource || !analyzer || !hasAudio)
            return;
        const qint64 nowMs = clock.elapsed();
        const double dt = (lastMs > 0) ? (nowMs - lastMs) * 0.001 : 0.0;
        lastMs = nowMs;

        if (AudioLoopbackEnumerator::pcmHasSignal(pcm)) {
            lastSignalMs = nowMs;
            analyzer->feedStereoPcm(pcm);
            publishOutput();
            return;
        }

        if (dt > 0.0) {
            analyzer->decay(dt);
            publishOutput();
        }
    }

    void tickLive() {
        if (!liveSource || !analyzer || !hasAudio)
            return;
        const qint64 nowMs = clock.elapsed();
        const double dt = (lastMs > 0) ? (nowMs - lastMs) * 0.001 : 0.0;
        lastMs = nowMs;
        if (dt <= 0.0)
            return;
        if ((nowMs - lastSignalMs) > 80) {
            analyzer->decay(dt);
            publishOutput();
        }
    }

    // ── Trigger ──
    void setTiming(TrigMode mode, int interval, QTime timeOfDay) {
        trigMode = mode;
        trigInterval = std::max(20, interval);
        trigTime = timeOfDay;
        applyTiming();
    }

    void pulse() {
        ++tickCount;
        QJsonObject obj;
        obj["tick"] = static_cast<double>(tickCount);
        obj["epoch"] = static_cast<double>(QDateTime::currentSecsSinceEpoch());
        obj["pulse"] = true;
        publishJson(*output, obj);
    }

    void applyTiming() {
        if (!trigTimer)
            return;
        trigTimer->stop();
        lastFireDate = QDate();
        if (trigMode == TrigMode::Interval)
            trigTimer->start(trigInterval);
        else if (trigMode == TrigMode::TimeOfDay)
            trigTimer->start(1000);
    }

    void onTrigTick() {
        if (trigMode == TrigMode::Interval) {
            pulse();
        } else if (trigMode == TrigMode::TimeOfDay) {
            const QDate today = QDate::currentDate();
            if (lastFireDate == today)
                return;
            if (QTime::currentTime() >= trigTime) {
                lastFireDate = today;
                pulse();
            }
        }
    }
};

bool isScriptType(NT t) { return t == NT::Script || t == NT::AudioScript || t == NT::Trigger; }

} // namespace

struct GraphRuntime::Impl {
    std::map<NodeId, std::unique_ptr<ScriptNode>> scripts;
    QHash<NodeId, uint> lastMergedVersion;
    QHash<NodeId, uint> lastAbVersion;
    mutable QHash<NodeId, std::shared_ptr<ScriptOutput>> dataInputs;
    std::shared_ptr<ThumbState> thumbs = std::make_shared<ThumbState>();
    QTimer *pollTimer = nullptr;

    ScriptNode *script(NodeId id) const {
        auto it = scripts.find(id);
        return it == scripts.end() ? nullptr : it->second.get();
    }

    void sync(ClipGraph *graph, NodeId id) {
        const ClipGraph::Node *n = graph->node(id);
        if (!n || !isScriptType(n->type)) {
            scripts.erase(id);
            return;
        }
        ScriptNode *sn = script(id);
        if (sn && sn->type != n->type) {
            scripts.erase(id);
            sn = nullptr;
        }
        const bool created = !sn;
        if (created) {
            auto owned = std::make_unique<ScriptNode>(n->type);
            sn = owned.get();
            scripts[id] = std::move(owned);
        }

        const QVariantMap &p = n->params;
        switch (n->type) {
        case NT::Script: {
            const QString code = p.value("luaCode").toString();
            const int mode = p.value("triggerMode", 3).toInt();
            const int interval = p.value("intervalMs", 1000).toInt();
            if (created) {
                sn->code = code;
                sn->triggerMode = mode;
                sn->intervalMs = interval;
                if (!code.isEmpty())
                    sn->applyScript(code, mode, interval);
            } else if (code != sn->code || mode != sn->triggerMode || interval != sn->intervalMs) {
                sn->applyScript(code, mode, interval);
            }
            break;
        }
        case NT::AudioScript: {
            const AudioAnalyzerConfig c = analyzerConfigFrom(p);
            if (!sameConfig(c, sn->config))
                sn->setAnalyzerConfig(c);
            break;
        }
        case NT::Trigger: {
            const auto mode = static_cast<ScriptNode::TrigMode>(
                std::clamp(p.value("trigMode", 0).toInt(), 0, 2));
            const int interval = std::max(20, p.value("trigInterval", 1000).toInt());
            QTime t = QTime::fromString(p.value("trigTime", QStringLiteral("12:00:00")).toString(),
                                        QStringLiteral("HH:mm:ss"));
            if (!t.isValid())
                t = QTime(12, 0);
            if (created) {
                sn->trigMode = mode;
                sn->trigInterval = interval;
                sn->trigTime = t;
                sn->applyTiming();
            } else if (mode != sn->trigMode || interval != sn->trigInterval || t != sn->trigTime) {
                sn->setTiming(mode, interval, t);
            }
            break;
        }
        default:
            break;
        }
    }

    void rebuild(ClipGraph *graph) {
        scripts.clear();
        lastMergedVersion.clear();
        lastAbVersion.clear();
        dataInputs.clear();
        for (NodeId id : graph->nodeIds())
            sync(graph, id);
    }

    void removeNode(NodeId id) {
        scripts.erase(id);
        lastMergedVersion.remove(id);
        lastAbVersion.remove(id);
        dataInputs.remove(id);
        QMutexLocker lock(&thumbs->mutex);
        thumbs->images.remove(id);
        thumbs->revisions.remove(id);
    }
};

GraphRuntime::GraphRuntime(ClipGraph *graph, QObject *parent)
    : QObject(parent), d(std::make_unique<Impl>()), m_graph(graph) {
    d->rebuild(m_graph);

    connect(m_graph, &ClipGraph::nodeAdded, this, [this](NodeId id) { d->sync(m_graph, id); });
    connect(m_graph, &ClipGraph::nodeChanged, this, [this](NodeId id) { d->sync(m_graph, id); });
    connect(m_graph, &ClipGraph::nodeRemoved, this, [this](NodeId id) { d->removeNode(id); });
    connect(m_graph, &ClipGraph::graphReset, this, [this]() {
        {
            QMutexLocker lock(&d->thumbs->mutex);
            d->thumbs->images.clear();
            d->thumbs->revisions.clear();
        }
        d->rebuild(m_graph);
    });

    d->pollTimer = new QTimer(this);
    connect(d->pollTimer, &QTimer::timeout, this, &GraphRuntime::pollDataConsumers);
    d->pollTimer->start(kPollIntervalMs);
}

GraphRuntime::~GraphRuntime() = default;

NodeId GraphRuntime::addClipNode(const QString &path, const QImage &thumbnail, const QPointF &pos) {
    const bool audioOnly = ClipManager::isAudioPath(path);
    const bool hasAudio = audioOnly || hasStream(MediaProbe::probe(path), StreamInfo::Type::Audio);

    SourceDescriptor desc;
    if (audioOnly)
        desc.kind = SourceDescriptor::Kind::AudioFile;
    else
        desc.kind = ThumbnailExtractor::isStaticImageFile(path) ? SourceDescriptor::Kind::Image
                                                                : SourceDescriptor::Kind::VideoFile;
    desc.path = path;
    desc.displayName = QFileInfo(path).baseName();

    const NodeId id = m_graph->addInputNode(desc, pos, hasAudio, audioOnly);
    if (id == 0)
        return 0;
    m_graph->setClipSettings(id, ClipSettings::loadFor(path));

    QImage thumb = thumbnail;
    if (thumb.isNull())
        thumb = ThumbnailExtractor::extract(path, 110, 65).toImage();
    if (!thumb.isNull())
        setThumbnail(id, thumb);
    return id;
}

NodeId GraphRuntime::addSourceNode(const SourceDescriptor &desc, const QImage &thumbnail,
                                   const QPointF &pos) {
    const NodeId id = m_graph->addInputNode(desc, pos, false, false);
    if (id == 0)
        return 0;
    if (!thumbnail.isNull())
        setThumbnail(id, thumbnail);
    return id;
}

QImage GraphRuntime::thumbnail(NodeId id) const {
    QMutexLocker lock(&d->thumbs->mutex);
    return d->thumbs->images.value(id);
}

void GraphRuntime::setThumbnail(NodeId id, const QImage &image) {
    {
        QMutexLocker lock(&d->thumbs->mutex);
        d->thumbs->images.insert(id, image);
        d->thumbs->revisions[id] += 1;
    }
    emit thumbnailChanged(id);
}

int GraphRuntime::thumbnailRevision(NodeId id) const {
    QMutexLocker lock(&d->thumbs->mutex);
    return d->thumbs->revisions.value(id, 0);
}

QQuickImageProvider *GraphRuntime::createImageProvider() {
    return new NodeThumbProvider(d->thumbs);
}

void GraphRuntime::pollDataConsumers() {
    for (auto &[id, sn] : d->scripts) {
        if (sn->type == NT::AudioScript)
            sn->tickLive();
    }

    for (auto &[id, sn] : d->scripts) {
        if (sn->type != NT::Script)
            continue;
        const QVector<NodeId> producers = m_graph->scriptProducersForData(id);
        uint verSum = 0;
        const QString merged = mergeScriptInputs(producers, verSum);
        if (!d->lastMergedVersion.contains(id) || d->lastMergedVersion.value(id) != verSum) {
            d->lastMergedVersion[id] = verSum;
            sn->setInputJson(merged);
        }
    }

    for (auto it = d->dataInputs.begin(); it != d->dataInputs.end(); ++it) {
        const QVector<NodeId> producers = m_graph->scriptProducersForData(it.key());
        if (producers.size() < 2 || !it.value())
            continue;
        uint verSum = 0;
        const QString merged = mergeScriptInputs(producers, verSum);
        if (d->lastMergedVersion.value(it.key(), ~0u) == verSum)
            continue;
        d->lastMergedVersion[it.key()] = verSum;
        {
            QMutexLocker lock(&it.value()->mutex);
            it.value()->json = merged;
        }
        it.value()->version.fetch_add(1, std::memory_order_release);
    }

    const QVector<NodeId> abNodes = m_graph->nodeIdsOfType(NT::AbSelect);
    for (NodeId abId : abNodes) {
        if (m_graph->scriptProducersForData(abId).isEmpty())
            continue;
        const auto output = scriptOutputForDataNode(abId);
        if (!output)
            continue;

        const uint ver = output->version.load(std::memory_order_acquire);
        if (ver == d->lastAbVersion.value(abId, 0))
            continue;
        d->lastAbVersion[abId] = ver;

        QString json;
        {
            QMutexLocker lock(&output->mutex);
            json = output->json;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (!doc.isObject())
            continue;
        m_graph->applyAbScriptOutput(abId, doc.object());
    }
}

QString GraphRuntime::mergeScriptInputs(const QVector<NodeId> &producers, uint &verSum) const {
    verSum = 0;
    QJsonObject combined;
    for (NodeId pid : producers) {
        const ScriptNode *sn = d->script(pid);
        if (!sn)
            continue;
        const auto out = sn->output;
        if (!out)
            continue;
        verSum += out->version.load(std::memory_order_acquire);
        QString j;
        {
            QMutexLocker lock(&out->mutex);
            j = out->json;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(j.toUtf8());
        if (doc.isObject()) {
            const QJsonObject o = doc.object();
            for (auto it = o.begin(); it != o.end(); ++it)
                combined.insert(it.key(), it.value());
        }
    }
    return QString::fromUtf8(QJsonDocument(combined).toJson(QJsonDocument::Compact));
}

std::shared_ptr<ScriptOutput> GraphRuntime::scriptOutputForDataNode(NodeId dataNodeId) const {
    const QVector<NodeId> producers = m_graph->scriptProducersForData(dataNodeId);
    if (producers.isEmpty()) {
        d->dataInputs.remove(dataNodeId);
        return nullptr;
    }
    if (producers.size() == 1) {
        d->dataInputs.remove(dataNodeId);
        const ScriptNode *sn = d->script(producers.first());
        return sn ? sn->output : nullptr;
    }
    auto slot = d->dataInputs.value(dataNodeId);
    if (!slot) {
        slot = std::make_shared<ScriptOutput>();
        d->dataInputs.insert(dataNodeId, slot);
        d->lastMergedVersion.remove(dataNodeId);
    }
    return slot;
}

ScriptBinding GraphRuntime::scriptBindingFor(NodeId dataNodeId) const {
    ScriptBinding binding;
    const NodeId scriptId = m_graph->dataScriptNodeId(dataNodeId);
    if (ScriptNode *sn = d->script(scriptId)) {
        binding.code = sn->code;
        binding.output = sn->output;
        if (sn->runtime) {
            QPointer<ScriptRuntime> rt(sn->runtime);
            binding.requestRun = [rt]() {
                if (rt)
                    QMetaObject::invokeMethod(rt, "runNow", Qt::QueuedConnection);
            };
        }
    }
    return binding;
}

bool GraphRuntime::runScript(NodeId scriptNodeId) {
    ScriptNode *sn = d->script(scriptNodeId);
    if (!sn || sn->type != NT::Script || !sn->runtime)
        return false;
    QMetaObject::invokeMethod(sn->runtime, "runNow", Qt::QueuedConnection);
    return true;
}

bool GraphRuntime::pulseTrigger(NodeId triggerNodeId) {
    ScriptNode *sn = d->script(triggerNodeId);
    if (!sn || sn->type != NT::Trigger)
        return false;
    sn->pulse();
    return true;
}

void GraphRuntime::feedLiveAudioForProducer(NodeId producerId, const QByteArray &pcm) {
    for (auto &[id, sn] : d->scripts) {
        if (sn->type != NT::AudioScript)
            continue;
        if (m_graph->audioSourceNodeIdForAudioScript(id) != producerId)
            continue;
        sn->feedLivePcm(pcm);
    }
}

void GraphRuntime::syncAudioScriptNode(NodeId scriptNodeId, double playbackTime, bool playing,
                                       double speed) {
    ScriptNode *sn = d->script(scriptNodeId);
    if (!sn || sn->type != NT::AudioScript)
        return;

    const NodeId producerId = m_graph->audioSourceNodeIdForAudioScript(scriptNodeId);
    if (producerId != 0 && m_graph->isLiveAudioScriptProducer(producerId)) {
        sn->setLiveSource(true);
        return;
    }

    sn->setLiveSource(false);

    QString audioPath;
    if (!m_graph->audioSourceForAudioScript(scriptNodeId, audioPath)) {
        sn->setAudioSourcePath(QString());
        return;
    }
    sn->setAudioSourcePath(audioPath);
    sn->setAudioSyncState(playbackTime, playing, speed);
}

void GraphRuntime::syncAllAudioScripts(NodeId deckAClip, double timeA, bool playingA, double speedA,
                                       NodeId deckBClip, double timeB, bool playingB, double speedB) {
    QVector<NodeId> ids;
    for (auto &[id, sn] : d->scripts)
        if (sn->type == NT::AudioScript)
            ids.append(id);
    for (NodeId id : ids) {
        const NodeId clip = m_graph->audioSourceNodeIdForAudioScript(id);
        if (clip != 0 && clip == deckAClip)
            syncAudioScriptNode(id, timeA, playingA, speedA);
        else if (clip != 0 && clip == deckBClip)
            syncAudioScriptNode(id, timeB, playingB, speedB);
        else
            syncAudioScriptNode(id, 0.0, false, 1.0);
    }
}
