#include "app/graph/GraphRuntime.h"
#include "app/mainwindow/SourceFactory.h"
#include "app/show/ShowManager.h"
#include "app/transitions/TransitionController.h"
#include "core/graph/ClipGraph.h"
#include "core/sources/SourceDescriptor.h"
#include "core/render/Compositor.h"
#include "core/sources/CameraSource.h"
#include "core/sources/CanvasSource.h"

#include <QGuiApplication>
#include <cmath>
#include <QMediaDevices>
#include <QPainter>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestCompositor : public QObject {
    Q_OBJECT

private slots:
    void programFrame_matchesProgramSizeAndContent() {
        Compositor comp;
        if (!comp.glAvailable())
            QSKIP("No OpenGL context available in this environment");

        QImage img(64, 64, QImage::Format_RGB888);
        img.fill(Qt::blue);
        QPainter(&img).fillRect(0, 0, 64, 32, Qt::red);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("test.png"));
        QVERIFY(img.save(path));

        comp.setProgramFrameConsumerCount(1);
        comp.loadVideoLive(path);
        QVERIFY(comp.sourceLive());
        comp.setCrossfade(0.f);

        // The CPU frame lags the GL one by a frame (PBO readback).
        QSignalSpy spy(&comp, &Compositor::frameReady);
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 1, 2000);

        const QImage frame = comp.programFrame();
        QVERIFY(!frame.isNull());
        QCOMPARE(frame.size(), QSize(Compositor::programWidth(), Compositor::programHeight()));
        QVERIFY(comp.programTexture() != 0);
        QVERIFY(comp.deckTexture(Deck::Live) != 0);
        QCOMPARE(comp.programTextureSize(), frame.size());

        const QColor top = frame.pixelColor(frame.width() / 2, 4);
        const QColor bottom = frame.pixelColor(frame.width() / 2, frame.height() - 5);
        QVERIFY2(top.red() > 200 && top.blue() < 50, qPrintable(top.name()));
        QVERIFY2(bottom.blue() > 200 && bottom.red() < 50, qPrintable(bottom.name()));
    }

    void sceneSource_rendersNestedSceneThroughSceneNode() {
        ShowManager show;
        Compositor comp;
        if (!comp.glAvailable())
            QSKIP("No OpenGL context available in this environment");

        Scene *inner = show.addScene(QStringLiteral("Inner"));
        Scene *outer = show.addScene(QStringLiteral("Outer"));

        SourceDescriptor solid;
        solid.kind = SourceDescriptor::Kind::Canvas;
        solid.canvasFill = SourceDescriptor::CanvasFill::Color;
        solid.color = Qt::red;
        const NodeId solidNode = inner->graph()->addInputNode(solid);
        QVERIFY(inner->graph()->connectNodes(solidNode, inner->graph()->outputNodeId()));

        SourceDescriptor ref;
        ref.kind = SourceDescriptor::Kind::Scene;
        ref.sceneId = inner->id();
        const NodeId refNode = outer->graph()->addInputNode(ref);
        QVERIFY(outer->graph()->connectNodes(refNode, outer->graph()->outputNodeId()));

        SourceFactory::setSceneResolver([&show](const QString &id) -> std::unique_ptr<MediaSource> {
            const Scene *target = show.scene(id);
            if (!target)
                return nullptr;
            ClipGraph *g = target->graph();
            const ClipGraph::ResolvedStream stream = g->evaluateVideoInput(g->outputSingleProducer());
            if (stream.layers.isEmpty())
                return nullptr;
            return SourceFactory::buildLayerSource(stream.layers.first(), g, target->runtime());
        });
        const auto resetResolver = qScopeGuard([] { SourceFactory::setSceneResolver({}); });

        const ClipGraph::ResolvedStream stream =
            outer->graph()->evaluateVideoInput(outer->graph()->outputSingleProducer());
        QCOMPARE(stream.layers.size(), 1);
        auto src = SourceFactory::buildLayerSource(stream.layers.first(), outer->graph(), outer->runtime());
        QVERIFY(src);
        QVERIFY(src->isReady());

        comp.setProgramFrameConsumerCount(1);
        comp.setSourceLive(std::move(src));
        comp.playLive();
        comp.setCrossfade(0.f);

        QSignalSpy spy(&comp, &Compositor::frameReady);
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 1, 2000);

        const QImage frame = comp.programFrame();
        QVERIFY(!frame.isNull());
        const QColor centre = frame.pixelColor(frame.width() / 2, frame.height() / 2);
        QVERIFY2(centre.red() > 200 && centre.green() < 50 && centre.blue() < 50, qPrintable(centre.name()));
    }

    void sceneSource_deletedSceneRendersEmpty() {
        ShowManager show;
        Scene *outer = show.addScene(QStringLiteral("Outer"));
        SourceDescriptor ref;
        ref.kind = SourceDescriptor::Kind::Scene;
        ref.sceneId = QStringLiteral("gone");
        const NodeId refNode = outer->graph()->addInputNode(ref);
        SourceFactory::setSceneResolver([](const QString &) -> std::unique_ptr<MediaSource> { return nullptr; });
        const auto resetResolver = qScopeGuard([] { SourceFactory::setSceneResolver({}); });

        auto src = SourceFactory::create(outer->graph()->sourceDescriptor(refNode));
        QVERIFY(src);
        QVERIFY(src->isReady());
        QVERIFY(src->hasAlpha());
        QVERIFY(!src->nextFrame());
        QCOMPARE(src->frameData()[3], uint8_t(0));
    }

    void noConsumers_noReadback() {
        Compositor comp;
        if (!comp.glAvailable())
            QSKIP("No OpenGL context available in this environment");

        QSignalSpy spy(&comp, &Compositor::frameReady);
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 2000);
        QVERIFY(comp.programFrame().isNull());
    }
    void take_swapsSlotContentsAndResetsProgress() {
        Compositor comp;
        auto live = std::make_unique<CanvasSource>();
        auto staging = std::make_unique<CanvasSource>();
        MediaSource *liveSrc = live.get();
        MediaSource *stagingSrc = staging.get();
        comp.adoptSourceLive(std::move(live));
        comp.adoptSourceStaging(std::move(staging));
        TransitionController tc(&comp);
        tc.setTransitionDuration(0.05);
        QSignalSpy started(&tc, &TransitionController::takeStarted);
        QSignalSpy committed(&tc, &TransitionController::takeCommitted);

        tc.autoTransition();
        QTRY_COMPARE(committed.count(), 1);
        QVERIFY(!tc.isRunning());
        QCOMPARE(tc.faderValue(), 0);
        QCOMPARE(started.count(), 1);
        QCOMPARE(comp.sourceLive(), stagingSrc);
        QCOMPARE(comp.sourceStaging(), liveSrc);
        QCOMPARE(comp.crossfade(), 0.f);

        tc.autoTransition();
        QTRY_COMPARE(committed.count(), 2);
        QCOMPARE(comp.sourceLive(), liveSrc);
        QCOMPARE(comp.sourceStaging(), stagingSrc);
        QCOMPARE(comp.crossfade(), 0.f);
    }

    void abortForRetarget_resetsProgressWithoutCommitOrCancel() {
        Compositor comp;
        comp.adoptSourceLive(std::make_unique<CanvasSource>());
        comp.adoptSourceStaging(std::make_unique<CanvasSource>());
        TransitionController tc(&comp);
        tc.setTransitionDuration(2.0);
        QSignalSpy started(&tc, &TransitionController::takeStarted);
        QSignalSpy committed(&tc, &TransitionController::takeCommitted);
        QSignalSpy cancelled(&tc, &TransitionController::takeCancelled);

        tc.autoTransition();
        QTRY_VERIFY_WITH_TIMEOUT(tc.progress() > 0.3f, 3000);
        QCOMPARE(started.count(), 1);

        tc.abortForRetarget();
        QVERIFY(!tc.isRunning());
        QCOMPARE(tc.progress(), 0.f);
        QCOMPARE(tc.faderValue(), 0);
        QCOMPARE(comp.crossfade(), 0.f);
        QCOMPARE(committed.count(), 0);
        QCOMPARE(cancelled.count(), 0);

        tc.autoTransition();
        QVERIFY(tc.isRunning());
        QTRY_VERIFY_WITH_TIMEOUT(tc.progress() > 0.f, 3000);
        QVERIFY(tc.progress() < 0.3f);
        QCOMPARE(started.count(), 2);
    }

    void cut_isImmediate() {
        Compositor comp;
        comp.adoptSourceLive(std::make_unique<CanvasSource>());
        comp.adoptSourceStaging(std::make_unique<CanvasSource>());
        TransitionController tc(&comp);
        tc.setTransitionDuration(5.0);
        QSignalSpy committed(&tc, &TransitionController::takeCommitted);
        tc.cutTransition();
        QCOMPARE(committed.count(), 1);
        QVERIFY(!tc.isRunning());
        QCOMPARE(tc.faderValue(), 0);
    }

    void takeAndCut_noOpWhenStagingEmpty() {
        Compositor comp;
        comp.adoptSourceLive(std::make_unique<CanvasSource>());
        TransitionController tc(&comp);
        tc.autoTransition();
        QSignalSpy committed(&tc, &TransitionController::takeCommitted);
        tc.cutTransition();
        QCOMPARE(committed.count(), 0);
        QVERIFY(!tc.isRunning());
        QVERIFY(comp.crossfade() < 0.01f);
    }

    void manualFader_reachingEndCommitsTake() {
        Compositor comp;
        comp.adoptSourceLive(std::make_unique<CanvasSource>());
        comp.adoptSourceStaging(std::make_unique<CanvasSource>());
        TransitionController tc(&comp);
        QSignalSpy committed(&tc, &TransitionController::takeCommitted);
        tc.setFaderValue(40);
        QCOMPARE(committed.count(), 0);
        QCOMPARE(tc.faderValue(), 40);
        tc.setFaderValue(100);
        QCOMPARE(committed.count(), 1);
        QCOMPARE(tc.faderValue(), 0);
        QCOMPARE(comp.crossfade(), 0.f);
    }

    void manualFader_returningToZeroCancelsTake() {
        Compositor comp;
        comp.adoptSourceLive(std::make_unique<CanvasSource>());
        comp.adoptSourceStaging(std::make_unique<CanvasSource>());
        TransitionController tc(&comp);
        QSignalSpy cancelled(&tc, &TransitionController::takeCancelled);
        QSignalSpy committed(&tc, &TransitionController::takeCommitted);
        tc.setFaderValue(0);
        QCOMPARE(cancelled.count(), 0);
        tc.setFaderValue(30);
        tc.setFaderValue(0);
        QCOMPARE(cancelled.count(), 1);
        tc.setFaderValue(100);
        QCOMPARE(committed.count(), 1);
        QCOMPARE(cancelled.count(), 1);
    }

    void takeProgress_isFloatAndEndsExactlyAtOne() {
        Compositor comp;
        comp.adoptSourceLive(std::make_unique<CanvasSource>());
        comp.adoptSourceStaging(std::make_unique<CanvasSource>());
        TransitionController tc(&comp);
        tc.setTransitionDuration(0.4);
        QSignalSpy steps(&tc, &TransitionController::crossfadeChanged);
        QSignalSpy committed(&tc, &TransitionController::takeCommitted);
        tc.autoTransition();
        QTRY_COMPARE_WITH_TIMEOUT(committed.count(), 1, 3000);
        QVERIFY(steps.count() > 2);
        QCOMPARE(steps.at(steps.count() - 2).at(0).toFloat(), 1.0f);
        QCOMPARE(steps.last().at(0).toFloat(), 0.0f);
        bool fractional = false;
        for (const auto &args : steps) {
            const float v = args.at(0).toFloat() * 100.f;
            if (std::abs(v - std::round(v)) > 0.01f)
                fractional = true;
        }
        QVERIFY(fractional);
        QCOMPARE(tc.progress(), 0.f);
    }

    void easedProgress_followsInOutQuad() {
        QCOMPARE(TransitionController::easedProgress(0, 5000), 0.f);
        QCOMPARE(TransitionController::easedProgress(5000, 5000), 1.f);
        QCOMPARE(TransitionController::easedProgress(9000, 5000), 1.f);
        QVERIFY(qFuzzyCompare(TransitionController::easedProgress(2500, 5000), 0.5f));
        QVERIFY(qFuzzyCompare(TransitionController::easedProgress(1250, 5000), 0.125f));
        const float a = TransitionController::easedProgress(2000, 5000);
        const float b = TransitionController::easedProgress(2016, 5000);
        QVERIFY(b > a && (b - a) < 0.01f);
    }

    void audioGains_equalPowerCutAndDip() {
        using M = Compositor::TransitionMode;
        auto g = Compositor::audioGains(M::Crossfade, 0.f);
        QVERIFY(qFuzzyCompare(g.first, 1.f) && g.second == 0.f);
        g = Compositor::audioGains(M::Crossfade, 1.f);
        QVERIFY(g.first < 1e-6f && qFuzzyCompare(g.second, 1.f));
        g = Compositor::audioGains(M::Crossfade, 0.5f);
        QVERIFY(qAbs(g.first - 0.70710678f) < 1e-5f && qAbs(g.second - 0.70710678f) < 1e-5f);
        for (float t = 0.f; t <= 1.f; t += 0.1f) {
            g = Compositor::audioGains(M::WipeLeft, t);
            QVERIFY(qAbs(g.first * g.first + g.second * g.second - 1.f) < 1e-5f);
        }
        g = Compositor::audioGains(M::Cut, 0.49f);
        QVERIFY(g.first == 1.f && g.second == 0.f);
        g = Compositor::audioGains(M::Cut, 0.5f);
        QVERIFY(g.first == 0.f && g.second == 1.f);
        g = Compositor::audioGains(M::DipToBlack, 0.5f);
        QVERIFY(g.first == 0.f && g.second == 0.f);
        g = Compositor::audioGains(M::DipToWhite, 0.25f);
        QVERIFY(g.first > 0.5f && g.second == 0.f);
        g = Compositor::audioGains(M::DipToBlack, 1.f);
        QVERIFY(g.first == 0.f && qFuzzyCompare(g.second, 1.f));
    }

    void deckContribution_skipsOffAirDeck() {
        Compositor comp;
        comp.setCrossfade(0.f);
        QVERIFY(comp.deckContributesToProgram(Deck::Live));
        QVERIFY(!comp.deckContributesToProgram(Deck::Staging));
        QVERIFY(!comp.deckNeedsRender(Deck::Staging));
        comp.addViewer(2);
        QVERIFY(comp.deckNeedsRender(Deck::Staging));
        comp.removeViewer(2);
        QVERIFY(!comp.deckNeedsRender(Deck::Staging));
        comp.setCrossfade(0.4f);
        QVERIFY(comp.deckContributesToProgram(Deck::Live) && comp.deckContributesToProgram(Deck::Staging));
        comp.setCrossfade(1.f);
        QVERIFY(!comp.deckContributesToProgram(Deck::Live));
        QVERIFY(comp.deckContributesToProgram(Deck::Staging));
        comp.setPanicOverlay(Compositor::PanicOverlay::Blackout);
        QVERIFY(!comp.deckContributesToProgram(Deck::Live) && !comp.deckContributesToProgram(Deck::Staging));
    }

    void clearDeckLive_dropsPrimarySource() {
        Compositor comp;
        comp.adoptSourceLive(std::make_unique<CanvasSource>());
        QVERIFY(comp.sourceLive());

        comp.clearDeckLive();
        QVERIFY(!comp.sourceLive());
    }

    void clearDeckStaging_dropsPrimarySource() {
        Compositor comp;
        comp.adoptSourceStaging(std::make_unique<CanvasSource>());
        QVERIFY(comp.sourceStaging());

        comp.clearDeckStaging();
        QVERIFY(!comp.sourceStaging());
    }

    void clearDeckLive_stopsCamera() {
        if (QMediaDevices::videoInputs().isEmpty())
            QSKIP("No video inputs available in this environment");

        Compositor comp;
        auto cam = std::make_unique<CameraSource>();
        cam->start({});
        QCoreApplication::processEvents();
        comp.adoptSourceLive(std::move(cam));
        QVERIFY(comp.sourceLive());

        comp.clearDeckLive();
        QVERIFY(!comp.sourceLive());
    }

    void destructWithLiveSourceBeforeGlInit() {
        auto comp = std::make_unique<Compositor>();
        comp->adoptSourceLive(std::make_unique<CanvasSource>());
        comp->adoptSourceStaging(std::make_unique<CanvasSource>());
        comp.reset();
        QVERIFY(true);
    }

    void destructWithCameraBeforeGlInit() {
        if (QMediaDevices::videoInputs().isEmpty())
            QSKIP("No video inputs available in this environment");

        auto comp = std::make_unique<Compositor>();
        auto cam = std::make_unique<CameraSource>();
        cam->start({});
        QCoreApplication::processEvents();
        comp->adoptSourceLive(std::move(cam));
        QCoreApplication::processEvents();

        comp.reset();
        QCoreApplication::processEvents();
        QVERIFY(true);
    }
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    TestCompositor tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_compositor.moc"
