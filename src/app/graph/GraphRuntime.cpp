#include "app/graph/GraphRuntime.h"

#include "app/BpmClock.h"
#include "app/graph/ModulatorEngine.h"

#include "core/media/AudioAnalyzer.h"
#include "core/media/MediaProbe.h"
#include "app/common/ThumbHelper.h"
#include "core/media/ThumbnailExtractor.h"
#include "core/project/ClipManager.h"
#include "core/scripting/ScriptRuntime.h"
#include "core/media/AudioLoopbackEnumerator.h"

#include <QCoreApplication>
#include <QDate>
#include <QEventLoop>
#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QTime>
#include <QTimer>
#include <QtQuick/QQuickImageProvider>

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

struct NodeThumbStore {
    QMutex mutex;
    QHash<NodeId, QImage> images;
    QHash<NodeId, int> revisions;
    int counter = 0;
};

namespace {

using NT = ClipGraph::NodeType;

constexpr int kPollIntervalMs = 33;

class NodeThumbProvider : public QQuickImageProvider {
public:
    explicit NodeThumbProvider(std::shared_ptr<NodeThumbStore> state)
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
    std::shared_ptr<NodeThumbStore> m_state;
};

bool isInput(const ClipGraph *graph, NodeId id) {
    const ClipGraph::Node *n = graph->node(id);
    return n && n->type == NT::Input;
}

QImage generateThumbnail(const SourceDescriptor &desc) {
    using K = SourceDescriptor::Kind;
    switch (desc.kind) {
    case K::VideoFile:
    case K::Image:
        return ThumbnailExtractor::extractImage(desc.path, 110, 65);
    case K::Slideshow: {
        const QStringList imgs = QDir(desc.path).entryList(
            {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.webp"}, QDir::Files, QDir::Name);
        QImage img;
        if (!imgs.isEmpty())
            img = ThumbnailExtractor::extractImage(QDir(desc.path).absoluteFilePath(imgs.first()), 110, 65);
        return img.isNull() ? ThumbHelper::makeIconThumb(ThumbHelper::Names::Folder) : img;
    }
    case K::Camera:
        return ThumbHelper::makeIconThumb(ThumbHelper::Names::PhotoCamera);
    case K::Screen:
        return ThumbHelper::makeIconThumb(ThumbHelper::Names::DesktopWindows);
    case K::Window:
        return ThumbHelper::makeIconThumb(ThumbHelper::Names::SelectWindow);
    case K::Ndi:
    case K::WebRtc:
        return ThumbHelper::makeIconThumb(ThumbHelper::Names::Sensors);
    case K::Shader:
        return ThumbHelper::makeIconThumb(ThumbHelper::Names::Grain);
    case K::Canvas: {
        const int g = std::gcd(std::max(1, desc.canvasWidth), std::max(1, desc.canvasHeight));
        return ThumbHelper::makeCanvasThumb(
            QStringLiteral("%1:%2").arg(desc.canvasWidth / g).arg(desc.canvasHeight / g),
            desc.canvasFill, desc.color);
    }
    case K::Text:
        return ThumbHelper::makeTextThumb(desc);
    case K::Shape:
        return ThumbHelper::makeShapeThumb(desc);
    case K::SvgTemplate:
        return ThumbHelper::makeSvgTemplateThumb(desc);
    case K::AudioFile:
    case K::Scene:
        break;
    }
    return {};
}

QString sourceKey(const SourceDescriptor &desc) {
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(ClipGraph::sourceToMap(desc)))
                                 .toJson(QJsonDocument::Compact));
}

void publishJson(ScriptOutput &out, const QJsonObject &obj) {
    const QString json = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    {
        QMutexLocker lock(&out.mutex);
        out.json = json;
    }
    out.version.fetch_add(1, std::memory_order_release);
}

struct PendingWrite {
    NodeId id;
    QString name;
    QVariant value;
};

/// State that script threads read or post to. It outlives the GraphRuntime while its scripts wind
/// down, so it never touches the runtime after `closed` is set.
struct RuntimeShared {
    QMutex mutex;
    bool closed = false;
    bool wakePosted = false;
    QHash<NodeId, QVariantMap> params;
    QHash<NodeId, QVariantMap> bands;
    NodeId latestBands = 0;
    QHash<NodeId, double> modulators;
    double bpm = 120.0;
    double beat = 0.0;
    QVector<PendingWrite> pending;
};

QVariant valueAtPath(const QVariantMap &map, const QString &path) {
    const int dot = path.indexOf(QLatin1Char('.'));
    if (dot < 0)
        return map.value(path);
    return valueAtPath(map.value(path.left(dot)).toMap(), path.mid(dot + 1));
}

QVariantMap withValueAtPath(QVariantMap map, const QString &path, const QVariant &value) {
    const int dot = path.indexOf(QLatin1Char('.'));
    if (dot < 0) {
        const QVariant old = map.value(path);
        if (old.typeId() == QMetaType::Int)
            map.insert(path, int(qRound64(value.toDouble())));
        else if (old.typeId() == QMetaType::LongLong)
            map.insert(path, qRound64(value.toDouble()));
        else if (old.typeId() == QMetaType::Bool)
            map.insert(path, value.toBool());
        else
            map.insert(path, value);
        return map;
    }
    const QString head = path.left(dot);
    map.insert(head, withValueAtPath(map.value(head).toMap(), path.mid(dot + 1), value));
    return map;
}

std::atomic<int> g_retiringScripts{0};

/// Stops a script thread without waiting for it: the runtime aborts at its next VM hook and is
/// deleted in its own thread, which then ends and is freed once the GUI loop runs again.
void retireScriptThread(QThread *thread, ScriptRuntime *runtime) {
    ++g_retiringScripts;
    QObject::connect(thread, &QThread::finished, [] { --g_retiringScripts; });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    runtime->requestShutdown();
    runtime->deleteLater();
    thread->quit();
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
    NT type = NT::Script;
    NodeId id = 0;
    std::shared_ptr<RuntimeShared> shared;
    std::shared_ptr<ScriptOutput> output = std::make_shared<ScriptOutput>();

    // Lua script
    QThread *thread = nullptr;
    ScriptRuntime *runtime = nullptr;
    QString code;
    int runOn = 0;
    int intervalMs = 1000;
    QString runTime;
    QString error;
    QStringList log;

    // Audio analyzer
    std::unique_ptr<AudioAnalyzer> analyzer;
    AudioAnalyzerConfig config;
    QString audioPath;
    bool hasAudio = false;
    bool liveSource = false;
    QElapsedTimer clock;
    qint64 lastMs = 0;
    qint64 lastSignalMs = 0;

    ScriptNode(NT t, NodeId nodeId, std::shared_ptr<RuntimeShared> sh,
               const std::shared_ptr<ScriptHost> &host)
        : type(t), id(nodeId), shared(std::move(sh)) {
        if (type == NT::Script) {
            thread = new QThread();
            runtime = new ScriptRuntime(output, host);
            runtime->moveToThread(thread);
            thread->start();
        } else if (type == NT::AudioAnalyzer) {
            analyzer = std::make_unique<AudioAnalyzer>();
            clock.start();
        }
    }

    ~ScriptNode() {
        if (runtime && thread)
            retireScriptThread(thread, runtime);
    }

    void applyScript(const QString &c, int mode, int interval, const QString &time) {
        code = c;
        runOn = mode;
        intervalMs = interval;
        runTime = time;
        if (runtime) {
            QMetaObject::invokeMethod(runtime, "applySettings", Qt::QueuedConnection,
                                      Q_ARG(QString, code), Q_ARG(int, runOn),
                                      Q_ARG(int, intervalMs), Q_ARG(QString, runTime));
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

    // ── Audio analyzer ──
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
        publishBands(obj);
    }

    void publishBands(const QJsonObject &obj) {
        QVariantMap b;
        for (const char *k : {"low", "mid", "high", "level", "beat", "hasAudio"})
            b.insert(QLatin1String(k), obj.value(QLatin1String(k)).toVariant());
        QMutexLocker lock(&shared->mutex);
        shared->bands.insert(id, b);
        shared->latestBands = id;
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
        publishBands(obj);
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
};

bool isScriptType(NT t) { return t == NT::Script || t == NT::AudioAnalyzer; }

} // namespace

struct GraphRuntime::Impl {
    GraphRuntime *owner = nullptr;
    std::map<NodeId, std::unique_ptr<ScriptNode>> scripts;
    QHash<NodeId, uint> lastMergedVersion;
    mutable QHash<NodeId, std::shared_ptr<ScriptOutput>> dataInputs;
    std::shared_ptr<NodeThumbStore> thumbs;
    QSet<NodeId> thumbIds;
    bool active = true;
    QHash<NodeId, QString> thumbKeys;
    QTimer *pollTimer = nullptr;

    std::shared_ptr<RuntimeShared> shared = std::make_shared<RuntimeShared>();
    std::shared_ptr<ScriptHost> host = std::make_shared<ScriptHost>();

    QHash<NodeId, QVariantMap> expressions;
    QHash<NodeId, QVariantMap> expressionErrors;
    QThread *exprThread = nullptr;
    ScriptRuntime *exprRuntime = nullptr;
    bool exprBusy = false;
    QElapsedTimer clock;
    ModulatorEngine modulators;
    BpmClock *bpmClock = nullptr;

    void tickModulators() {
        const ModulatorClock mc{clock.elapsed() / 1000.0, bpmClock ? bpmClock->beats() : 0.0};
        modulators.tick(mc, [this](const QString &spec) {
            QMutexLocker lock(&shared->mutex);
            const int colon = spec.indexOf(QLatin1Char(':'));
            const NodeId analyzer = colon > 0 ? spec.left(colon).toULongLong() : shared->latestBands;
            return shared->bands.value(analyzer).value(colon > 0 ? spec.mid(colon + 1) : spec).toDouble();
        });
        QMutexLocker lock(&shared->mutex);
        shared->bpm = bpmClock ? bpmClock->bpm() : 120.0;
        shared->beat = mc.beat;
        shared->modulators.clear();
        for (NodeId id : modulators.ids())
            shared->modulators.insert(id, modulators.value(id));
    }

    ScriptNode *script(NodeId id) const {
        auto it = scripts.find(id);
        return it == scripts.end() ? nullptr : it->second.get();
    }

    void ensureExpressionRuntime() {
        if (exprRuntime)
            return;
        exprThread = new QThread();
        exprRuntime = new ScriptRuntime(std::make_shared<ScriptOutput>(), host);
        exprRuntime->moveToThread(exprThread);
        QObject::connect(exprRuntime, &ScriptRuntime::expressionsEvaluated, owner,
                         [this](const QVariantList &results) { applyExpressionResults(results); });
        exprThread->start();
    }

    void applyExpressionResults(const QVariantList &results) {
        exprBusy = false;
        for (const QVariant &rv : results) {
            const QVariantMap r = rv.toMap();
            const NodeId id = r.value("node").toULongLong();
            const QString name = r.value("name").toString();
            if (!expressions.value(id).contains(name))
                continue;
            QString error = r.value("error").toString();
            if (r.value("ok").toBool()) {
                const ClipGraph::Node *n = owner->m_graph->node(id);
                const double v = r.value("value").toDouble();
                if (n) {
                    const QVariant cur = valueAtPath(n->params, name);
                    if (!cur.isValid() || std::abs(cur.toDouble() - v) > 1e-9)
                        owner->writeParam(id, name, v);
                }
            }
            setExpressionError(id, name, error);
        }
    }

    void setExpressionError(NodeId id, const QString &name, const QString &error) {
        QVariantMap &m = expressionErrors[id];
        if (m.value(name).toString() == error)
            return;
        if (error.isEmpty())
            m.remove(name);
        else
            m[name] = error;
        if (m.isEmpty())
            expressionErrors.remove(id);
        emit owner->scriptStatusChanged(id);
    }

    void runExpressions() {
        if (exprBusy || expressions.isEmpty())
            return;
        QVariantList jobs;
        for (auto it = expressions.begin(); it != expressions.end(); ++it)
            for (auto e = it->begin(); e != it->end(); ++e)
                jobs.append(QVariantMap{{"node", QVariant::fromValue<qulonglong>(it.key())},
                                        {"name", e.key()}, {"expr", e.value()}});
        ensureExpressionRuntime();
        exprBusy = true;
        QMetaObject::invokeMethod(exprRuntime, "evaluateExpressions", Qt::QueuedConnection,
                                  Q_ARG(QVariantList, jobs),
                                  Q_ARG(double, clock.elapsed() / 1000.0));
    }

    void connectStatus(ScriptNode *sn) {
        ScriptRuntime *rt = sn->runtime;
        const NodeId id = sn->id;
        QObject::connect(rt, &ScriptRuntime::executionFinished, owner,
                         [this, id, rt](bool, const QString &error) {
            ScriptNode *node = script(id);
            if (!node || node->runtime != rt || node->error == error)
                return;
            node->error = error;
            emit owner->scriptStatusChanged(id);
        });
        QObject::connect(rt, &ScriptRuntime::logChanged, owner,
                         [this, id, rt](const QStringList &lines) {
            ScriptNode *node = script(id);
            if (!node || node->runtime != rt)
                return;
            node->log = lines;
            emit owner->scriptStatusChanged(id);
        });
    }

    void syncExpressions(NodeId id, const ClipGraph::Node *n) {
        QVariantMap exprs;
        if (n) {
            const QVariantMap raw = n->params.value("expressions").toMap();
            for (auto it = raw.begin(); it != raw.end(); ++it)
                if (!it.value().toString().trimmed().isEmpty())
                    exprs.insert(it.key(), it.value().toString());
        }
        if (exprs.isEmpty()) {
            expressions.remove(id);
        } else {
            expressions.insert(id, exprs);
        }
        QVariantMap &errs = expressionErrors[id];
        bool changed = false;
        for (auto it = errs.begin(); it != errs.end();) {
            if (!exprs.contains(it.key())) {
                it = errs.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        if (errs.isEmpty())
            expressionErrors.remove(id);
        if (changed)
            emit owner->scriptStatusChanged(id);
    }

    void sync(ClipGraph *graph, NodeId id) {
        const ClipGraph::Node *n = graph->node(id);
        {
            QMutexLocker lock(&shared->mutex);
            if (n)
                shared->params.insert(id, n->params);
            else
                shared->params.remove(id);
        }
        syncExpressions(id, n);
        if (n && n->type == NT::Modulator)
            modulators.setNode(id, n->params);
        else
            modulators.removeNode(id);
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
            auto owned = std::make_unique<ScriptNode>(n->type, id, shared, host);
            sn = owned.get();
            scripts[id] = std::move(owned);
            if (sn->runtime) {
                connectStatus(sn);
                QMetaObject::invokeMethod(sn->runtime, "setActive", Qt::QueuedConnection,
                                          Q_ARG(bool, active));
            }
        }

        const QVariantMap &p = n->params;
        switch (n->type) {
        case NT::Script: {
            const QString code = p.value("luaCode").toString();
            const int mode = std::clamp(p.value("runOn", 0).toInt(), 0, 3);
            const int interval = std::max(20, p.value("intervalMs", 1000).toInt());
            const QString time = p.value("runTime", QStringLiteral("12:00:00")).toString();
            if (created || code != sn->code || mode != sn->runOn || interval != sn->intervalMs
                || time != sn->runTime)
                sn->applyScript(code, mode, interval, time);
            break;
        }
        case NT::AudioAnalyzer: {
            const AudioAnalyzerConfig c = analyzerConfigFrom(p);
            if (!sameConfig(c, sn->config))
                sn->setAnalyzerConfig(c);
            break;
        }
        default:
            break;
        }
    }

    void rebuild(ClipGraph *graph) {
        scripts.clear();
        lastMergedVersion.clear();
        dataInputs.clear();
        expressions.clear();
        expressionErrors.clear();
        modulators.clear();
        {
            QMutexLocker lock(&shared->mutex);
            shared->params.clear();
            shared->modulators.clear();
            shared->bands.clear();
            shared->latestBands = 0;
        }
        for (NodeId id : graph->nodeIds())
            sync(graph, id);
        emit owner->scriptStatusChanged(0);
    }

    void removeNode(NodeId id) {
        scripts.erase(id);
        expressions.remove(id);
        expressionErrors.remove(id);
        modulators.removeNode(id);
        {
            QMutexLocker lock(&shared->mutex);
            shared->modulators.remove(id);
            shared->params.remove(id);
            shared->bands.remove(id);
            if (shared->latestBands == id)
                shared->latestBands = 0;
        }
        lastMergedVersion.remove(id);
        dataInputs.remove(id);
        thumbKeys.remove(id);
        thumbIds.remove(id);
        QMutexLocker lock(&thumbs->mutex);
        thumbs->images.remove(id);
        thumbs->revisions.remove(id);
    }
};

std::shared_ptr<NodeThumbStore> GraphRuntime::createThumbStore() {
    return std::make_shared<NodeThumbStore>();
}

GraphRuntime::GraphRuntime(ClipGraph *graph, QObject *parent, std::shared_ptr<NodeThumbStore> thumbs)
    : QObject(parent), d(std::make_unique<Impl>()), m_graph(graph) {
    d->owner = this;
    d->clock.start();
    d->thumbs = thumbs ? std::move(thumbs) : createThumbStore();

    {
        auto sh = d->shared;
        d->host->getParam = [sh](quint64 id, const QString &name, QVariant *out) {
            QMutexLocker lock(&sh->mutex);
            const auto it = sh->params.constFind(id);
            if (it == sh->params.constEnd())
                return false;
            *out = valueAtPath(*it, name);
            return true;
        };
        d->host->setParam = [sh, this](quint64 id, const QString &name, const QVariant &value) {
            QMutexLocker lock(&sh->mutex);
            if (sh->closed || !sh->params.contains(id))
                return false;
            bool replaced = false;
            for (PendingWrite &w : sh->pending) {
                if (w.id == id && w.name == name) {
                    w.value = value;
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
                sh->pending.append({id, name, value});
            if (!sh->wakePosted) {
                sh->wakePosted = true;
                QMetaObject::invokeMethod(this, [this] { applyScriptWrites(); }, Qt::QueuedConnection);
            }
            return true;
        };
        d->host->clockInfo = [sh] {
            QMutexLocker lock(&sh->mutex);
            return QVariantMap{{QStringLiteral("bpm"), sh->bpm}, {QStringLiteral("beat"), sh->beat}};
        };
        d->host->modulatorValue = [sh](quint64 id) {
            QMutexLocker lock(&sh->mutex);
            return sh->modulators.value(id);
        };
        d->host->audioBands = [sh](quint64 id) {
            QMutexLocker lock(&sh->mutex);
            return sh->bands.value(id ? id : sh->latestBands);
        };
    }

    d->rebuild(m_graph);

    connect(m_graph, &ClipGraph::nodeAdded, this, [this](NodeId id) {
        d->sync(m_graph, id);
        QTimer::singleShot(0, this, [this, id]() {
            if (m_graph->hasNode(id) && thumbnailRevision(id) == 0 && !d->thumbKeys.contains(id))
                requestThumbnail(id);
        });
    });
    connect(m_graph, &ClipGraph::nodeChanged, this, [this](NodeId id) {
        d->sync(m_graph, id);
        if (!isInput(m_graph, id))
            return;
        const QString key = sourceKey(m_graph->sourceDescriptor(id));
        auto it = d->thumbKeys.find(id);
        if (it == d->thumbKeys.end())
            d->thumbKeys.insert(id, key);
        else if (*it != key)
            requestThumbnail(id);
    });
    connect(m_graph, &ClipGraph::nodeRemoved, this, [this](NodeId id) { d->removeNode(id); });
    connect(m_graph, &ClipGraph::graphReset, this, [this]() {
        {
            QMutexLocker lock(&d->thumbs->mutex);
            for (NodeId id : std::as_const(d->thumbIds)) {
                d->thumbs->images.remove(id);
                d->thumbs->revisions.remove(id);
            }
        }
        d->thumbIds.clear();
        d->thumbKeys.clear();
        emit thumbnailChanged(0);
        d->rebuild(m_graph);
        generateMissingThumbnails();
    });

    d->pollTimer = new QTimer(this);
    connect(d->pollTimer, &QTimer::timeout, this, &GraphRuntime::pollDataConsumers);
    d->pollTimer->start(kPollIntervalMs);
}

GraphRuntime::~GraphRuntime() {
    {
        QMutexLocker lock(&d->shared->mutex);
        d->shared->closed = true;
        d->shared->pending.clear();
    }
    if (d->exprRuntime) {
        retireScriptThread(d->exprThread, d->exprRuntime);
        d->exprRuntime = nullptr;
    }
}

bool GraphRuntime::waitForRetiredScripts(int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (g_retiringScripts.load() > 0) {
        if (t.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return true;
}

void GraphRuntime::writeParam(NodeId id, const QString &name, const QVariant &value) {
    const ClipGraph::Node *n = m_graph->node(id);
    if (!n)
        return;
    const QString top = name.section(QLatin1Char('.'), 0, 0);
    const QVariantMap next = withValueAtPath(n->params, name, value);
    m_graph->setParams(id, {{top, next.value(top)}});
}

void GraphRuntime::applyScriptWrites() {
    QVector<PendingWrite> writes;
    {
        QMutexLocker lock(&d->shared->mutex);
        writes.swap(d->shared->pending);
        d->shared->wakePosted = false;
    }
    for (const PendingWrite &w : std::as_const(writes))
        writeParam(w.id, w.name, w.value);
}

QString GraphRuntime::scriptError(NodeId id) const {
    const ScriptNode *sn = d->script(id);
    return sn ? sn->error : QString();
}

QStringList GraphRuntime::scriptLog(NodeId id) const {
    const ScriptNode *sn = d->script(id);
    return sn ? sn->log : QStringList();
}

QString GraphRuntime::expressionError(NodeId id, const QString &name) const {
    return d->expressionErrors.value(id).value(name).toString();
}

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
        d->thumbs->revisions[id] = ++d->thumbs->counter;
    }
    d->thumbIds.insert(id);
    emit thumbnailChanged(id);
}

void GraphRuntime::generateMissingThumbnails() {
    for (NodeId id : m_graph->nodeIds()) {
        if (isInput(m_graph, id) && thumbnailRevision(id) == 0)
            requestThumbnail(id);
    }
}

void GraphRuntime::requestThumbnail(NodeId id) {
    if (!m_graph->hasNode(id) || !isInput(m_graph, id))
        return;
    const SourceDescriptor desc = m_graph->sourceDescriptor(id);
    const QString key = sourceKey(desc);
    d->thumbKeys.insert(id, key);
    QPointer<GraphRuntime> self(this);
    QThreadPool::globalInstance()->start([self, id, key, desc]() {
        const QImage image = generateThumbnail(desc);
        if (image.isNull() || !self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, id, key, image]() {
            if (self && self->m_graph->hasNode(id) && self->d->thumbKeys.value(id) == key)
                self->setThumbnail(id, image);
        }, Qt::QueuedConnection);
    });
}

int GraphRuntime::thumbnailRevision(NodeId id) const {
    QMutexLocker lock(&d->thumbs->mutex);
    return d->thumbs->revisions.value(id, 0);
}

QQuickImageProvider *GraphRuntime::createImageProvider(std::shared_ptr<NodeThumbStore> thumbs) {
    return new NodeThumbProvider(std::move(thumbs));
}

void GraphRuntime::setBpmClock(BpmClock *clock) {
    d->bpmClock = clock;
}

double GraphRuntime::modulatorValue(NodeId id) const {
    return d->modulators.value(id);
}

bool GraphRuntime::isActive() const {
    return d->active;
}

void GraphRuntime::setActive(bool active) {
    if (d->active == active)
        return;
    d->active = active;
    if (active)
        d->pollTimer->start(kPollIntervalMs);
    else
        d->pollTimer->stop();
    for (auto &[id, sn] : d->scripts) {
        if (sn->runtime)
            QMetaObject::invokeMethod(sn->runtime, "setActive", Qt::QueuedConnection, Q_ARG(bool, active));
    }
}

void GraphRuntime::pollDataConsumers() {
    for (auto &[id, sn] : d->scripts) {
        if (sn->type == NT::AudioAnalyzer)
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

    d->tickModulators();
    d->runExpressions();

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

void GraphRuntime::feedLiveAudioForProducer(NodeId producerId, const QByteArray &pcm) {
    for (auto &[id, sn] : d->scripts) {
        if (sn->type != NT::AudioAnalyzer)
            continue;
        if (m_graph->audioSourceNodeIdForAudioAnalyzer(id) != producerId)
            continue;
        sn->feedLivePcm(pcm);
    }
}

void GraphRuntime::syncAudioAnalyzerNode(NodeId scriptNodeId, double playbackTime, bool playing,
                                       double speed) {
    ScriptNode *sn = d->script(scriptNodeId);
    if (!sn || sn->type != NT::AudioAnalyzer)
        return;

    const NodeId producerId = m_graph->audioSourceNodeIdForAudioAnalyzer(scriptNodeId);
    if (producerId != 0 && m_graph->isLiveAudioAnalyzerProducer(producerId)) {
        sn->setLiveSource(true);
        return;
    }

    sn->setLiveSource(false);

    QString audioPath;
    if (!m_graph->audioSourceForAudioAnalyzer(scriptNodeId, audioPath)) {
        sn->setAudioSourcePath(QString());
        return;
    }
    sn->setAudioSourcePath(audioPath);
    sn->setAudioSyncState(playbackTime, playing, speed);
}

void GraphRuntime::syncAllAudioAnalyzers(NodeId liveClip, double timeLive, bool playingLive, double speedLive,
                                       NodeId stagingClip, double timeStaging, bool playingStaging, double speedStaging) {
    QVector<NodeId> ids;
    for (auto &[id, sn] : d->scripts)
        if (sn->type == NT::AudioAnalyzer)
            ids.append(id);
    for (NodeId id : ids) {
        const NodeId clip = m_graph->audioSourceNodeIdForAudioAnalyzer(id);
        if (clip != 0 && clip == liveClip)
            syncAudioAnalyzerNode(id, timeLive, playingLive, speedLive);
        else if (clip != 0 && clip == stagingClip)
            syncAudioAnalyzerNode(id, timeStaging, playingStaging, speedStaging);
        else
            syncAudioAnalyzerNode(id, 0.0, false, 1.0);
    }
}
