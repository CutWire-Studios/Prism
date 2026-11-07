#include "TestSupport.h"
#include "core/media/AudioDecoder.h"
#include "core/media/AudioAnalyzer.h"

#include <array>
#include <functional>
#include <cmath>
#include <random>
#include <vector>
#include "core/media/AudioLevelMeter.h"
#include "core/graph/ClipGraph.h"

#include <QtTest>
#include <QTemporaryDir>
#include <cmath>

namespace {

// Kick drum: a 120 -> 50 Hz sweep with a ~55 ms decay, every 60 / bpm seconds.
double kick(double t, double bpm) {
    const double tk = std::fmod(t, 60.0 / bpm);
    return std::exp(-tk * 18) * std::sin(2 * M_PI * (50 * tk + 70 * (1 - std::exp(-tk * 30)) / 30));
}

struct Pulses {
    int beats = 0;   // tempo-tracked beats counted from `from` seconds on
    int kicks = 0;   // kick hits over the whole signal
    float tempo = 0.f;
};

// Runs `seconds` of signal(t, unit noise) through an analyzer in 1024-frame chunks, like live
// capture.
Pulses analyze(const std::function<double(double, double)> &signal, double seconds = 8.0, double from = 0.0) {
    const int sr = 44100, frames = int(seconds * sr), chunk = 1024;
    std::mt19937 rng(1);
    std::normal_distribution<double> noise(0.0, 1.0);
    AudioAnalyzer analyzer;
    Pulses out;
    if (!analyzer.beginLive())
        return {-1, -1, -1.f};
    float prevBeat = 0.f, prevKick = 0.f;
    QByteArray pcm(chunk * 2 * int(sizeof(float)), Qt::Uninitialized);
    auto *f = reinterpret_cast<float *>(pcm.data());
    for (int off = 0; off + chunk <= frames; off += chunk) {
        for (int i = 0; i < chunk; ++i)
            f[2 * i] = f[2 * i + 1] = float(signal(double(off + i) / sr, noise(rng)));
        analyzer.feedStereoPcm(pcm);
        if (analyzer.beatPulse() > prevBeat + 0.05f && double(off) / sr >= from)
            ++out.beats;
        if (analyzer.kickPulse() > prevKick + 0.05f)
            ++out.kicks;
        prevBeat = analyzer.beatPulse();
        prevKick = analyzer.kickPulse();
    }
    out.tempo = analyzer.tempo();
    return out;
}

int countKicks(const std::function<double(double, double)> &signal) { return analyze(signal).kicks; }

} // namespace

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

    void audioAnalyzer_kicksFollowKicksOverBassLine() {
        const auto mix = [](double kickAmp, double bpm, double gain = 1.0) {
            return [=](double t, double noise) {
                return gain * (kickAmp * kick(t, bpm) + 0.25 * std::sin(2 * M_PI * 82.4 * t) + 0.05 * noise);
            };
        };
        const int at120 = countKicks(mix(0.8, 120));   // 16 kicks in 8 s
        QVERIFY2(at120 >= 15 && at120 <= 16, qPrintable(QString::number(at120)));
        const int at174 = countKicks(mix(0.8, 174));   // 24 kicks
        QVERIFY2(at174 >= 22 && at174 <= 24, qPrintable(QString::number(at174)));
        const int quiet = countKicks(mix(0.8, 120, 0.1));   // the same mix 20 dB down
        QVERIFY2(quiet >= 15 && quiet <= 16, qPrintable(QString::number(quiet)));
        QCOMPARE(countKicks(mix(0.0, 120)), 0);        // the bass line alone is not a beat
    }

    void audioAnalyzer_voiceIsNotAKickButPercussionIs() {
        // Sung phrases: harmonics of a ~210 Hz voice with vibrato, in syllables with gaps.
        const auto voice = [](double f0) {
            return [=](double t, double noise) {
                const double syl = std::fmod(t, 0.37), len = 0.26;
                const double env = syl < len ? std::min(1.0, syl / 0.03) * std::min(1.0, (len - syl) / 0.05) : 0.0;
                // Phase of f0 * step * (1 + 3 % vibrato at 5.5 Hz); the pitch steps between syllables.
                const double step = 1.0 + 0.12 * std::floor(std::fmod(t / 0.37, 4.0)) / 4.0;
                const double phase = 2 * M_PI * f0 * step * (t - 0.03 * std::cos(2 * M_PI * 5.5 * t) / (2 * M_PI * 5.5));
                double v = 0.0;
                for (int h = 1; h <= 10; ++h)
                    v += std::sin(phase * h) / h;
                return 0.3 * env * v + 0.005 * noise;
            };
        };
        // A voice above the kick band is not a kick. (A low male voice around 110 Hz still triggers:
        // its fundamental sits inside 40-150 Hz.)
        const int female = countKicks(voice(210));
        QVERIFY2(female <= 2, qPrintable(QString::number(female)));

        // Drum kit: kicks on every beat, snares on 2 and 4, toms on random eighths, hats on sixteenths.
        const auto kit = [](double t, double noise) {
            const double beat = 0.5, eighth = beat / 2, sixteenth = beat / 4;
            const double ts = std::fmod(t + beat, 2 * beat);
            const double tt = std::fmod(t, eighth);
            const int eighthIndex = int(t / eighth);
            const bool tom = (eighthIndex * 2654435761u >> 28) % 3 == 0;
            const double th = std::fmod(t, sixteenth);
            return 0.7 * kick(t, 120) + 0.45 * std::exp(-ts * 25) * noise
                 + (tom ? 0.4 * std::exp(-tt * 10) * std::sin(2 * M_PI * (110 - 20 * tt) * tt) : 0.0)
                 + 0.12 * std::exp(-th * 60) * noise;
        };
        const int drums = countKicks(kit);   // 16 kicks, plus some toms
        QVERIFY2(drums >= 15 && drums <= 26, qPrintable(QString::number(drums)));
        QVERIFY(drums > 4 * female);
    }

    void audioAnalyzer_trackedBeatLocksToTempoAndNeedsDrums() {
        // Kicks over a bass line at 120 bpm, with every fourth kick left out: the tracker needs
        // ~3 s to lock on, then keeps the pulse through the missing kicks.
        const auto groove = [](double t, double noise) {
            const bool skipped = int(t / 0.5) % 4 == 3;
            return (skipped ? 0.0 : 0.8 * kick(t, 120)) + 0.25 * std::sin(2 * M_PI * 82.4 * t) + 0.05 * noise;
        };
        const Pulses p = analyze(groove, 14.0, 6.0);
        QVERIFY2(std::abs(p.tempo - 120.f) < 4.f, qPrintable(QString::number(p.tempo)));
        // 6 s to 14 s holds 16 beats of the pulse, 12 of them with a kick.
        QVERIFY2(p.beats >= 14 && p.beats <= 17, qPrintable(QString::number(p.beats)));

        // A voice alone has a rhythm but no drums: no tracked beats, no tempo.
        const auto voice = [](double t, double noise) {
            const double syl = std::fmod(t, 0.5);
            const double env = syl < 0.3 ? std::min(1.0, syl / 0.03) * std::min(1.0, (0.3 - syl) / 0.05) : 0.0;
            double v = 0.0;
            for (int h = 1; h <= 10; ++h)
                v += std::sin(2 * M_PI * 210.0 * h * t) / h;
            return 0.3 * env * v + 0.005 * noise;
        };
        const Pulses sung = analyze(voice, 14.0);
        QCOMPARE(sung.beats, 0);
        QCOMPARE(sung.tempo, 0.f);
    }

    void audioAnalyzer_bandsFollowAbsoluteLoudness() {
        const auto bassAt = [](float amplitude) {
            AudioAnalyzer analyzer;
            if (!analyzer.beginLive())
                return std::array<float, 3>{-1.f, -1.f, -1.f};
            QByteArray pcm(44100 * 2 * int(sizeof(float)), Qt::Uninitialized);
            auto *f = reinterpret_cast<float *>(pcm.data());
            for (int i = 0; i < 44100; ++i)
                f[2 * i] = f[2 * i + 1] = amplitude * float(std::sin(2.0 * M_PI * 100.0 * i / 44100.0));
            analyzer.feedStereoPcm(pcm);
            return std::array<float, 3>{analyzer.lowBand(), analyzer.midBand(), analyzer.highBand()};
        };
        const auto loud = bassAt(1.f);
        const auto quiet = bassAt(0.01f);
        // A full-scale sine has an RMS of -3 dBFS; at 1 % it is -43 dBFS.
        QVERIFY2(loud[0] > 0.9f && loud[0] <= 1.f, qPrintable(QString::number(loud[0])));
        QVERIFY2(quiet[0] > 0.2f && quiet[0] < 0.35f, qPrintable(QString::number(quiet[0])));
        QVERIFY(loud[1] < loud[0] - 0.3f);
        QVERIFY(loud[2] < loud[0] - 0.3f);
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
