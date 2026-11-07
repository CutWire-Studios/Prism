#include "TestSupport.h"
#include "core/media/AudioDecoder.h"
#include "core/media/AudioAnalyzer.h"
#include "core/media/AudioLevelMeter.h"
#include "core/graph/ClipGraph.h"

#include <QtTest>
#include <QTemporaryDir>
#include <cmath>

class TestAudio : public QObject {
    Q_OBJECT

private slots:
    void audioDecoder_sineWav() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("tone.wav"));
        QVERIFY(TestSupport::writeSineWav(path, 440.0, 44100, 2, 0.5));

        AudioDecoder dec;
        QVERIFY(dec.open(path));
        QVERIFY(dec.isOpen());

        int totalSamples = 0;
        while (!dec.atEnd()) {
            QByteArray chunk;
            if (!dec.decodeNextChunk(chunk)) {
                break;
            }
            const qsizetype frameBytes = static_cast<qsizetype>(sizeof(float))
                                         * AudioDecoder::kOutputChannels;
            QVERIFY((chunk.size() % frameBytes) == 0);
            totalSamples += static_cast<int>(chunk.size() / frameBytes);
        }
        QVERIFY(totalSamples > 0);

        QVERIFY(dec.seek(0.0));
        QByteArray again;
        QVERIFY(dec.decodeNextChunk(again));
        QVERIFY(!again.isEmpty());
        dec.close();
        QVERIFY(!dec.isOpen());

        AudioDecoder bad;
        QVERIFY(!bad.open(dir.filePath(QStringLiteral("missing.wav"))));
        QVERIFY(!bad.open(dir.filePath(QStringLiteral("tone.wav") + QStringLiteral(".garbage"))));
    }

    void audioAnalyzer_spectrumPeak() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("tone.wav"));
        QVERIFY(TestSupport::writeSineWav(path, 440.0, 44100, 2, 2.0));

        AudioAnalyzer analyzer;
        QVERIFY(analyzer.open(path));

        for (int i = 0; i < 400; ++i) {
            analyzer.advance(0.02);
        }

        QVERIFY(analyzer.hasData());
        QVERIFY(analyzer.level() >= 0.f && analyzer.level() <= 1.f);

        const auto &spec = analyzer.spectrum();
        QCOMPARE(static_cast<int>(spec.size()), AudioAnalyzer::kBins);

        int peakBin = 0;
        float peakVal = 0.f;
        for (int b = 0; b < AudioAnalyzer::kBins; ++b) {
            if (spec[b] > peakVal) {
                peakVal = spec[b];
                peakBin = b;
            }
        }
        QVERIFY(peakVal > 0.2f);

        const int numFftBins = AudioAnalyzer::kFftSize / 2;
        const int linearBin = static_cast<int>(440.0 * AudioAnalyzer::kFftSize
                                               / AudioDecoder::kOutputSampleRate);
        int expectedBin = -1;
        for (int b = 0; b < AudioAnalyzer::kBins; ++b) {
            const float t0 = static_cast<float>(b) / AudioAnalyzer::kBins;
            const float t1 = static_cast<float>(b + 1) / AudioAnalyzer::kBins;
            const int binLow  = std::max(1, static_cast<int>(std::pow(numFftBins, t0)));
            const int binHigh = std::min(numFftBins, static_cast<int>(std::pow(numFftBins, t1)));
            if (linearBin >= binLow && linearBin <= binHigh) {
                expectedBin = b;
                break;
            }
        }
        QVERIFY(expectedBin >= 0);
        QVERIFY(std::abs(peakBin - expectedBin) <= 2);
    }

    void levelMeter_peakAndDecay() {
        AudioLevelMeter m;
        const float pcm[] = {0.1f, -0.8f, 0.3f, 0.2f};
        m.feed(pcm, 4);
        m.feed(pcm, 2, 0.5f);
        QCOMPARE(m.poll(0.0f), 0.8f);
        // Without new audio the reading falls linearly and bottoms out at zero.
        QVERIFY(qFuzzyCompare(m.poll(0.1f), 0.8f - AudioLevelMeter::kDecayPerSecond * 0.1f));
        QCOMPARE(m.poll(10.0f), 0.0f);
        // A louder block wins over the decayed value, a quieter one does not.
        m.feed(pcm, 4);
        m.poll(0.0f);
        const float quiet[] = {0.05f};
        m.feed(quiet, 1);
        QVERIFY(m.poll(0.1f) < 0.8f);
        QCOMPARE(AudioMeterBank::toDisplay(0.0f), 0.0f);
        QCOMPARE(AudioMeterBank::toDisplay(1.0f), 1.0f);
        QVERIFY(qAbs(AudioMeterBank::toDisplay(0.1f) - 2.0f / 3.0f) < 1e-4f);
    }

    void meterBank_derivesDownstreamNodes() {
        using NT = ClipGraph::NodeType;
        ClipGraph g;
        const quint64 mic = g.addNode(NT::MasterAudioInput);
        const quint64 mixer = g.addNode(NT::AudioMixer);
        const quint64 out = g.addNode(NT::MasterAudioOutput);
        QVERIFY(g.connectPorts(mic, 0, out, 0));

        AudioMeterBank bank;
        const float pcm[] = {0.5f, -0.25f};
        bank.meter(mic)->feed(pcm, 2);
        const auto levels = bank.poll(0.0f, {&g});
        QCOMPARE(levels.value(mic), 0.5f);
        QCOMPARE(levels.value(out), 0.5f);
        QCOMPARE(levels.value(mixer), 0.0f);
        QVERIFY(bank.poll(10.0f, {&g}).isEmpty());
    }
};

QTEST_MAIN(TestAudio)
#include "test_audio.moc"
