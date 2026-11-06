#include "app/AppController.h"
#include "app/session/SessionManager.h"
#include "app/show/ShowManager.h"
#include "core/graph/ClipGraph.h"
#include "core/sources/SourceDescriptor.h"

#include <QtTest>
#include <QImage>
#include <QJsonArray>
#include <QSignalSpy>
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
