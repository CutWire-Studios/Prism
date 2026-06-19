#include "TestSupport.h"
#include "core/AudioDecoder.h"
#include "core/AudioAnalyzer.h"

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
};

QTEST_MAIN(TestAudio)
#include "test_audio.moc"
