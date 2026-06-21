#include "core/sources/CameraSource.h"
#include "core/sources/CanvasSource.h"
#include "ui/canvas/VideoWidget.h"

#include <QtTest>
#include <QApplication>
#include <QMediaDevices>

class TestVideoWidget : public QObject {
    Q_OBJECT

    QWidget m_parent;

private slots:
    void clearDeckA_dropsPrimarySource() {
        VideoWidget widget(&m_parent);
        widget.adoptSourceA(std::make_unique<CanvasSource>());
        QVERIFY(widget.sourceA());

        widget.clearDeckA();
        QVERIFY(!widget.sourceA());
    }

    void clearDeckB_dropsPrimarySource() {
        VideoWidget widget(&m_parent);
        widget.adoptSourceB(std::make_unique<CanvasSource>());
        QVERIFY(widget.sourceB());

        widget.clearDeckB();
        QVERIFY(!widget.sourceB());
    }

    void clearDeckA_stopsCamera() {
        if (QMediaDevices::videoInputs().isEmpty()) {
            QSKIP("No video inputs available in this environment");
        }

        VideoWidget widget(&m_parent);
        auto cam = std::make_unique<CameraSource>();
        cam->start({});
        QCoreApplication::processEvents();
        widget.adoptSourceA(std::move(cam));
        QVERIFY(widget.sourceA());

        widget.clearDeckA();
        QVERIFY(!widget.sourceA());
    }

    void destructWithLiveSourceBeforeGlInit() {
        auto widget = std::make_unique<VideoWidget>(&m_parent);
        widget->adoptSourceA(std::make_unique<CanvasSource>());
        widget->adoptSourceB(std::make_unique<CanvasSource>());
        widget.reset();
        QVERIFY(true);
    }

    void destructWithCameraBeforeGlInit() {
        if (QMediaDevices::videoInputs().isEmpty()) {
            QSKIP("No video inputs available in this environment");
        }

        auto widget = std::make_unique<VideoWidget>(&m_parent);
        auto cam = std::make_unique<CameraSource>();
        cam->start({});
        QCoreApplication::processEvents();
        widget->adoptSourceA(std::move(cam));
        QCoreApplication::processEvents();

        widget.reset();
        QCoreApplication::processEvents();
        QVERIFY(true);
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    TestVideoWidget tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_videowidget.moc"
