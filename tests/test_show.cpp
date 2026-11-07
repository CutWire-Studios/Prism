#include "app/AppController.h"
#include "app/graph/GraphRuntime.h"
#include "app/session/SessionManager.h"
#include "app/transitions/TransitionController.h"
#include "app/remote/OscProtocol.h"
#include "app/remote/OscServer.h"
#include "app/remote/RemoteControlServer.h"
#include "app/remote/RemoteProtocol.h"
#include "app/show/SceneThumbnails.h"
#include "app/show/Preflight.h"
#include "app/show/VenueProfile.h"
#include "app/show/ShowManager.h"
#include "core/graph/ClipGraph.h"
#include "core/graph/ClipGraphModels.h"
#include "core/sources/SourceDescriptor.h"

#include <QtTest>
#include <QImage>
#include <QJsonArray>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QUndoStack>
#include <QStandardPaths>
#include <QTemporaryDir>

using NT = ClipGraph::NodeType;

namespace {

QVariantMap canvas(const QString &name)
{
    return {{"source", QVariantMap{{"kind", int(SourceDescriptor::Kind::Canvas)}, {"displayName", name}}}};
}

// A new scene holding one still clip wired to its Output node; leaves it as the edited scene.
QString addClipScene(AppController &app, const QString &path, const QColor &color)
{
    QImage img(32, 32, QImage::Format_RGB888);
    img.fill(color);
    if (!img.save(path))
        return {};
    const QString scene = app.addScene();
    app.enterScene(scene);
    const quint64 clip = app.addSourceFromMap({{QStringLiteral("kind"), int(SourceDescriptor::Kind::VideoFile)},
                                               {QStringLiteral("path"), path}});
    if (!clip || !app.clipGraph()->connectNodes(clip, app.clipGraph()->outputNodeId()))
        return {};
    return scene;
}

// Adds a Scene source node pointing at @p target to @p host's graph.
void addSceneRef(Scene *host, const QString &target)
{
    host->graph()->addNode(NT::Input, {}, {{"source", QVariantMap{{"kind", int(SourceDescriptor::Kind::Scene)},
                                                                  {"sceneId", target}}}});
}

}  // namespace

class TestShow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void init() { QFile::remove(SessionManager::lockFilePath()); }

    void newShowStartsWithOneLiveScene() {
        ShowManager show;
        QCOMPARE(show.scenes().size(), 1);
        QCOMPARE(show.liveSceneId(), show.scenes().first()->id());
        QVERIFY(show.stagingSceneId().isEmpty());
        QCOMPARE(show.editedSceneId(), show.liveSceneId());
        const ClipGraph *g = show.liveScene()->graph();
        QVERIFY(g->outputNodeId() != 0);
        QVERIFY(g->audioOutputNodeId() != 0);
    }

    void enteringTabsLeavesLiveAndStagingAlone() {
        AppController app(nullptr);
        const QString live = app.liveSceneId();
        const QString b = app.addScene(QStringLiteral("B"));
        const QString c = app.addScene(QStringLiteral("C"));
        QVERIFY(app.stageScene(c));

        app.enterScene(b);
        QCOMPARE(app.editedSceneId(), b);
        QCOMPARE(app.liveSceneId(), live);
        QCOMPARE(app.stagingSceneId(), c);

        app.enterScene(live);
        app.enterSceneRelative(-1);
        QCOMPARE(app.editedSceneId(), app.showManager()->scenes().last()->id());
        app.enterSceneRelative(1);
        QCOMPARE(app.editedSceneId(), live);
        QCOMPARE(app.liveSceneId(), live);
        QCOMPARE(app.stagingSceneId(), c);
    }

    void logicalAudioPortsMapThroughVenue() {
        AppController app(nullptr);
        const QString logical = QStringLiteral("Output 2");
        QVERIFY(app.audioDeviceFor(logical).isEmpty());
        QCOMPARE(app.venue().resolveDevice(logical), logical);

        QSignalSpy spy(&app, &AppController::audioDevicesChanged);
        app.setAudioDevice(logical, QStringLiteral("hw:2"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(app.audioDeviceFor(logical), QStringLiteral("hw:2"));
        QCOMPARE(app.venue().resolveDevice(logical), QStringLiteral("hw:2"));

        app.setAudioDevice(logical, QString());
        QCOMPARE(app.venue().resolveDevice(logical), QString());
        app.clearAudioDevice(logical);
        QCOMPARE(app.venue().resolveDevice(logical), logical);
        QSettings().remove(QStringLiteral("venue/profile"));
    }

    void transitionResolutionPrefersIncoming() {
        ShowManager show;
        Scene *a = show.addScene(QStringLiteral("A"));
        show.setDefaultTransition({3, 800});
        QCOMPARE(show.resolveTransition(a), (TransitionSpec{3, 800}));
        QCOMPARE(show.resolveTransition(nullptr), (TransitionSpec{3, 800}));

        a->setIncoming(TransitionSpec{5, 250});
        QCOMPARE(show.resolveTransition(a), (TransitionSpec{5, 250}));
        a->setIncoming(std::nullopt);
        QCOMPARE(show.resolveTransition(a), (TransitionSpec{3, 800}));
    }

    void stageAndSwapExchangeScenes() {
        ShowManager show;
        Scene *a = show.addScene(QStringLiteral("A"));
        const QString first = show.liveSceneId();
        QVERIFY(show.stageScene(a->id()));
        QCOMPARE(show.stagingSceneId(), a->id());

        QSignalSpy live(&show, &ShowManager::liveSceneChanged);
        QSignalSpy staging(&show, &ShowManager::stagingSceneChanged);
        show.swapLiveStaging();
        QCOMPARE(show.liveSceneId(), a->id());
        QCOMPARE(show.stagingSceneId(), first);
        QCOMPARE(live.size(), 1);
        QCOMPARE(staging.size(), 1);

        QVERIFY(!show.stageScene(QStringLiteral("missing")));
        QVERIFY(!show.removeScene(show.liveSceneId()));
        QVERIFY(show.removeScene(first));
        QVERIFY(show.stagingSceneId().isEmpty());
    }

    void nodeIdsAreUniqueAcrossScenes() {
        ShowManager show;
        Scene *a = show.addScene();
        Scene *b = show.addScene();
        a->graph()->addNode(NT::Input, {}, canvas(QStringLiteral("x")));
        b->graph()->addNode(NT::Input, {}, canvas(QStringLiteral("y")));
        QSet<NodeId> seen;
        for (const Scene *scene : show.scenes()) {
            for (NodeId id : scene->graph()->nodeIds()) {
                QVERIFY2(!seen.contains(id), "node id reused across scenes");
                QVERIFY(id > (quint64(scene->serial()) << 40));
                seen.insert(id);
            }
        }
        for (const Scene *scene : show.scenes())
            QCOMPARE(show.sceneForNode(scene->graph()->outputNodeId()), scene);
    }

    void duplicateGetsFreshIdsAndKeepsWiring() {
        ShowManager show;
        Scene *a = show.addScene(QStringLiteral("A"));
        const NodeId in = a->graph()->addNode(NT::Input, {}, canvas(QStringLiteral("x")));
        QVERIFY(a->graph()->connectNodes(in, a->graph()->outputNodeId()));
        a->setEndAction(Scene::EndAction::Loop);

        Scene *copy = show.duplicateScene(a->id());
        QVERIFY(copy);
        QCOMPARE(show.indexOf(copy->id()), show.indexOf(a->id()) + 1);
        QCOMPARE(copy->endAction(), Scene::EndAction::Loop);
        QCOMPARE(copy->graph()->nodeIds().size(), a->graph()->nodeIds().size());
        for (NodeId id : copy->graph()->nodeIds())
            QVERIFY(!a->graph()->hasNode(id));
        QVERIFY(copy->graph()->outputSingleProducer() != 0);
        QVERIFY(copy->graph()->addNode(NT::Layer) > copy->graph()->outputNodeId());
    }

    void jsonRoundTrip() {
        ShowManager show;
        Scene *a = show.addScene(QStringLiteral("A"));
        Scene *b = show.addScene(QStringLiteral("B"));
        const NodeId in = a->graph()->addNode(NT::Input, {}, canvas(QStringLiteral("x")));
        QVERIFY(a->graph()->connectNodes(in, a->graph()->outputNodeId()));
        a->setIncoming(TransitionSpec{2, 400});
        a->setEndAction(Scene::EndAction::AutoAdvance);
        a->setAutoAdvanceMs(1500);
        a->setAdvanceOnMediaEnd(true);
        b->setColor(QStringLiteral("#ff0000"));
        b->setNotes(QStringLiteral("hello"));
        show.setDefaultTransition({4, 900});
        QVERIFY(show.stageScene(a->id()));
        QVERIFY(show.enterScene(b->id()));

        const QJsonObject json = show.toJson();
        ShowManager loaded;
        QVERIFY(loaded.fromJson(json));
        QCOMPARE(loaded.scenes().size(), 3);
        QCOMPARE(loaded.liveSceneId(), show.liveSceneId());
        QCOMPARE(loaded.stagingSceneId(), a->id());
        QCOMPARE(loaded.editedSceneId(), b->id());
        QCOMPARE(loaded.defaultTransition(), (TransitionSpec{4, 900}));

        const Scene *la = loaded.scene(a->id());
        QVERIFY(la);
        QCOMPARE(la->name(), QStringLiteral("A"));
        QVERIFY(la->incoming().has_value());
        QCOMPARE(*la->incoming(), (TransitionSpec{2, 400}));
        QCOMPARE(la->endAction(), Scene::EndAction::AutoAdvance);
        QCOMPARE(la->autoAdvanceMs(), 1500);
        QVERIFY(la->advanceOnMediaEnd());
        QVERIFY(la->graph()->hasNode(in));
        QCOMPARE(la->graph()->outputSingleProducer(), in);
        QCOMPARE(loaded.scene(b->id())->color(), QStringLiteral("#ff0000"));
        QCOMPARE(loaded.scene(b->id())->notes(), QStringLiteral("hello"));
        QVERIFY(!loaded.scene(b->id())->incoming().has_value());

        QCOMPARE(loaded.toJson(), json);

        // New nodes in a loaded scene continue above the stored ids.
        QVERIFY(loaded.scene(a->id())->graph()->addNode(NT::Layer) > a->graph()->nodeIds().last());
    }

    void oldSceneGraphVersionIsRejected() {
        ShowManager show;
        show.addScene(QStringLiteral("A"));
        QJsonObject json = show.toJson();
        QJsonArray scenes = json["scenes"].toArray();
        QJsonObject scene = scenes.at(1).toObject();
        QJsonObject graph = scene["graph"].toObject();
        graph["graphVersion"] = ClipGraph::kGraphVersion - 1;
        scene["graph"] = graph;
        scenes[1] = scene;
        json["scenes"] = scenes;

        ShowManager loaded;
        QVERIFY(!loaded.fromJson(json));
        QCOMPARE(loaded.scenes().size(), 1);

        QVERIFY(!loaded.fromJson(QJsonObject{}));
        QCOMPARE(loaded.scenes().size(), 1);
    }

    void mediaEndOnlyEndsScenesThatOptedIn() {
        ShowManager show;
        QSignalSpy ended(&show, &ShowManager::liveEnded);
        show.notifyLiveMediaEnded();
        QCOMPARE(ended.size(), 0);

        Scene *live = show.liveScene();
        live->setEndAction(Scene::EndAction::AutoAdvance);
        show.notifyLiveMediaEnded();
        QCOMPARE(ended.size(), 0);

        live->setAdvanceOnMediaEnd(true);
        show.notifyLiveMediaEnded();
        QCOMPARE(ended.size(), 1);

        live->setEndAction(Scene::EndAction::Hold);
        show.notifyLiveMediaEnded();
        QCOMPARE(ended.size(), 1);
    }

    void takeSwapsLiveAndStagingScenes() {
        QTemporaryDir dir;
        AppController app(nullptr);
        const QString first = app.liveSceneId();
        const QString scene = addClipScene(app, dir.filePath(QStringLiteral("a.png")), Qt::red);
        QVERIFY(!scene.isEmpty());

        QVERIFY(!app.stageScene(first));   // the Live scene cannot be staged
        QVERIFY(app.stageScene(scene));
        QTRY_VERIFY(app.compositor()->sourceStaging());

        app.setTransitionDuration(0.05);
        app.take();
        QTRY_COMPARE(app.liveSceneId(), scene);
        QCOMPARE(app.stagingSceneId(), first);
        QVERIFY(!app.removeScene(scene));
    }

    void takeDuringTransitionRetargetsFromFrozenBlend() {
        QTemporaryDir dir;
        AppController app(nullptr);
        const QString first = app.liveSceneId();
        const QString b = addClipScene(app, dir.filePath(QStringLiteral("b.png")), Qt::red);
        const QString c = addClipScene(app, dir.filePath(QStringLiteral("c.png")), Qt::blue);
        QVERIFY(!b.isEmpty() && !c.isEmpty());
        app.setTransitionMode(0);
        app.setTransitionDuration(3.0);

        QVERIFY(app.stageAndTake(b));
        QTRY_VERIFY_WITH_TIMEOUT(app.transitionRunning(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(app.faderValue() >= 30, 3000);
        QCOMPARE(app.stagingSceneId(), b);

        QVERIFY(app.stageAndTake(b));   // already the target
        QCOMPARE(app.stagingSceneId(), b);
        QVERIFY(app.faderValue() >= 30);
        QVERIFY(!app.stageAndTake(first));   // the Live scene is not a retarget candidate

        QSignalSpy committed(app.transitionController(), &TransitionController::takeCommitted);
        QVERIFY(app.stageAndTake(c));
        QCOMPARE(app.liveSceneId(), first);
        QCOMPARE(app.stagingSceneId(), c);
        QVERIFY(app.faderValue() < 30);
        QVERIFY(app.transitionRunning());
        QCOMPARE(committed.count(), 0);
        if (app.compositor()->glAvailable()) {
            QVERIFY(app.compositor()->sourceLive());
            QCOMPARE(app.compositor()->sourceLive()->displayName(), QStringLiteral("Frozen Frame"));
        }

        app.setTransitionDuration(0.05);
        QTRY_COMPARE_WITH_TIMEOUT(app.liveSceneId(), c, 6000);
        QCOMPARE(app.stagingSceneId(), first);
        QCOMPARE(committed.count(), 1);
    }

    void previewNodeDoesNotAffectTake() {
        QTemporaryDir dir;
        AppController app(nullptr);
        const QString first = app.liveSceneId();
        const QString scene = addClipScene(app, dir.filePath(QStringLiteral("p.png")), Qt::red);
        QVERIFY(!scene.isEmpty());
        QVERIFY(app.stageScene(scene));
        QTRY_VERIFY(app.compositor()->sourceStaging());

        const NodeId clip = app.clipGraph()->nodeIdsOfType(NT::Input).first();
        QSignalSpy changed(&app, &AppController::previewNodeChanged);
        app.previewNode(clip);
        QCOMPARE(app.previewNodeId(), clip);
        QCOMPARE(changed.size(), 1);
        app.previewNode(clip);
        QCOMPARE(app.previewNodeId(), quint64(0));
        app.previewNode(clip);
        QCOMPARE(app.previewNodeId(), clip);
        app.previewNode(app.clipGraph()->outputNodeId());
        QCOMPARE(app.previewNodeId(), clip);

        app.setTransitionDuration(0.05);
        app.take();
        QTRY_COMPARE(app.liveSceneId(), scene);
        QCOMPARE(app.stagingSceneId(), first);

        app.clearNodePreview();
        QCOMPARE(app.previewNodeId(), quint64(0));
        app.previewNode(clip);
        app.removeNode(clip);
        QCOMPARE(app.previewNodeId(), quint64(0));
    }

    void sceneTransitionAppliesToTakeThenDefaultReturns() {
        QTemporaryDir dir;
        AppController app(nullptr);
        const QString scene = addClipScene(app, dir.filePath(QStringLiteral("t.png")), Qt::red);
        QVERIFY(!scene.isEmpty());
        app.setTransitionMode(0);
        app.setTransitionDuration(2.0);
        app.setSceneProps(scene, {{"transition", QVariantMap{{"mode", 1}, {"durationMs", 50}}}});

        QVERIFY(app.stageScene(scene));
        QTRY_VERIFY(app.compositor()->sourceStaging());
        app.take();   // mode 1 is Cut: commits immediately
        QTRY_COMPARE(app.liveSceneId(), scene);
        QCOMPARE(app.transitionMode(), 0);
        QCOMPARE(app.transitionDuration(), 2.0);
        QCOMPARE(app.showManager()->defaultTransition(), (TransitionSpec{0, 2000}));
    }

    void showUndoCoversAddRemoveReorderAndProps() {
        ShowManager show;
        const QString first = show.liveSceneId();
        QUndoStack *undo = show.undoStack();

        Scene *a = show.addScene(QStringLiteral("A"));
        const QString aId = a->id();
        const NodeId in = a->graph()->addNode(NT::Input, {}, canvas(QStringLiteral("x")));
        QCOMPARE(show.scenes().size(), 2);
        undo->undo();
        QCOMPARE(show.scenes().size(), 1);
        undo->redo();
        QCOMPARE(show.scenes().size(), 2);
        QVERIFY(show.scene(aId)->graph()->hasNode(in));

        Scene *b = show.addScene(QStringLiteral("B"));
        const QString bId = b->id();
        QCOMPARE(show.indexOf(bId), 2);
        QVERIFY(show.moveScene(2, 0));
        QCOMPARE(show.indexOf(bId), 0);
        undo->undo();
        QCOMPARE(show.indexOf(bId), 2);
        undo->redo();
        QCOMPARE(show.indexOf(bId), 0);
        undo->undo();

        SceneMeta meta = show.scene(aId)->meta();
        meta.name = QStringLiteral("Renamed");
        meta.incoming = TransitionSpec{2, 300};
        show.setSceneMeta(aId, meta);
        QCOMPARE(show.scene(aId)->name(), QStringLiteral("Renamed"));
        undo->undo();
        QCOMPARE(show.scene(aId)->name(), QStringLiteral("A"));
        QVERIFY(!show.scene(aId)->incoming());
        undo->redo();
        QCOMPARE(show.scene(aId)->incoming()->durationMs, 300);

        QVERIFY(show.removeScene(aId));
        QVERIFY(!show.scene(aId));
        undo->undo();
        QVERIFY(show.scene(aId));
        QCOMPARE(show.scene(aId)->name(), QStringLiteral("Renamed"));
        undo->redo();
        QVERIFY(!show.scene(aId));

        Scene *dup = show.duplicateScene(first);
        QVERIFY(dup);
        const QString dupId = dup->id();
        undo->undo();
        QVERIFY(!show.scene(dupId));
        undo->redo();
        QVERIFY(show.scene(dupId));
    }

    void thumbnailPathPersists() {
        QTemporaryDir dir;
        QImage img(320, 180, QImage::Format_RGB888);
        img.fill(Qt::red);
        ShowManager show;
        Scene *a = show.addScene(QStringLiteral("A"));
        const QString path = SceneThumbnails::save(img, dir.filePath(QStringLiteral("scenethumbs")), a->id());
        QVERIFY(!path.isEmpty());
        const QImage saved(path);
        QCOMPARE(saved.size(), QSize(SceneThumbnails::kWidth, SceneThumbnails::kHeight));
        a->setThumbnail(path);

        ShowManager other;
        QVERIFY(other.fromJson(show.toJson()));
        QCOMPARE(other.scene(a->id())->thumbnail(), path);
    }

    void preflightReportsMissingFileAndDevice() {
        using Kind = SourceDescriptor::Kind;
        ShowManager show;
        Scene *a = show.addScene(QStringLiteral("A"));
        a->graph()->addNode(NT::Input, {}, {{"source", QVariantMap{{"kind", int(Kind::VideoFile)},
                                                                   {"path", QStringLiteral("/nonexistent/clip.mp4")}}}});
        a->graph()->addNode(NT::Input, {}, {{"source", QVariantMap{{"kind", int(Kind::Camera)},
                                                                   {"path", QStringLiteral("fake-cam")}}}});
        const auto issues = Preflight::run(show, QDir(), [](const SourceDescriptor &d) { return d.kind == Kind::Camera; });
        QCOMPARE(issues.size(), 2);
        int files = 0, devices = 0;
        for (const PreflightIssue &i : issues) {
            QCOMPARE(i.sceneId, a->id());
            files += i.kind == PreflightIssue::Kind::MissingFile;
            devices += i.kind == PreflightIssue::Kind::MissingDevice;
        }
        QCOMPARE(files, 1);
        QCOMPARE(devices, 1);
    }

    void sceneCycleDetection() {
        ShowManager show;
        Scene *a = show.addScene(QStringLiteral("A"));
        Scene *b = show.addScene(QStringLiteral("B"));
        Scene *c = show.addScene(QStringLiteral("C"));
        QVERIFY(show.wouldCreateSceneCycle(a->id(), a->id()));
        QVERIFY(!show.wouldCreateSceneCycle(a->id(), b->id()));

        addSceneRef(a, b->id());
        QVERIFY(show.wouldCreateSceneCycle(b->id(), a->id()));
        QVERIFY(!show.wouldCreateSceneCycle(a->id(), b->id()));

        addSceneRef(b, c->id());
        QVERIFY(show.wouldCreateSceneCycle(c->id(), a->id()));
        QVERIFY(show.wouldCreateSceneCycle(c->id(), b->id()));
        QVERIFY(!show.wouldCreateSceneCycle(a->id(), c->id()));
    }

    void sceneRefValidatorRefusesCycles() {
        ShowManager show;
        Scene *a = show.addScene(QStringLiteral("A"));
        Scene *b = show.addScene(QStringLiteral("B"));
        addSceneRef(a, b->id());
        const QVariantMap toA{{"source", QVariantMap{{"kind", int(SourceDescriptor::Kind::Scene)},
                                                     {"sceneId", a->id()}}}};
        QCOMPARE(b->controller()->addNode(int(NT::Input), 0, 0, toA), quint64(0));
        const quint64 own = b->controller()->addNode(int(NT::Input), 0, 0, {{"source", QVariantMap{
            {"kind", int(SourceDescriptor::Kind::Scene)}}}});
        QVERIFY(own != 0);
        QVERIFY(!b->controller()->setParams(own, toA));
        QVERIFY(b->controller()->setParams(own, {{"source", QVariantMap{
            {"kind", int(SourceDescriptor::Kind::Scene)}, {"sceneId", QStringLiteral("none")}}}}));
    }

    void sceneRefsActivateTransitiveClosure() {
        ShowManager show;
        Scene *a = show.liveScene();
        Scene *b = show.addScene(QStringLiteral("B"));
        Scene *c = show.addScene(QStringLiteral("C"));
        Scene *d = show.addScene(QStringLiteral("D"));
        addSceneRef(a, b->id());
        addSceneRef(b, c->id());
        show.updateRuntimeActivity();
        QVERIFY(a->runtime()->isActive());
        QVERIFY(b->runtime()->isActive());
        QVERIFY(c->runtime()->isActive());
        QVERIFY(!d->runtime()->isActive());
    }

    void loadClearsCyclicSceneRef() {
        ShowManager show;
        Scene *a = show.liveScene();
        Scene *b = show.addScene(QStringLiteral("B"));
        addSceneRef(a, b->id());
        addSceneRef(b, a->id());

        ShowManager other;
        QVERIFY(other.fromJson(show.toJson()));
        const bool aKept = !other.sceneReferences(a->id()).isEmpty();
        const bool bKept = !other.sceneReferences(b->id()).isEmpty();
        QVERIFY(aKept != bKept);
        QVERIFY(!other.wouldCreateSceneCycle(a->id(), b->id()) || !other.wouldCreateSceneCycle(b->id(), a->id()));
    }

    void preflightReportsMissingAndCyclicScenes() {
        ShowManager show;
        Scene *a = show.liveScene();
        Scene *b = show.addScene(QStringLiteral("B"));
        addSceneRef(a, QStringLiteral("deleted"));
        addSceneRef(a, b->id());
        addSceneRef(b, a->id());
        const auto issues = Preflight::run(show, QDir(), {});
        int missing = 0, cycles = 0;
        for (const PreflightIssue &i : issues) {
            missing += i.kind == PreflightIssue::Kind::MissingScene;
            cycles += i.kind == PreflightIssue::Kind::SceneCycle;
        }
        QCOMPARE(missing, 1);
        QCOMPARE(cycles, 2);
    }

    void stageRelativeStepsFromLiveScene() {
        AppController app(nullptr);
        const QString first = app.liveSceneId();
        const QString second = app.addScene();
        QVERIFY(!app.stageRelative(-1));
        QVERIFY(app.stageRelative(1));
        QCOMPARE(app.stagingSceneId(), second);
        QVERIFY(!app.stageRelative(2));
        QVERIFY(app.moveScene(second, 0));
        QCOMPARE(app.showManager()->indexOf(first), 1);
        QVERIFY(app.stageRelative(-1));
        QCOMPARE(app.stagingSceneId(), second);
    }

    void oscNextPrevStageRelativeToLive() {
        QSettings settings;
        settings.setValue(QStringLiteral("osc/enabled"), true);
        settings.setValue(QStringLiteral("osc/port"), 39217);
        settings.setValue(QStringLiteral("osc/feedbackHost"), QString());
        AppController app(nullptr);
        OscServer osc(&app);
        QVERIFY(osc.applySettings());
        const QString second = app.addScene();
        const QString third = app.addScene();
        const QString live = app.liveSceneId();

        QUdpSocket sock;
        auto fire = [&](const QString &address, const QVariantList &args = {}) {
            sock.writeDatagram(OscProtocol::encode({address, args}), QHostAddress::LocalHost, 39217);
        };
        fire(QStringLiteral("/prism/scene/next"));
        QTRY_VERIFY(!app.stagingSceneId().isEmpty());
        QCOMPARE(app.showManager()->indexOf(app.stagingSceneId()), app.showManager()->indexOf(live) + 1);
        fire(QStringLiteral("/prism/scene/stage"), {3});
        QTRY_COMPARE(app.stagingSceneId(), third);
        fire(QStringLiteral("/prism/scene/prev"));
        QTRY_VERIFY(app.stagingSceneId() != third);
        Q_UNUSED(second);
        osc.stop();
        settings.setValue(QStringLiteral("osc/enabled"), false);
    }

    void httpSceneEndpoints() {
        AppController app(nullptr);
        RemoteControlServer server(&app);
        QVERIFY(server.startServer(QHostAddress::LocalHost, 39218));
        const QString second = app.addScene();
        app.addScene();

        auto get = [&](const QString &path) {
            QTcpSocket sock;
            sock.connectToHost(QHostAddress::LocalHost, 39218);
            (void)QTest::qWaitFor([&] { return sock.state() == QAbstractSocket::ConnectedState; }, 2000);
            sock.write("GET " + path.toUtf8() + " HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
            QByteArray all;
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < 3000 && !all.contains("}")) {
                QTest::qWait(20);
                all += sock.readAll();
            }
            return all;
        };

        QVERIFY(get(QStringLiteral("/api/scene/next")).contains("ok"));
        QCOMPARE(app.showManager()->indexOf(app.stagingSceneId()), 1);
        QVERIFY(get(QStringLiteral("/api/scene/prev")).contains("ok"));
        QVERIFY(get(QStringLiteral("/api/stage?id=") + second).contains("ok"));
        QCOMPARE(app.stagingSceneId(), second);
        QVERIFY(get(QStringLiteral("/api/status")).contains(second.toUtf8()));
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/scene/take?id=x")),
                 RemoteProtocol::Route::SceneTake);
    }

    void autoAdvanceStagesAndTakesNextScene() {
        QTemporaryDir dir;
        AppController app(nullptr);
        const QString a = addClipScene(app, dir.filePath(QStringLiteral("a.png")), Qt::red);
        const QString b = addClipScene(app, dir.filePath(QStringLiteral("b.png")), Qt::green);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        app.setTransitionDuration(0.05);
        app.setSceneProps(a, {{"endAction", "autoAdvance"}, {"autoAdvanceMs", 100}});

        QVERIFY(app.stageScene(a));
        QTRY_VERIFY(app.compositor()->sourceStaging());
        app.cut();
        QTRY_COMPARE(app.liveSceneId(), a);

        QTRY_COMPARE_WITH_TIMEOUT(app.liveSceneId(), b, 5000);
        QCOMPARE(app.stagingSceneId(), a);
    }

    void globalsRejectBadDuplicateAndBuiltinNames() {
        ShowManager show;
        QCOMPARE(show.addGlobal(QStringLiteral("speed")), QStringLiteral("speed"));
        QCOMPARE(show.addGlobal(QStringLiteral("_x1")), QStringLiteral("_x1"));
        QCOMPARE(show.addGlobal(), QStringLiteral("global3"));
        for (const char *bad : {"1a", "a b", "a-b", "a.b", "caf\u00e9", "speed", "bpm", "beat", "t", "audio",
                                "param", "mod", "globals", "end", "nil"})
            QVERIFY2(show.addGlobal(QString::fromUtf8(bad)).isEmpty(), bad);
        QVERIFY(show.addGlobal(QStringLiteral("ok"), 0, 1, 1).isEmpty());
        QCOMPARE(show.globals().size(), 3);

        QVERIFY(show.renameGlobal(QStringLiteral("speed"), QStringLiteral("rate")));
        QCOMPARE(show.globals().first().name, QStringLiteral("rate"));
        QVERIFY(!show.renameGlobal(QStringLiteral("rate"), QStringLiteral("_x1")));
        QVERIFY(!show.renameGlobal(QStringLiteral("rate"), QStringLiteral("bpm")));
        QVERIFY(!show.renameGlobal(QStringLiteral("rate"), QStringLiteral("2x")));
        QVERIFY(!show.renameGlobal(QStringLiteral("nope"), QStringLiteral("zzz")));
        QVERIFY(show.renameGlobal(QStringLiteral("rate"), QStringLiteral("rate")));
    }

    void globalsClampUndoAndRemove() {
        ShowManager show;
        show.addGlobal(QStringLiteral("g"), 5.0, 0.0, 2.0);
        QCOMPARE(show.globals().first().value, 2.0);
        QVERIFY(show.setGlobalValue(QStringLiteral("g"), -4.0));
        QCOMPARE(show.globals().first().value, 0.0);
        QVERIFY(show.setGlobalValue(QStringLiteral("g"), 1.5));
        QVERIFY(show.setGlobalValue(QStringLiteral("g"), 1.25));
        QCOMPARE(show.globals().first().value, 1.25);
        show.undoStack()->undo();
        QCOMPARE(show.globals().first().value, 2.0);

        QVERIFY(show.setGlobalValue(QStringLiteral("g"), 1.0));
        QVERIFY(!show.setGlobalRange(QStringLiteral("g"), 3.0, 3.0));
        QVERIFY(show.setGlobalRange(QStringLiteral("g"), 0.0, 0.5));
        QCOMPARE(show.globals().first().value, 0.5);

        QVERIFY(show.removeGlobal(QStringLiteral("g")));
        QVERIFY(show.globals().isEmpty());
        show.undoStack()->undo();
        QCOMPARE(show.globals().size(), 1);
        QCOMPARE(show.globals().first().max, 0.5);
    }

    void expressionsReadGlobalsWithoutRebuild() {
        ShowManager show;
        ClipGraph *g = show.liveScene()->graph();
        const NodeId fx = g->addNode(NT::Process, {}, {{"effect", 6}, {"params", QVariantMap{{"opacity", 0.5}}}});
        g->setParams(fx, {{"expressions", QVariantMap{{"params.opacity", QStringLiteral("globals.speed")}}}});
        auto opacity = [&] { return g->node(fx)->params.value("params").toMap().value("opacity").toDouble(); };

        show.addGlobal(QStringLiteral("speed"), 0.25);
        QTRY_VERIFY(std::abs(opacity() - 0.25) < 1e-9);
        show.setGlobalValue(QStringLiteral("speed"), 0.75);
        QTRY_VERIFY(std::abs(opacity() - 0.75) < 1e-9);
    }
};

QTEST_MAIN(TestShow)
#include "test_show.moc"
