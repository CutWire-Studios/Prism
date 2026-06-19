#include "core/CameraSource.h"

#include <QtTest>
#include <QApplication>
#include <QMediaDevices>

class TestCameraSource : public QObject {
    Q_OBJECT

private slots:
    void defaultConstructStopDestruct() {
        {
            CameraSource cam;
            cam.stop();
        }
        {
            CameraSource cam;
            cam.stop();
            cam.stop();
        }
    }

    void nextFrameWhenNotDirty() {
        CameraSource cam;
        QVERIFY(!cam.nextFrame());
        QVERIFY(!cam.isCapturing());
    }

    void startStopCycle() {
        if (QMediaDevices::videoInputs().isEmpty()) {
            QSKIP("No video inputs available in this environment");
        }

        CameraSource cam;
        QVERIFY(cam.start({}));
        QCoreApplication::processEvents();
        cam.stop();
        QVERIFY(!cam.isCapturing());
        QVERIFY(cam.start({}));
        QCoreApplication::processEvents();
        cam.stop();
        QVERIFY(!cam.isCapturing());
    }

    void destructShortlyAfterStart() {
        // Queued frame delivery after delete is UB and cannot be reproduced in-process;
        // this locks teardown ordering: stop() in ~CameraSource() before member destruction.
        if (QMediaDevices::videoInputs().isEmpty()) {
            QSKIP("No video inputs available in this environment");
        }

        {
            CameraSource cam;
            cam.start({});
            QCoreApplication::processEvents();
        }
        QCoreApplication::processEvents();
        QVERIFY(true);
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    TestCameraSource tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_camerasource.moc"
