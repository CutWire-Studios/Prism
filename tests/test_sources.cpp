#include "TestSupport.h"
#include "core/sources/ImageSource.h"
#include "core/sources/CanvasSource.h"
#include "core/sources/VideoFileSource.h"
#include "core/sources/SourceDescriptor.h"
#include "core/project/ClipManager.h"
#include "ui/mainwindow/SourceFactory.h"
#include "ui/canvas/VideoWidget.h"

#include <QtTest>
#include <QTemporaryDir>

class TestSources : public QObject {
    Q_OBJECT

private slots:
    void imageSource_loadAndStatic() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("test.png"));
        QVERIFY(TestSupport::writePng(dir.path(), QStringLiteral("test.png"), Qt::cyan));

        ImageSource src;
        QVERIFY(src.load(path));
        QVERIFY(src.isReady());
        QCOMPARE(src.frameSize(), QSize(64, 48));
        QVERIFY(src.hasAlpha());
        QVERIFY(src.frameData() != nullptr);
        QVERIFY(!src.nextFrame());

        ImageSource missing;
        QVERIFY(!missing.load(dir.filePath(QStringLiteral("nope.png"))));

        ImageSource garbage;
        QFile f(dir.filePath(QStringLiteral("garbage.png")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a png");
        f.close();
        QVERIFY(!garbage.load(f.fileName()));
    }

    void canvasSource_fills() {
        CanvasSource checkered;
        QCOMPARE(checkered.frameSize(), QSize(1280, 720));
        QVERIFY(checkered.frameData() != nullptr);
        QVERIFY(!checkered.nextFrame());

        CanvasSource solid(CanvasSource::Fill::SolidColor, QSize(640, 480), Qt::yellow);
        QCOMPARE(solid.frameSize(), QSize(640, 480));
        QVERIFY(solid.isReady());
        QVERIFY(!solid.nextFrame());
    }

    void videoFileSource_decodeCycle() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString mkv = TestSupport::encodeSampleMkv(dir.path(), 30, Qt::blue);
        QVERIFY(!mkv.isEmpty());

        VideoFileSource src;
        QVERIFY(src.open(mkv));
        QVERIFY(src.isReady());
        QVERIFY(src.duration() > 0.0);

        int frames = 0;
        while (src.nextFrame()) {
            ++frames;
        }
        QVERIFY(frames > 0);
        QVERIFY(src.frameData() != nullptr);

        src.seek(0.0);
        QVERIFY(src.nextFrame());

        while (src.nextFrame()) {}
        QVERIFY(!src.nextFrame());
    }

    void clipManager_mediaPaths() {
        QVERIFY(ClipManager::isMediaPath(QStringLiteral("clip.mp4")));
        QVERIFY(ClipManager::isMediaPath(QStringLiteral("photo.png")));
        QVERIFY(ClipManager::isAudioPath(QStringLiteral("song.mp3")));
        QVERIFY(ClipManager::isAudioPath(QStringLiteral("track.flac")));
        QVERIFY(ClipManager::isAudioPath(QStringLiteral("voice.wav")));
        QVERIFY(ClipManager::isMediaPath(QStringLiteral("voice.wav")));
        QVERIFY(!ClipManager::isMediaPath(QStringLiteral("notes.txt")));
        QVERIFY(!ClipManager::isAudioPath(QStringLiteral("video.mp4")));
    }

    void audioFileDescriptor_capabilities() {
        SourceDescriptor audio;
        audio.kind = SourceDescriptor::Kind::AudioFile;
        audio.path = QStringLiteral("/tmp/test.mp3");
        QVERIFY(audio.isAudioOnlySource());
        QVERIFY(audio.isFileSource());
        QVERIFY(audio.isRepeatable());
        QVERIFY(audio.isSeekable());
        QVERIFY(audio.hasSpeedControl());
        QVERIFY(!audio.isLiveSource());
    }

    void sourceFactory_dispatch() {
        SourceDescriptor video;
        video.kind = SourceDescriptor::Kind::VideoFile;
        video.path = QStringLiteral("/nonexistent/video.mp4");
        QVERIFY(!SourceFactory::create(video));

        SourceDescriptor imageBad;
        imageBad.kind = SourceDescriptor::Kind::Image;
        imageBad.path = QStringLiteral("/nonexistent/image.png");
        QVERIFY(!SourceFactory::create(imageBad));

        SourceDescriptor transparent;
        transparent.kind = SourceDescriptor::Kind::Canvas;
        transparent.canvasFill = SourceDescriptor::CanvasFill::Transparent;
        QVERIFY(!SourceFactory::create(transparent));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(TestSupport::writePng(dir.path(), QStringLiteral("clip.png")));

        SourceDescriptor imageOk;
        imageOk.kind = SourceDescriptor::Kind::Image;
        imageOk.path = dir.filePath(QStringLiteral("clip.png"));
        auto imgSrc = SourceFactory::create(imageOk);
        QVERIFY(imgSrc);
        QVERIFY(imgSrc->isReady());

        SourceDescriptor canvasOk;
        canvasOk.kind = SourceDescriptor::Kind::Canvas;
        canvasOk.canvasFill = SourceDescriptor::CanvasFill::Checkered;
        auto canvasSrc = SourceFactory::create(canvasOk);
        QVERIFY(canvasSrc);
        QVERIFY(canvasSrc->isReady());

        SourceDescriptor ndi;
        ndi.kind = SourceDescriptor::Kind::Ndi;
        ndi.path = QStringLiteral("Fake NDI Source");
        auto ndiSrc = SourceFactory::create(ndi);
        QVERIFY(!ndiSrc);

#ifndef PRISM_HAVE_WEBRTC
        SourceDescriptor webrtc;
        webrtc.kind = SourceDescriptor::Kind::WebRtc;
        QVERIFY(!SourceFactory::create(webrtc));
#endif
    }
};

QTEST_MAIN(TestSources)
#include "test_sources.moc"
