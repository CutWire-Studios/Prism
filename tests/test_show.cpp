#include "app/AppController.h"
#include "app/session/SessionManager.h"
#include "app/remote/OscProtocol.h"
#include "app/remote/OscServer.h"
#include "app/remote/RemoteControlServer.h"
#include "app/remote/RemoteProtocol.h"
#include "app/show/CueThumbnails.h"
#include "app/show/Preflight.h"
#include "app/show/VenueProfile.h"
#include "app/show/ShowManager.h"
#include "core/graph/ClipGraph.h"
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

// A new cue holding one still clip wired to its Output node; leaves it as the edited cue.
QString addClipCue(AppController &app, const QString &path, const QColor &color)
{
    QImage img(32, 32, QImage::Format_RGB888);
    img.fill(color);
    if (!img.save(path))
        return {};
    const QString cue = app.addCue();
    app.enterCue(cue);
    const quint64 clip = app.addSourceFromMap({{QStringLiteral("kind"), int(SourceDescriptor::Kind::VideoFile)},
                                               {QStringLiteral("path"), path}});
    if (!clip || !app.clipGraph()->connectNodes(clip, app.clipGraph()->outputNodeId()))
        return {};
    return cue;
}

}  // namespace

class TestShow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void init() { QFile::remove(SessionManager::lockFilePath()); }

    void newShowStartsWithOneLiveCue() {
        ShowManager show;
        QCOMPARE(show.cues().size(), 1);
        QCOMPARE(show.liveCueId(), show.cues().first()->id());
        QVERIFY(show.stagingCueId().isEmpty());
        QCOMPARE(show.editedCueId(), show.liveCueId());
        const ClipGraph *g = show.liveCue()->graph();
        QVERIFY(g->outputNodeId() != 0);
        QVERIFY(g->audioOutputNodeId() != 0);
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
        Cue *a = show.addCue(QStringLiteral("A"));
        show.setDefaultTransition({3, 800});
        QCOMPARE(show.resolveTransition(a), (TransitionSpec{3, 800}));
        QCOMPARE(show.resolveTransition(nullptr), (TransitionSpec{3, 800}));

        a->setIncoming(TransitionSpec{5, 250});
        QCOMPARE(show.resolveTransition(a), (TransitionSpec{5, 250}));
        a->setIncoming(std::nullopt);
        QCOMPARE(show.resolveTransition(a), (TransitionSpec{3, 800}));
    }

    void stageAndSwapExchangeCues() {
        ShowManager show;
        Cue *a = show.addCue(QStringLiteral("A"));
        const QString first = show.liveCueId();
        QVERIFY(show.stageCue(a->id()));
        QCOMPARE(show.stagingCueId(), a->id());

        QSignalSpy live(&show, &ShowManager::liveCueChanged);
        QSignalSpy staging(&show, &ShowManager::stagingCueChanged);
        show.swapLiveStaging();
        QCOMPARE(show.liveCueId(), a->id());
        QCOMPARE(show.stagingCueId(), first);
        QCOMPARE(live.size(), 1);
        QCOMPARE(staging.size(), 1);

        QVERIFY(!show.stageCue(QStringLiteral("missing")));
        QVERIFY(!show.removeCue(show.liveCueId()));
        QVERIFY(show.removeCue(first));
        QVERIFY(show.stagingCueId().isEmpty());
    }

    void nodeIdsAreUniqueAcrossCues() {
        ShowManager show;
        Cue *a = show.addCue();
        Cue *b = show.addCue();
        a->graph()->addNode(NT::Input, {}, canvas(QStringLiteral("x")));
        b->graph()->addNode(NT::Input, {}, canvas(QStringLiteral("y")));
        QSet<NodeId> seen;
        for (const Cue *cue : show.cues()) {
            for (NodeId id : cue->graph()->nodeIds()) {
                QVERIFY2(!seen.contains(id), "node id reused across cues");
                QVERIFY(id > (quint64(cue->serial()) << 40));
                seen.insert(id);
            }
        }
        for (const Cue *cue : show.cues())
            QCOMPARE(show.cueForNode(cue->graph()->outputNodeId()), cue);
    }

    void duplicateGetsFreshIdsAndKeepsWiring() {
        ShowManager show;
        Cue *a = show.addCue(QStringLiteral("A"));
        const NodeId in = a->graph()->addNode(NT::Input, {}, canvas(QStringLiteral("x")));
        QVERIFY(a->graph()->connectNodes(in, a->graph()->outputNodeId()));
        a->setEndAction(Cue::EndAction::Loop);

        Cue *copy = show.duplicateCue(a->id());
        QVERIFY(copy);
        QCOMPARE(show.indexOf(copy->id()), show.indexOf(a->id()) + 1);
        QCOMPARE(copy->endAction(), Cue::EndAction::Loop);
        QCOMPARE(copy->graph()->nodeIds().size(), a->graph()->nodeIds().size());
        for (NodeId id : copy->graph()->nodeIds())
            QVERIFY(!a->graph()->hasNode(id));
        QVERIFY(copy->graph()->outputSingleProducer() != 0);
        QVERIFY(copy->graph()->addNode(NT::Layer) > copy->graph()->outputNodeId());
    }

    void jsonRoundTrip() {
        ShowManager show;
        Cue *a = show.addCue(QStringLiteral("A"));
        Cue *b = show.addCue(QStringLiteral("B"));
        const NodeId in = a->graph()->addNode(NT::Input, {}, canvas(QStringLiteral("x")));
        QVERIFY(a->graph()->connectNodes(in, a->graph()->outputNodeId()));
        a->setIncoming(TransitionSpec{2, 400});
        a->setEndAction(Cue::EndAction::AutoAdvance);
        a->setAutoAdvanceMs(1500);
        a->setAdvanceOnMediaEnd(true);
        b->setColor(QStringLiteral("#ff0000"));
        b->setNotes(QStringLiteral("hello"));
        show.setDefaultTransition({4, 900});
        QVERIFY(show.stageCue(a->id()));
        QVERIFY(show.enterCue(b->id()));

        const QJsonObject json = show.toJson();
        ShowManager loaded;
        QVERIFY(loaded.fromJson(json));
        QCOMPARE(loaded.cues().size(), 3);
        QCOMPARE(loaded.liveCueId(), show.liveCueId());
        QCOMPARE(loaded.stagingCueId(), a->id());
        QCOMPARE(loaded.editedCueId(), b->id());
        QCOMPARE(loaded.defaultTransition(), (TransitionSpec{4, 900}));

        const Cue *la = loaded.cue(a->id());
        QVERIFY(la);
        QCOMPARE(la->name(), QStringLiteral("A"));
        QVERIFY(la->incoming().has_value());
        QCOMPARE(*la->incoming(), (TransitionSpec{2, 400}));
        QCOMPARE(la->endAction(), Cue::EndAction::AutoAdvance);
        QCOMPARE(la->autoAdvanceMs(), 1500);
        QVERIFY(la->advanceOnMediaEnd());
        QVERIFY(la->graph()->hasNode(in));
        QCOMPARE(la->graph()->outputSingleProducer(), in);
        QCOMPARE(loaded.cue(b->id())->color(), QStringLiteral("#ff0000"));
        QCOMPARE(loaded.cue(b->id())->notes(), QStringLiteral("hello"));
        QVERIFY(!loaded.cue(b->id())->incoming().has_value());

        QCOMPARE(loaded.toJson(), json);

        // New nodes in a loaded cue continue above the stored ids.
        QVERIFY(loaded.cue(a->id())->graph()->addNode(NT::Layer) > a->graph()->nodeIds().last());
    }

    void oldCueGraphVersionIsRejected() {
        ShowManager show;
        show.addCue(QStringLiteral("A"));
        QJsonObject json = show.toJson();
        QJsonArray cues = json["cues"].toArray();
        QJsonObject cue = cues.at(1).toObject();
        QJsonObject graph = cue["graph"].toObject();
        graph["graphVersion"] = ClipGraph::kGraphVersion - 1;
        cue["graph"] = graph;
        cues[1] = cue;
        json["cues"] = cues;

        ShowManager loaded;
        QVERIFY(!loaded.fromJson(json));
        QCOMPARE(loaded.cues().size(), 1);

        QVERIFY(!loaded.fromJson(QJsonObject{}));
        QCOMPARE(loaded.cues().size(), 1);
    }

    void mediaEndOnlyEndsCuesThatOptedIn() {
        ShowManager show;
        QSignalSpy ended(&show, &ShowManager::liveEnded);
        show.notifyLiveMediaEnded();
        QCOMPARE(ended.size(), 0);

        Cue *live = show.liveCue();
        live->setEndAction(Cue::EndAction::AutoAdvance);
        show.notifyLiveMediaEnded();
        QCOMPARE(ended.size(), 0);

        live->setAdvanceOnMediaEnd(true);
        show.notifyLiveMediaEnded();
        QCOMPARE(ended.size(), 1);

        live->setEndAction(Cue::EndAction::Hold);
        show.notifyLiveMediaEnded();
        QCOMPARE(ended.size(), 1);
    }

    void takeSwapsLiveAndStagingCues() {
        QTemporaryDir dir;
        AppController app(nullptr);
        const QString first = app.liveCueId();
        const QString cue = addClipCue(app, dir.filePath(QStringLiteral("a.png")), Qt::red);
        QVERIFY(!cue.isEmpty());

        QVERIFY(!app.stageCue(first));   // the Live cue cannot be staged
        QVERIFY(app.stageCue(cue));
        QTRY_VERIFY(app.compositor()->sourceStaging());

        app.setTransitionDuration(0.05);
        app.take();
        QTRY_COMPARE(app.liveCueId(), cue);
        QCOMPARE(app.stagingCueId(), first);
        QVERIFY(!app.removeCue(cue));
    }

    void cueTransitionAppliesToTakeThenDefaultReturns() {
        QTemporaryDir dir;
        AppController app(nullptr);
        const QString cue = addClipCue(app, dir.filePath(QStringLiteral("t.png")), Qt::red);
        QVERIFY(!cue.isEmpty());
        app.setTransitionMode(0);
        app.setTransitionDuration(2.0);
        app.setCueProps(cue, {{"transition", QVariantMap{{"mode", 1}, {"durationMs", 50}}}});

        QVERIFY(app.stageCue(cue));
        QTRY_VERIFY(app.compositor()->sourceStaging());
        app.take();   // mode 1 is Cut: commits immediately
        QTRY_COMPARE(app.liveCueId(), cue);
        QCOMPARE(app.transitionMode(), 0);
        QCOMPARE(app.transitionDuration(), 2.0);
        QCOMPARE(app.showManager()->defaultTransition(), (TransitionSpec{0, 2000}));
    }

    void showUndoCoversAddRemoveReorderAndProps() {
        ShowManager show;
        const QString first = show.liveCueId();
        QUndoStack *undo = show.undoStack();

        Cue *a = show.addCue(QStringLiteral("A"));
        const QString aId = a->id();
        const NodeId in = a->graph()->addNode(NT::Input, {}, canvas(QStringLiteral("x")));
        QCOMPARE(show.cues().size(), 2);
        undo->undo();
        QCOMPARE(show.cues().size(), 1);
        undo->redo();
        QCOMPARE(show.cues().size(), 2);
        QVERIFY(show.cue(aId)->graph()->hasNode(in));

        Cue *b = show.addCue(QStringLiteral("B"));
        const QString bId = b->id();
        QCOMPARE(show.indexOf(bId), 2);
        QVERIFY(show.moveCue(2, 0));
        QCOMPARE(show.indexOf(bId), 0);
        undo->undo();
        QCOMPARE(show.indexOf(bId), 2);
        undo->redo();
        QCOMPARE(show.indexOf(bId), 0);
        undo->undo();

        CueMeta meta = show.cue(aId)->meta();
        meta.name = QStringLiteral("Renamed");
        meta.incoming = TransitionSpec{2, 300};
        show.setCueMeta(aId, meta);
        QCOMPARE(show.cue(aId)->name(), QStringLiteral("Renamed"));
        undo->undo();
        QCOMPARE(show.cue(aId)->name(), QStringLiteral("A"));
        QVERIFY(!show.cue(aId)->incoming());
        undo->redo();
        QCOMPARE(show.cue(aId)->incoming()->durationMs, 300);

        QVERIFY(show.removeCue(aId));
        QVERIFY(!show.cue(aId));
        undo->undo();
        QVERIFY(show.cue(aId));
        QCOMPARE(show.cue(aId)->name(), QStringLiteral("Renamed"));
        undo->redo();
        QVERIFY(!show.cue(aId));

        Cue *dup = show.duplicateCue(first);
        QVERIFY(dup);
        const QString dupId = dup->id();
        undo->undo();
        QVERIFY(!show.cue(dupId));
        undo->redo();
        QVERIFY(show.cue(dupId));
    }

    void thumbnailPathPersists() {
        QTemporaryDir dir;
        QImage img(320, 180, QImage::Format_RGB888);
        img.fill(Qt::red);
        ShowManager show;
        Cue *a = show.addCue(QStringLiteral("A"));
        const QString path = CueThumbnails::save(img, dir.filePath(QStringLiteral("cuethumbs")), a->id());
        QVERIFY(!path.isEmpty());
        const QImage saved(path);
        QCOMPARE(saved.size(), QSize(CueThumbnails::kWidth, CueThumbnails::kHeight));
        a->setThumbnail(path);

        ShowManager other;
        QVERIFY(other.fromJson(show.toJson()));
        QCOMPARE(other.cue(a->id())->thumbnail(), path);
    }

    void preflightReportsMissingFileAndDevice() {
        using Kind = SourceDescriptor::Kind;
        ShowManager show;
        Cue *a = show.addCue(QStringLiteral("A"));
        a->graph()->addNode(NT::Input, {}, {{"source", QVariantMap{{"kind", int(Kind::VideoFile)},
                                                                   {"path", QStringLiteral("/nonexistent/clip.mp4")}}}});
        a->graph()->addNode(NT::Input, {}, {{"source", QVariantMap{{"kind", int(Kind::Camera)},
                                                                   {"path", QStringLiteral("fake-cam")}}}});
        const auto issues = Preflight::run(show, QDir(), [](const SourceDescriptor &d) { return d.kind == Kind::Camera; });
        QCOMPARE(issues.size(), 2);
        int files = 0, devices = 0;
        for (const PreflightIssue &i : issues) {
            QCOMPARE(i.cueId, a->id());
            files += i.kind == PreflightIssue::Kind::MissingFile;
            devices += i.kind == PreflightIssue::Kind::MissingDevice;
        }
        QCOMPARE(files, 1);
        QCOMPARE(devices, 1);
    }

    void stageRelativeStepsFromLiveCue() {
        AppController app(nullptr);
        const QString first = app.liveCueId();
        const QString second = app.addCue();
        QVERIFY(!app.stageRelative(-1));
        QVERIFY(app.stageRelative(1));
        QCOMPARE(app.stagingCueId(), second);
        QVERIFY(!app.stageRelative(2));
        QVERIFY(app.moveCue(second, 0));
        QCOMPARE(app.showManager()->indexOf(first), 1);
        QVERIFY(app.stageRelative(-1));
        QCOMPARE(app.stagingCueId(), second);
    }

    void oscNextPrevStageRelativeToLive() {
        QSettings settings;
        settings.setValue(QStringLiteral("osc/enabled"), true);
        settings.setValue(QStringLiteral("osc/port"), 39217);
        settings.setValue(QStringLiteral("osc/feedbackHost"), QString());
        AppController app(nullptr);
        OscServer osc(&app);
        QVERIFY(osc.applySettings());
        const QString second = app.addCue();
        const QString third = app.addCue();
        const QString live = app.liveCueId();

        QUdpSocket sock;
        auto fire = [&](const QString &address, const QVariantList &args = {}) {
            sock.writeDatagram(OscProtocol::encode({address, args}), QHostAddress::LocalHost, 39217);
        };
        fire(QStringLiteral("/prism/cue/next"));
        QTRY_VERIFY(!app.stagingCueId().isEmpty());
        QCOMPARE(app.showManager()->indexOf(app.stagingCueId()), app.showManager()->indexOf(live) + 1);
        fire(QStringLiteral("/prism/cue/stage"), {3});
        QTRY_COMPARE(app.stagingCueId(), third);
        fire(QStringLiteral("/prism/cue/prev"));
        QTRY_VERIFY(app.stagingCueId() != third);
        Q_UNUSED(second);
        osc.stop();
        settings.setValue(QStringLiteral("osc/enabled"), false);
    }

    void httpCueEndpoints() {
        AppController app(nullptr);
        RemoteControlServer server(&app);
        QVERIFY(server.startServer(QHostAddress::LocalHost, 39218));
        const QString second = app.addCue();
        app.addCue();

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

        QVERIFY(get(QStringLiteral("/api/cue/next")).contains("ok"));
        QCOMPARE(app.showManager()->indexOf(app.stagingCueId()), 1);
        QVERIFY(get(QStringLiteral("/api/cue/prev")).contains("ok"));
        QVERIFY(get(QStringLiteral("/api/stage?id=") + second).contains("ok"));
        QCOMPARE(app.stagingCueId(), second);
        QVERIFY(get(QStringLiteral("/api/status")).contains(second.toUtf8()));
        QCOMPARE(RemoteProtocol::matchRoute(QStringLiteral("GET"), QStringLiteral("/api/cue/take?id=x")),
                 RemoteProtocol::Route::CueTake);
    }

    void autoAdvanceStagesAndTakesNextCue() {
        QTemporaryDir dir;
        AppController app(nullptr);
        const QString a = addClipCue(app, dir.filePath(QStringLiteral("a.png")), Qt::red);
        const QString b = addClipCue(app, dir.filePath(QStringLiteral("b.png")), Qt::green);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        app.setTransitionDuration(0.05);
        app.setCueProps(a, {{"endAction", "autoAdvance"}, {"autoAdvanceMs", 100}});

        QVERIFY(app.stageCue(a));
        QTRY_VERIFY(app.compositor()->sourceStaging());
        app.cut();
        QTRY_COMPARE(app.liveCueId(), a);

        QTRY_COMPARE_WITH_TIMEOUT(app.liveCueId(), b, 5000);
        QCOMPARE(app.stagingCueId(), a);
    }
};

QTEST_MAIN(TestShow)
#include "test_show.moc"
