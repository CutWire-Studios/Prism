#include "TestSupport.h"
#include "core/audio/AudioRack.h"
#include "core/audio/dsp/PedalCatalog.h"
#include "core/media/AudioDecoder.h"
#include "core/media/AudioEffectChain.h"

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <vector>

using namespace prism::audiofx;

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr int kRate = AudioEffectChain::kSampleRate;
constexpr int kStereoFrameBytes = AudioEffectChain::kChannels * static_cast<int>(sizeof(float));

// Interleaved stereo float sine, phase-continuous from startFrame.
QByteArray sineBlock(qint64 startFrame, int frames, double freq, float amp = 0.5f)
{
    QByteArray bytes(frames * kStereoFrameBytes, Qt::Uninitialized);
    float *p = reinterpret_cast<float *>(bytes.data());
    for (int i = 0; i < frames; ++i) {
        const float v = amp * static_cast<float>(std::sin(kTwoPi * freq * double(startFrame + i) / kRate));
        p[2 * i] = v;
        p[2 * i + 1] = v;
    }
    return bytes;
}

std::vector<float> leftChannel(const QByteArray &stereo)
{
    const float *p = reinterpret_cast<const float *>(stereo.constData());
    std::vector<float> out(static_cast<size_t>(stereo.size() / kStereoFrameBytes));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = p[2 * i];
    return out;
}

bool allFinite(const QByteArray &stereo)
{
    const float *p = reinterpret_cast<const float *>(stereo.constData());
    const qsizetype n = stereo.size() / static_cast<qsizetype>(sizeof(float));
    for (qsizetype i = 0; i < n; ++i)
        if (!std::isfinite(p[i]))
            return false;
    return true;
}

AudioEffectRef refFor(int nodeId, const AudioRack &rack)
{
    AudioEffectRef ref;
    ref.nodeId = nodeId;
    ref.rack = rack;
    return ref;
}

// Writes a mono 32-bit float WAV at 44.1 kHz.
bool writeFloatWav(const QString &path, const std::vector<float> &samples)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    const quint32 dataBytes = static_cast<quint32>(samples.size() * sizeof(float));
    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataBytes);
    out.writeRawData("WAVE", 4);
    out.writeRawData("fmt ", 4);
    out << quint32(16) << quint16(3) << quint16(1) << quint32(kRate) << quint32(kRate * 4)
        << quint16(4) << quint16(32);
    out.writeRawData("data", 4);
    out << dataBytes;
    out.writeRawData(reinterpret_cast<const char *>(samples.data()), int(dataBytes));
    return file.error() == QFileDevice::NoError;
}

int signChanges(const std::vector<float> &x, size_t begin, size_t end)
{
    int changes = 0;
    for (size_t i = begin + 1; i < end; ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f))
            ++changes;
    return changes;
}

} // namespace

class TestAudioFx : public QObject {
    Q_OBJECT

private slots:
    void everyPedalBuildsAndProcesses() {
        const int totalFrames = kRate / 2;
        const int chunkSizes[] = {256, 512, 1000, 1024, 4096};

        for (const PedalSpec &spec : pedalSpecs()) {
            if (std::string_view(spec.type) == "convolution")
                continue;
            const QString type = QString::fromUtf8(spec.type);

            AudioRack rack;
            QString id;
            QVERIFY2(addPedal(rack, type, {}, &id), qPrintable(type));

            QString error;
            QVERIFY2(buildGraph(rack, &error) != nullptr, qPrintable(type + ": " + error));

            for (int chunk : chunkSizes) {
                AudioEffectChain chain;
                chain.setEffects({refFor(1, rack)});
                QVERIFY2(chain.hasFilters(), qPrintable(type));

                qsizetype inBytes = 0;
                qsizetype outBytes = 0;
                for (int offset = 0; offset < totalFrames; offset += chunk) {
                    const int count = std::min(chunk, totalFrames - offset);
                    const QByteArray in = sineBlock(offset, count, 440.0);
                    QByteArray out;
                    QVERIFY2(chain.process(in, out), qPrintable(type));
                    QCOMPARE(out.size(), in.size());
                    QVERIFY2(allFinite(out), qPrintable(QStringLiteral("%1 chunk %2").arg(type).arg(chunk)));
                    inBytes += in.size();
                    outBytes += out.size();
                }
                QCOMPARE(outBytes, inBytes);
            }
        }
    }

    void convolutionWithIr() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString irPath = dir.filePath(QStringLiteral("impulse.wav"));

        std::vector<float> ir(300);
        for (size_t i = 0; i < ir.size(); ++i)
            ir[i] = (i == 0) ? 1.0f : 0.05f * static_cast<float>(std::exp(-double(i) / 50.0));
        QVERIFY(writeFloatWav(irPath, ir));

        AudioRack rack;
        QString id;
        QVERIFY(addPedal(rack, QStringLiteral("convolution"), {}, &id));
        findPedal(rack, id)->irPath = irPath;

        QString error;
        QVERIFY2(buildGraph(rack, &error) != nullptr, qPrintable(error));

        AudioEffectChain chain;
        chain.setEffects({refFor(1, rack)});
        QVERIFY(chain.hasFilters());

        const int totalFrames = kRate / 4;
        const int chunk = 1024;
        float peak = 0.0f;
        for (int offset = 0; offset < totalFrames; offset += chunk) {
            const int count = std::min(chunk, totalFrames - offset);
            const QByteArray in = sineBlock(offset, count, 440.0);
            QByteArray out;
            QVERIFY(chain.process(in, out));
            QCOMPARE(out.size(), in.size());
            QVERIFY(allFinite(out));
            for (float s : leftChannel(out))
                peak = std::max(peak, std::fabs(s));
        }
        QVERIFY(peak > 0.0f);
    }

    void splitModulatorRouteParses() {
        AudioRack rack;
        QString splitId;
        QVERIFY(addSplit(rack, RackSplit::Mode::Parallel, 2, {}, &splitId));

        QString gainId;
        QString driveId;
        QVERIFY(addPedal(rack, QStringLiteral("gain"), RackSlot{splitId, 0, -1}, &gainId));
        QVERIFY(addPedal(rack, QStringLiteral("drive"), RackSlot{splitId, 1, -1}, &driveId));

        QString modId;
        QVERIFY(addModulator(rack, QStringLiteral("lfo"), &modId));
        QString routeId;
        QVERIFY(addRoute(rack, modId, gainId, QStringLiteral("gain"), 0.4f, &routeId));

        QString error;
        QVERIFY2(buildGraph(rack, &error) != nullptr, qPrintable(error));

        const QJsonObject json = toJson(rack);
        const AudioRack reloaded = fromJson(json);
        const QJsonObject again = toJson(reloaded);
        QCOMPARE(QJsonDocument(again).toJson(QJsonDocument::Compact),
                 QJsonDocument(json).toJson(QJsonDocument::Compact));
        QCOMPARE(shapeSignature(reloaded), shapeSignature(rack));

        QVERIFY2(buildGraph(reloaded, &error) != nullptr, qPrintable(error));
        QCOMPARE(reloaded.routes.size(), 1);
        QCOMPARE(reloaded.routes[0].knob, QStringLiteral("gain"));
    }

    void shapeSignatureIgnoresValues() {
        AudioRack rack;
        QString gainId;
        QVERIFY(addPedal(rack, QStringLiteral("gain"), {}, &gainId));
        const QString before = shapeSignature(rack);

        findPedal(rack, gainId)->knobs[QStringLiteral("gain")] = -6.0f;
        QCOMPARE(shapeSignature(rack), before);

        QString delayId;
        QVERIFY(addPedal(rack, QStringLiteral("delay"), {}, &delayId));
        QVERIFY(shapeSignature(rack) != before);
    }

    void valueChangeIsContinuous() {
        AudioRack rack;
        QString gainId;
        QVERIFY(addPedal(rack, QStringLiteral("gain"), {}, &gainId));

        AudioEffectChain chain;
        chain.setEffects({refFor(1, rack)});

        const int block = 1024;
        const double freq = 440.0;
        const float amp = 0.5f;

        // Block 1 at unity gain.
        const QByteArray in1 = sineBlock(0, block, freq, amp);
        QByteArray out1;
        QVERIFY(chain.process(in1, out1));

        // Change gain to -6 dB (same node id, same shape) and process block 2.
        findPedal(rack, gainId)->knobs[QStringLiteral("gain")] = -6.0f;
        chain.setEffects({refFor(1, rack)});
        const QByteArray in2 = sineBlock(block, block, freq, amp);
        QByteArray out2;
        QVERIFY(chain.process(in2, out2));

        const std::vector<float> l1 = leftChannel(out1);
        const std::vector<float> l2 = leftChannel(out2);

        // Sine slope bound within block 1, versus the jump across the block boundary.
        float maxWithin = 0.0f;
        for (size_t i = 1; i < l1.size(); ++i)
            maxWithin = std::max(maxWithin, std::fabs(l1[i] - l1[i - 1]));
        const float boundary = std::fabs(l2.front() - l1.back());
        QVERIFY2(boundary <= 1.5f * maxWithin + 1e-4f,
                 qPrintable(QStringLiteral("boundary %1 vs in-block max %2")
                                .arg(boundary).arg(maxWithin)));

        // After 50 ms, the output level must have moved to the new gain (-6 dB ~ 0.501x).
        const int settle = kRate * 50 / 1000;
        const QByteArray in3 = sineBlock(2 * block, settle, freq, amp);
        QByteArray out3;
        QVERIFY(chain.process(in3, out3));
        const std::vector<float> l3 = leftChannel(out3);
        const std::vector<float> in3l = leftChannel(in3);

        const size_t tailBegin = l3.size() - kRate / 100; // last 10 ms
        double outEnergy = 0.0;
        double inEnergy = 0.0;
        for (size_t i = tailBegin; i < l3.size(); ++i) {
            outEnergy += double(l3[i]) * l3[i];
            inEnergy += double(in3l[i]) * in3l[i];
        }
        const double ratio = std::sqrt(outEnergy / inEnergy);
        QVERIFY2(std::fabs(ratio - 0.501) < 0.03, qPrintable(QStringLiteral("ratio %1").arg(ratio)));
    }

    void decoderSpeedPreservesPitch() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("tone2s.wav"));
        QVERIFY(TestSupport::writeSineWav(path, 440.0, 44100, 2, 2.0));

        AudioDecoder dec;
        QVERIFY(dec.open(path));
        dec.setPlaybackSpeed(2.0);
        QCOMPARE(dec.playbackSpeed(), 2.0);

        QByteArray all;
        while (!dec.atEnd()) {
            QByteArray chunk;
            if (!dec.decodeNextChunk(chunk))
                break;
            all.append(chunk);
        }

        const double frames = double(all.size()) / kStereoFrameBytes;
        // 2 s of input at 2x speed should yield about 1 s of output.
        QVERIFY2(std::fabs(frames - kRate) / kRate < 0.10,
                 qPrintable(QStringLiteral("frames %1").arg(frames)));

        const std::vector<float> left = leftChannel(all);
        const size_t begin = left.size() / 4;
        const size_t end = left.size() * 3 / 4;
        const double seconds = double(end - begin) / kRate;
        const double estimated = signChanges(left, begin, end) / 2.0 / seconds;
        QVERIFY2(std::fabs(estimated - 440.0) / 440.0 < 0.05,
                 qPrintable(QStringLiteral("estimated %1 Hz").arg(estimated)));
    }
};

QTEST_MAIN(TestAudioFx)
#include "test_audiofx.moc"
