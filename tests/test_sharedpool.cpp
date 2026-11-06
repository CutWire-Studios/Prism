#include <QtTest>
#include "core/sources/SharedSourcePool.h"
#include "core/media/SharedAudioCapture.h"

namespace {

struct FakeBackend {
    explicit FakeBackend(int *alive) : alive(alive) { ++*alive; }
    ~FakeBackend() { --*alive; }
    int *alive;
    QImage image{4, 4, QImage::Format_RGB888};
    quint64 gen = 0;
};

class FakeAudio : public SharedAudioCapture::Backend {
public:
    bool configure(const QString &device, const SharedAudioCapture::Settings &s,
                   SharedAudioCapture::PcmFn p, SharedAudioCapture::PcmFn a) override {
        const bool restart = !running || deviceId != device;
        deviceId = device;
        settings = s;
        program = std::move(p);
        analysis = std::move(a);
        return restart;
    }
    void start() override { running = true; ++starts; }
    void stop() override { running = false; }
    bool isRunning() const override { return running; }

    void emitPcm(const QByteArray &pcm) { program(pcm); analysis(pcm); }

    bool running = false;
    int starts = 0;
    QString deviceId;
    SharedAudioCapture::Settings settings;
    SharedAudioCapture::PcmFn program, analysis;
};

}

class TestSharedPool : public QObject {
    Q_OBJECT

    static std::shared_ptr<FakeBackend> acquire(const QString &key, int *alive) {
        return SharedSourcePool::instance().acquire<FakeBackend>(
            key, [alive] { return std::make_shared<FakeBackend>(alive); });
    }

private slots:
    void sameKeySharesBackend() {
        int alive = 0;
        auto a = acquire(QStringLiteral("t:cam0"), &alive);
        auto b = acquire(QStringLiteral("t:cam0"), &alive);
        QVERIFY(a);
        QCOMPARE(a.get(), b.get());
        QCOMPARE(alive, 1);
    }

    void lastConsumerDestroysBackend() {
        int alive = 0;
        auto a = acquire(QStringLiteral("t:cam1"), &alive);
        auto b = acquire(QStringLiteral("t:cam1"), &alive);
        a.reset();
        QCOMPARE(alive, 1);
        b.reset();
        QCOMPARE(alive, 0);
        auto c = acquire(QStringLiteral("t:cam1"), &alive);
        QCOMPARE(alive, 1);
    }

    void differentKeysAreDistinct() {
        int alive = 0;
        auto a = acquire(QStringLiteral("t:a"), &alive);
        auto b = acquire(QStringLiteral("t:b"), &alive);
        QVERIFY(a.get() != b.get());
        QCOMPARE(alive, 2);
    }

    void failedFactoryIsNotCached() {
        auto &pool = SharedSourcePool::instance();
        auto none = pool.acquire<FakeBackend>(QStringLiteral("t:fail"),
                                              [] { return std::shared_ptr<FakeBackend>(); });
        QVERIFY(!none);
        int alive = 0;
        QVERIFY(acquire(QStringLiteral("t:fail"), &alive));
    }

    void deviceMapRemapsKeys() {
        auto &pool = SharedSourcePool::instance();
        pool.setDeviceResolver([](const QString &l) {
            return l == QLatin1String("Front Cam") ? QStringLiteral("/dev/video4") : l;
        });
        int alive = 0;
        auto a = acquire(QStringLiteral("t:") + pool.resolve(QStringLiteral("Front Cam")), &alive);
        auto b = acquire(QStringLiteral("t:/dev/video4"), &alive);
        QCOMPARE(a.get(), b.get());
        QCOMPARE(pool.resolve(QStringLiteral("other")), QStringLiteral("other"));
        pool.setDeviceResolver({});
    }

    void consumersPullByGeneration() {
        int alive = 0;
        auto backend = acquire(QStringLiteral("t:frames"), &alive);
        auto make = [&] {
            SharedFrameSource::Producer p;
            p.keepAlive = backend;
            p.poll = [b = backend.get()] { return b->gen; };
            p.frame = [b = backend.get()]() -> const QImage & { return b->image; };
            return SharedFrameSource(MediaSource::Type::Ndi, std::move(p), QStringLiteral("x"));
        };
        SharedFrameSource c1 = make(), c2 = make();
        QVERIFY(!c1.nextFrame());
        QVERIFY(!c1.isReady());
        backend->gen = 1;
        QVERIFY(c1.nextFrame());
        QVERIFY(c1.isReady());
        QVERIFY(!c1.nextFrame());
        QVERIFY(c2.nextFrame());
        QCOMPARE(c1.frameSize(), QSize(4, 4));
        QVERIFY(!c1.isMissing());
    }

    void missingSourceIsBlackAndReportsMissing() {
        MissingSource m(MediaSource::Type::Camera, QStringLiteral("Cam"));
        QVERIFY(m.isReady());
        QVERIFY(m.isMissing());
        QVERIFY(!m.nextFrame());
        QVERIFY(!m.frameSize().isEmpty());
        const uint8_t *px = m.frameData();
        QVERIFY(px);
        for (int i = 0; i < 16 * 9 * 3; ++i)
            QCOMPARE(int(px[i]), 0);
    }

    void audioFansOutToEveryConsumer() {
        auto fake = std::make_unique<FakeAudio>();
        FakeAudio *raw = fake.get();
        int programChunks = 0;
        SharedAudioCapture cap(std::move(fake), [&](const QByteArray &) { ++programChunks; });

        int a = 0, b = 0;
        cap.setConsumers({[&](const QByteArray &) { ++a; }, [&](const QByteArray &) { ++b; }});
        SharedAudioCapture::Settings s;
        s.volume = 70;
        cap.update(QStringLiteral("mic0"), s);
        QCOMPARE(raw->starts, 1);

        raw->emitPcm(QByteArray(8, 'x'));
        QCOMPARE(a, 1);
        QCOMPARE(b, 1);
        QCOMPARE(programChunks, 1);

        s.volume = 20;
        cap.update(QStringLiteral("mic0"), s);
        QCOMPARE(raw->starts, 1);
        QCOMPARE(raw->settings.volume, 20);

        cap.setConsumers({[&](const QByteArray &) { ++a; }});
        raw->emitPcm(QByteArray(8, 'x'));
        QCOMPARE(a, 2);
        QCOMPARE(b, 1);

        cap.update(QStringLiteral("mic1"), s);
        QCOMPARE(raw->starts, 2);
    }
};

QTEST_MAIN(TestSharedPool)
#include "test_sharedpool.moc"
