#include "TestSupport.h"
#include "ui/recording/ProgramRecorder.h"
#include "ui/recording/ProgramAudioRecorder.h"
#include "ui/output/OutputHub.h"
#include "ui/output/ProgramFrameSource.h"
#include "ui/canvas/VideoWidget.h"

#include <QtTest>
#include <QSignalSpy>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>

class FakeProgramFrameSource : public ProgramFrameSource {
public:
    QImage programFrame() const override { return m_program; }
    QImage deckProgramFrame(bool deckA) const override { return deckA ? m_deckA : m_deckB; }
    void setProgramFrameConsumerCount(int count) override { m_programConsumers = count; }
    void setDeckFrameConsumerCount(int count) override { m_deckConsumers = count; }
    void captureOutputFrameNow() override { ++m_captureCalls; }

    int programConsumerCount() const { return m_programConsumers; }
    int deckConsumerCount() const { return m_deckConsumers; }
    int captureCalls() const { return m_captureCalls; }

    QImage m_program = TestSupport::makeSolidImage(VideoWidget::kProgramWidth,
                                                   VideoWidget::kProgramHeight, Qt::red);
    QImage m_deckA   = TestSupport::makeSolidImage(VideoWidget::kProgramWidth,
                                                   VideoWidget::kProgramHeight, Qt::green);
    QImage m_deckB   = TestSupport::makeSolidImage(VideoWidget::kProgramWidth,
                                                   VideoWidget::kProgramHeight, Qt::blue);
    int m_programConsumers = 0;
    int m_deckConsumers    = 0;
    int m_captureCalls     = 0;
};

class TestRecording : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
    }

    void programRecorder_basicCycle() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        ProgramRecorder recorder;
        const QString path = dir.filePath(QStringLiteral("out.mkv"));
        QVERIFY(recorder.startRecording(path, QStringLiteral("Program"), false));

        const QImage frame = TestSupport::makeSolidImage(VideoWidget::kProgramWidth,
                                                         VideoWidget::kProgramHeight);
        for (int i = 0; i < 10; ++i) {
            recorder.submitFrame(frame);
        }

        recorder.stopRecording();
        QVERIFY(!recorder.isRecording());
        QVERIFY(QFileInfo(path).size() > 0);
    }

    void programRecorder_zeroFramesDiscarded() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        ProgramRecorder recorder;
        const QString path = dir.filePath(QStringLiteral("empty.mkv"));
        QSignalSpy errors(&recorder, &ProgramRecorder::errorOccurred);

        QVERIFY(recorder.startRecording(path, QStringLiteral("Program"), false));
        recorder.stopRecording();

        QVERIFY(!QFile::exists(path));
        QCOMPARE(errors.count(), 1);
        QVERIFY(!recorder.isRecording());
    }

    void programRecorder_doubleStopSafe() {
        ProgramRecorder recorder;
        recorder.stopRecording();
        recorder.stopRecording();
        QVERIFY(!recorder.isRecording());
    }

    void programRecorder_destructWhileRecording() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString path = dir.filePath(QStringLiteral("dtor.mkv"));
        {
            ProgramRecorder recorder;
            QVERIFY(recorder.startRecording(path, QStringLiteral("Program"), false));
            recorder.submitFrame(TestSupport::makeSolidImage(VideoWidget::kProgramWidth,
                                                             VideoWidget::kProgramHeight));
        }
        QVERIFY(QFileInfo(path).size() > 0);
    }

    void programRecorder_restartAndEdgeCases() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        ProgramRecorder recorder;
        const QString path1 = dir.filePath(QStringLiteral("a.mkv"));
        const QString path2 = dir.filePath(QStringLiteral("b.mkv"));
        const QImage frame = TestSupport::makeSolidImage(VideoWidget::kProgramWidth,
                                                         VideoWidget::kProgramHeight);

        QVERIFY(recorder.startRecording(path1, QStringLiteral("A"), false));
        recorder.submitFrame(frame);
        QVERIFY(recorder.startRecording(path2, QStringLiteral("B"), false));
        recorder.submitFrame(frame);
        recorder.stopRecording();
        QVERIFY(QFileInfo(path2).size() > 0);

        recorder.submitFrame(frame);
        QVERIFY(!recorder.isRecording());

        QImage wrongSize(640, 360, QImage::Format_ARGB32);
        wrongSize.fill(Qt::magenta);
        QVERIFY(recorder.startRecording(dir.filePath(QStringLiteral("scaled.mkv")),
                                        QStringLiteral("scaled"), false));
        recorder.submitFrame(wrongSize);
        recorder.stopRecording();

        ProgramRecorder badPath;
        QVERIFY(!badPath.startRecording(QStringLiteral("/nonexistent_dir_xyz/out.mkv"),
                                        QStringLiteral("bad"), false));
    }

    void programRecorder_pathHelpers() {
        const QString path = ProgramRecorder::makeOutputPath(
            QStringLiteral("/tmp/out"), QStringLiteral("2024-01-01"), QStringLiteral("deckA"));
        QCOMPARE(path, QStringLiteral("/tmp/out/2024-01-01_deckA.mkv"));

        const QString programOnly = ProgramRecorder::makeOutputPath(
            QStringLiteral("/tmp/out"), QStringLiteral("stamp"), QString());
        QCOMPARE(programOnly, QStringLiteral("/tmp/out/stamp.mkv"));

        QVERIFY(!ProgramRecorder::defaultOutputDir().isEmpty());
        QVERIFY(ProgramRecorder::defaultOutputPath().endsWith(QStringLiteral(".mkv")));
    }

    void programRecorder_buildMarkersJson() {
        ProgramRecorder::Marker m1;
        m1.timeMs = 100;
        m1.label = QStringLiteral("intro");
        ProgramRecorder::Marker m2;
        m2.timeMs = 500;
        m2.label = QStringLiteral("outro");

        const QJsonDocument doc = ProgramRecorder::buildMarkersJson(
            QStringLiteral("/tmp/video.mkv"),
            {m1, m2},
            QStringLiteral("Program"),
            1000,
            30);

        const QJsonObject root = doc.object();
        QCOMPARE(root.value(QStringLiteral("video")).toString(), QStringLiteral("/tmp/video.mkv"));
        QCOMPARE(root.value(QStringLiteral("track")).toString(), QStringLiteral("Program"));
        QCOMPARE(root.value(QStringLiteral("durationMs")).toInteger(), qint64(1000));
        QCOMPARE(root.value(QStringLiteral("frameRate")).toInt(), 30);

        const QJsonArray markers = root.value(QStringLiteral("markers")).toArray();
        QCOMPARE(markers.size(), 2);
        QCOMPARE(markers.at(0).toObject().value(QStringLiteral("label")).toString(),
                 QStringLiteral("intro"));
    }

    void programRecorder_markersAndSignals() {
        QTemporaryDir dir;
        ProgramRecorder recorder;
        QSignalSpy recordingChanged(&recorder, &ProgramRecorder::recordingChanged);

        const QString path = dir.filePath(QStringLiteral("marked.mkv"));
        QVERIFY(recorder.startRecording(path, QStringLiteral("Program"), true));
        QCOMPARE(recordingChanged.count(), 1);

        recorder.addMarker(QStringLiteral("intro"));
        recorder.submitFrame(TestSupport::makeSolidImage(VideoWidget::kProgramWidth,
                                                         VideoWidget::kProgramHeight));
        recorder.addMarker(QStringLiteral("outro"));
        recorder.stopRecording();

        QCOMPARE(recordingChanged.count(), 2);
        QVERIFY(!recorder.isRecording());
        QCOMPARE(recorder.capturedFrameCount(), 1);
        QVERIFY(recorder.recordingDurationMs() >= 0);
        QVERIFY(QFileInfo::exists(recorder.markersPath()));

        QFile markersFile(recorder.markersPath());
        QVERIFY(markersFile.open(QIODevice::ReadOnly));
        const QJsonObject markers = QJsonDocument::fromJson(markersFile.readAll()).object();
        QCOMPARE(markers.value(QStringLiteral("track")).toString(), QStringLiteral("Program"));
        QCOMPARE(markers.value(QStringLiteral("markers")).toArray().size(), 2);
    }

    void outputHub_programAudioRecording() {
        OutputHub hub;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        hub.setOutputDir(dir.path());

        QVERIFY(hub.startProgramAudioRecording());
        QVERIFY(hub.isProgramAudioRecording());

        QByteArray pcm;
        pcm.resize(ProgramAudioRecorder::kChannels * static_cast<int>(sizeof(float)) * 1024);
        auto *samples = reinterpret_cast<float *>(pcm.data());
        for (int i = 0; i < 1024 * ProgramAudioRecorder::kChannels; ++i)
            samples[i] = 0.1f * std::sin(static_cast<float>(i) * 0.01f);

        for (int i = 0; i < 20; ++i)
            hub.submitProgramAudioChunk(0, pcm);

        hub.stopProgramAudioRecording();
        QVERIFY(!hub.isProgramAudioRecording());

        const QStringList files = QDir(dir.path()).entryList({QStringLiteral("*.flac")}, QDir::Files);
        QVERIFY(!files.isEmpty());
        QVERIFY(QFileInfo(dir.filePath(files.first())).size() > 0);
    }

    void programAudioRecorder_buildMarkersJson() {
        ProgramAudioRecorder::Marker m1;
        m1.timeMs = 250;
        m1.label = QStringLiteral("cue");

        const QJsonDocument doc = ProgramAudioRecorder::buildMarkersJson(
            QStringLiteral("/tmp/audio.flac"),
            {m1},
            QStringLiteral("Program audio"),
            5000);

        const QJsonObject root = doc.object();
        QCOMPARE(root.value(QStringLiteral("audio")).toString(), QStringLiteral("/tmp/audio.flac"));
        QCOMPARE(root.value(QStringLiteral("sampleRate")).toInt(), 44100);
        QCOMPARE(root.value(QStringLiteral("markers")).toArray().size(), 1);
    }

    void outputHub_deckAudioRecording() {
        OutputHub hub;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        hub.setOutputDir(dir.path());

        QVERIFY(hub.startDeckAAudioRecording());
        QVERIFY(hub.isTrackRecording(OutputHub::TrackKind::DeckAAudio));

        QByteArray pcm(ProgramAudioRecorder::kChannels * static_cast<int>(sizeof(float)) * 1024, Qt::Uninitialized);
        hub.submitDeckAudioChunk(0, 42, pcm);

        hub.stopDeckAAudioRecording();
        QVERIFY(!hub.isTrackRecording(OutputHub::TrackKind::DeckAAudio));

        const QStringList files = QDir(dir.path()).entryList({QStringLiteral("*deckA*.flac")}, QDir::Files);
        QVERIFY(!files.isEmpty());
    }

    void outputHub_programRecording() {
        OutputHub hub;
        FakeProgramFrameSource fake;
        hub.setProgramSourceForTest(&fake);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        hub.setOutputDir(dir.path());

        QVERIFY(hub.startProgramRecording());
        QVERIFY(hub.isProgramRecording());
        QVERIFY(fake.programConsumerCount() > 0);

        QMetaObject::invokeMethod(&hub, "onProgramFrameReady", Qt::DirectConnection);
        for (int i = 0; i < 5; ++i) {
            QMetaObject::invokeMethod(&hub, "onProgramFrameReady", Qt::DirectConnection);
        }

        hub.stopProgramRecording();
        QVERIFY(!hub.isProgramRecording());
        QCOMPARE(fake.programConsumerCount(), 0);

        const QStringList files = QDir(dir.path()).entryList({QStringLiteral("*.mkv")}, QDir::Files);
        QVERIFY(!files.isEmpty());
        QVERIFY(QFileInfo(dir.filePath(files.first())).size() > 0);
    }

    void outputHub_stopAllRecording() {
        OutputHub hub;
        FakeProgramFrameSource fake;
        hub.setProgramSourceForTest(&fake);

        QTemporaryDir dir;
        hub.setOutputDir(dir.path());
        hub.setActiveDeckNodes(42, 0);

        QVERIFY(hub.startProgramRecording());
        QVERIFY(hub.startDeckARecording());
        QVERIFY(hub.startSourceRecording(42, QStringLiteral("cam")));

        hub.stopAllRecording();
        QVERIFY(!hub.isRecording());
        QCOMPARE(fake.programConsumerCount(), 0);
        QCOMPARE(fake.deckConsumerCount(), 0);
    }

    void outputHub_destructWhileRecording() {
        QTemporaryDir dir;
        {
            OutputHub hub;
            FakeProgramFrameSource fake;
            hub.setProgramSourceForTest(&fake);
            hub.setOutputDir(dir.path());
            QVERIFY(hub.startProgramRecording());
            QMetaObject::invokeMethod(&hub, "onProgramFrameReady", Qt::DirectConnection);
        }
        QVERIFY(true);
    }

    void outputHub_frameReadyAfterStopIsSafe() {
        OutputHub hub;
        FakeProgramFrameSource fake;
        hub.setProgramSourceForTest(&fake);

        QTemporaryDir dir;
        hub.setOutputDir(dir.path());
        hub.setActiveDeckNodes(7, 0);

        QVERIFY(hub.startSourceRecording(7, QStringLiteral("src")));
        hub.stopAllRecording();
        QMetaObject::invokeMethod(&hub, "onProgramFrameReady", Qt::DirectConnection);
        hub.stopSourceRecording(7);
        QMetaObject::invokeMethod(&hub, "onProgramFrameReady", Qt::DirectConnection);
        QVERIFY(!hub.isRecording());
    }
};

QTEST_MAIN(TestRecording)
#include "test_recording.moc"
