#include "core/render/Compositor.h"
#include "core/sources/CameraSource.h"
#include "core/sources/CanvasSource.h"

#include <QGuiApplication>
#include <QMediaDevices>
#include <QPainter>
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
        comp.loadVideoA(path);
        QVERIFY(comp.sourceA());
        comp.setCrossfade(0.f);

        QSignalSpy spy(&comp, &Compositor::frameReady);
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 2000);

        const QImage frame = comp.programFrame();
        QVERIFY(!frame.isNull());
        QCOMPARE(frame.size(), QSize(Compositor::programWidth(), Compositor::programHeight()));
        QVERIFY(comp.programTexture() != 0);
        QVERIFY(comp.deckTexture(true) != 0);
        QCOMPARE(comp.programTextureSize(), frame.size());

        const QColor top = frame.pixelColor(frame.width() / 2, 4);
        const QColor bottom = frame.pixelColor(frame.width() / 2, frame.height() - 5);
        QVERIFY2(top.red() > 200 && top.blue() < 50, qPrintable(top.name()));
        QVERIFY2(bottom.blue() > 200 && bottom.red() < 50, qPrintable(bottom.name()));
    }

    void noConsumers_noReadback() {
        Compositor comp;
        if (!comp.glAvailable())
            QSKIP("No OpenGL context available in this environment");

        QSignalSpy spy(&comp, &Compositor::frameReady);
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 2000);
        QVERIFY(comp.programFrame().isNull());
    }
    void clearDeckA_dropsPrimarySource() {
        Compositor comp;
        comp.adoptSourceA(std::make_unique<CanvasSource>());
        QVERIFY(comp.sourceA());

        comp.clearDeckA();
        QVERIFY(!comp.sourceA());
    }

    void clearDeckB_dropsPrimarySource() {
        Compositor comp;
        comp.adoptSourceB(std::make_unique<CanvasSource>());
        QVERIFY(comp.sourceB());

        comp.clearDeckB();
        QVERIFY(!comp.sourceB());
    }

    void clearDeckA_stopsCamera() {
        if (QMediaDevices::videoInputs().isEmpty())
            QSKIP("No video inputs available in this environment");

        Compositor comp;
        auto cam = std::make_unique<CameraSource>();
        cam->start({});
        QCoreApplication::processEvents();
        comp.adoptSourceA(std::move(cam));
        QVERIFY(comp.sourceA());

        comp.clearDeckA();
        QVERIFY(!comp.sourceA());
    }

    void destructWithLiveSourceBeforeGlInit() {
        auto comp = std::make_unique<Compositor>();
        comp->adoptSourceA(std::make_unique<CanvasSource>());
        comp->adoptSourceB(std::make_unique<CanvasSource>());
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
        comp->adoptSourceA(std::move(cam));
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
