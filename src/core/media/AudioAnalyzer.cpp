#include "core/media/AudioAnalyzer.h"
#include "core/media/AudioDecoder.h"

#include "kiss_fftr.h"

#include <algorithm>
#include <cmath>

static constexpr int kSampleRate = AudioDecoder::kOutputSampleRate;

AudioAnalyzer::AudioAnalyzer()
    : m_decoder(new AudioDecoder())
{
    allocateBuffers();
}

void AudioAnalyzer::allocateBuffers() {
    const int n = m_config.fftSize;
    m_ringBuffer.assign(n, 0.f);
    m_hannWindow.assign(n, 0.f);
    m_fftInput.assign(n, 0.f);
    m_spectrum.assign(m_config.binCount, 0.f);
    m_smoothedSpectrum.assign(m_config.binCount, 0.f);
    for (int i = 0; i < n; ++i) {
        const float w = static_cast<float>(i) / static_cast<float>(n - 1);
        m_hannWindow[i] = 0.5f * (1.f - std::cos(2.f * static_cast<float>(M_PI) * w));
    }
    m_ringWrite = 0;
    m_ringFilled = 0;

    if (m_fftCfg) {
        kiss_fftr_free(static_cast<kiss_fftr_cfg>(m_fftCfg));
        m_fftCfg = nullptr;
    }
    if (m_decoder && m_decoder->isOpen())
        m_fftCfg = kiss_fftr_alloc(n, 0, nullptr, nullptr);
}

void AudioAnalyzer::setConfig(const AudioAnalyzerConfig &cfg) {
    AudioAnalyzerConfig c = cfg;
    // FFT size must be a power of two within bounds.
    c.fftSize = std::clamp(c.fftSize, 256, kMaxFftSize);
    int pow2 = 256;
    while (pow2 < c.fftSize) pow2 <<= 1;
    c.fftSize = pow2;
    c.binCount = std::clamp(c.binCount, 8, kMaxBins);
    c.smoothing = std::clamp(c.smoothing, 0.f, 0.98f);
    c.beatSensitivity = std::clamp(c.beatSensitivity, 0.1f, 5.f);
    c.lowMidHz = std::clamp(c.lowMidHz, 20.f, 20000.f);
    c.midHighHz = std::clamp(c.midHighHz, c.lowMidHz + 1.f, 20000.f);

    const bool sizeChanged = (c.fftSize != m_config.fftSize) || (c.binCount != m_config.binCount);
    m_config = c;
    if (sizeChanged)
        allocateBuffers();
}

AudioAnalyzer::~AudioAnalyzer() {
    if (m_fftCfg) {
        kiss_fftr_free(static_cast<kiss_fftr_cfg>(m_fftCfg));
        m_fftCfg = nullptr;
    }
    delete m_decoder;
}

bool AudioAnalyzer::open(const QString &filePath, double startTime) {
    close();
    m_live = false;

    if (!m_decoder->open(filePath))
        return false;

    if (startTime > 0.0)
        m_decoder->seek(startTime);

    m_fftCfg = kiss_fftr_alloc(m_config.fftSize, 0, nullptr, nullptr);
    if (!m_fftCfg) {
        m_decoder->close();
        return false;
    }

    std::fill(m_ringBuffer.begin(), m_ringBuffer.end(), 0.f);
    std::fill(m_spectrum.begin(), m_spectrum.end(), 0.f);
    std::fill(m_smoothedSpectrum.begin(), m_smoothedSpectrum.end(), 0.f);
    m_ringWrite = 0;
    m_ringFilled = 0;
    m_level = 0.f;
    m_lowBand = 0.f;
    m_midBand = 0.f;
    m_highBand = 0.f;
    m_beatPulse = 0.f;
    m_prevLowEnergy = 0.f;
    m_sinceHop = 0;
    m_beatArmed = true;
    m_playheadSeconds = std::max(0.0, startTime);
    m_playbackSpeed = 1.0;
    m_hasData = false;
    return true;
}

void AudioAnalyzer::close() {
    if (m_fftCfg) {
        kiss_fftr_free(static_cast<kiss_fftr_cfg>(m_fftCfg));
        m_fftCfg = nullptr;
    }
    if (m_decoder)
        m_decoder->close();

    m_ringWrite = 0;
    m_ringFilled = 0;
    m_level = 0.f;
    m_lowBand = 0.f;
    m_midBand = 0.f;
    m_highBand = 0.f;
    m_beatPulse = 0.f;
    m_prevLowEnergy = 0.f;
    m_sinceHop = 0;
    m_beatArmed = true;
    m_playheadSeconds = 0.0;
    m_playbackSpeed = 1.0;
    m_hasData = false;
    m_live = false;
}

bool AudioAnalyzer::beginLive() {
    close();
    m_live = true;
    m_fftCfg = kiss_fftr_alloc(m_config.fftSize, 0, nullptr, nullptr);
    if (!m_fftCfg) {
        m_live = false;
        return false;
    }
    std::fill(m_ringBuffer.begin(), m_ringBuffer.end(), 0.f);
    std::fill(m_spectrum.begin(), m_spectrum.end(), 0.f);
    std::fill(m_smoothedSpectrum.begin(), m_smoothedSpectrum.end(), 0.f);
    m_ringWrite = 0;
    m_ringFilled = 0;
    m_level = 0.f;
    m_lowBand = 0.f;
    m_midBand = 0.f;
    m_highBand = 0.f;
    m_beatPulse = 0.f;
    m_prevLowEnergy = 0.f;
    m_sinceHop = 0;
    m_beatArmed = true;
    m_playheadSeconds = 0.0;
    m_playbackSpeed = 1.0;
    m_hasData = false;
    return true;
}

void AudioAnalyzer::feedStereoPcm(const QByteArray &pcm) {
    if (!m_live || !m_fftCfg || pcm.isEmpty())
        return;

    const auto *data = reinterpret_cast<const float *>(pcm.constData());
    const int frameSize = static_cast<int>(sizeof(float)) * AudioDecoder::kOutputChannels;
    const int numFrames = pcm.size() / frameSize;
    if (numFrames <= 0)
        return;

    double sumSq = 0.0;
    int levelCount = 0;
    for (int i = 0; i < numFrames; ++i) {
        const float mono = (data[i * 2] + data[i * 2 + 1]) * 0.5f;
        appendMonoSample(mono);
        sumSq += static_cast<double>(mono) * static_cast<double>(mono);
        ++levelCount;
    }

    if (levelCount > 0) {
        const float rms = static_cast<float>(std::sqrt(sumSq / levelCount));
        m_level = std::clamp(rms * 4.f, 0.f, 1.f);
        m_playheadSeconds += static_cast<double>(numFrames) / static_cast<double>(kSampleRate);
    }

}

bool AudioAnalyzer::seek(double seconds) {
    if (m_live || !m_decoder || !m_decoder->isOpen())
        return false;
    const double clamped = std::max(0.0, seconds);
    if (!m_decoder->seek(clamped))
        return false;

    std::fill(m_ringBuffer.begin(), m_ringBuffer.end(), 0.f);
    std::fill(m_spectrum.begin(), m_spectrum.end(), 0.f);
    std::fill(m_smoothedSpectrum.begin(), m_smoothedSpectrum.end(), 0.f);
    m_ringWrite = 0;
    m_ringFilled = 0;
    m_level = 0.f;
    m_lowBand = 0.f;
    m_midBand = 0.f;
    m_highBand = 0.f;
    m_beatPulse = 0.f;
    m_prevLowEnergy = 0.f;
    m_sinceHop = 0;
    m_beatArmed = true;
    m_playheadSeconds = clamped;
    m_hasData = false;
    return true;
}

void AudioAnalyzer::setPlaybackSpeed(double speed) {
    if (!m_decoder)
        return;
    const double clamped = (speed > 0.01) ? speed : 1.0;
    m_playbackSpeed = clamped;
    m_decoder->setPlaybackSpeed(clamped);
}

// Analyse every hop of new samples rather than once per caller tick, so a short transient
// between two ticks still lands in a window and the outputs move at a steady rate.
void AudioAnalyzer::appendMonoSample(float sample) {
    const int n = m_config.fftSize;
    m_ringBuffer[m_ringWrite] = sample;
    m_ringWrite = (m_ringWrite + 1) % n;
    m_ringFilled = std::min(m_ringFilled + 1, n);
    if (++m_sinceHop >= hopSize() && m_ringFilled >= n) {
        m_sinceHop = 0;
        computeSpectrum();
    }
}

int AudioAnalyzer::hopSize() const {
    return std::min(m_config.fftSize / 2, 512);
}

void AudioAnalyzer::advance(double deltaSeconds) {
    if (m_live || !m_decoder->isOpen() || !m_fftCfg || deltaSeconds <= 0.0)
        return;

    deltaSeconds = std::min(deltaSeconds, 0.1);
    const int samplesNeeded = static_cast<int>(deltaSeconds * kSampleRate);

    int samplesDecoded = 0;
    double sumSq = 0.0;
    int levelCount = 0;

    while (samplesDecoded < samplesNeeded) {
        QByteArray chunk;
        if (!m_decoder->decodeNextChunk(chunk)) {
            if (m_decoder->atEnd()) {
                m_decoder->seek(0.0);
                continue;
            }
            break;
        }

        const auto *data = reinterpret_cast<const float *>(chunk.constData());
        const int numFrames = chunk.size() / (static_cast<int>(sizeof(float)) * AudioDecoder::kOutputChannels);
        for (int i = 0; i < numFrames && samplesDecoded < samplesNeeded; ++i) {
            const float mono = (data[i * 2] + data[i * 2 + 1]) * 0.5f;
            appendMonoSample(mono);
            sumSq += static_cast<double>(mono) * static_cast<double>(mono);
            ++levelCount;
            ++samplesDecoded;
        }
    }

    if (levelCount > 0) {
        const float rms = static_cast<float>(std::sqrt(sumSq / levelCount));
        m_level = std::clamp(rms * 4.f, 0.f, 1.f);
        const double decodedSeconds = static_cast<double>(samplesDecoded) / static_cast<double>(kSampleRate);
        m_playheadSeconds += decodedSeconds * m_playbackSpeed;
    }

}

void AudioAnalyzer::decay(double deltaSeconds) {
    if (deltaSeconds <= 0.0)
        return;
    const float k = std::exp(static_cast<float>(-deltaSeconds) * 6.f);
    auto fade = [k](float &v) { v = (v * k < 1e-4f) ? 0.f : v * k; };
    for (float &v : m_spectrum) fade(v);
    for (float &v : m_smoothedSpectrum) fade(v);
    fade(m_level);
    fade(m_lowBand);
    fade(m_midBand);
    fade(m_highBand);
    fade(m_prevLowEnergy);
    m_beatPulse = std::max(0.f, m_beatPulse - static_cast<float>(deltaSeconds) * 3.0f);
    float maxSpec = 0.f;
    for (float v : m_spectrum)
        maxSpec = std::max(maxSpec, v);
    if (m_level < 1e-4f && m_lowBand < 1e-4f && m_midBand < 1e-4f && m_highBand < 1e-4f
        && maxSpec < 1e-4f)
        m_hasData = false;
}

void AudioAnalyzer::computeSpectrum() {
    const int n = m_config.fftSize;
    const int bins = m_config.binCount;

    for (int i = 0; i < n; ++i) {
        const int idx = (m_ringWrite + i) % n;
        m_fftInput[i] = m_ringBuffer[idx] * m_hannWindow[i];
    }

    std::vector<kiss_fft_cpx> freqData(n / 2 + 1);
    kiss_fftr(static_cast<kiss_fftr_cfg>(m_fftCfg),
              m_fftInput.data(),
              freqData.data());

    const int numFftBins = n / 2;
    const float freqPerBin = static_cast<float>(kSampleRate) / static_cast<float>(n);

    // ── Log-spaced display spectrum ──────────────────────────────────────────
    std::vector<float> rawBins(bins, 0.f);
    for (int b = 0; b < bins; ++b) {
        const float t0 = static_cast<float>(b) / bins;
        const float t1 = static_cast<float>(b + 1) / bins;
        const int binLow  = std::max(1, static_cast<int>(std::pow(numFftBins, t0)));
        const int binHigh = std::min(numFftBins, static_cast<int>(std::pow(numFftBins, t1)));
        float sum = 0.f;
        int count = 0;
        for (int i = binLow; i <= binHigh; ++i) {
            const float re = freqData[i].r;
            const float im = freqData[i].i;
            sum += std::sqrt(re * re + im * im);
            ++count;
        }
        rawBins[b] = count > 0 ? sum / static_cast<float>(count) : 0.f;
    }

    float maxVal = 0.f;
    for (float v : rawBins)
        maxVal = std::max(maxVal, v);
    const float norm = maxVal > 1e-6f ? maxVal : 1.f;

    // config.smoothing is the weight kept from the past per 1/30 s; rescaled to the hop length so
    // the feel does not depend on FFT size.
    const float hopSeconds = static_cast<float>(hopSize()) / static_cast<float>(kSampleRate);
    const float keep = std::pow(m_config.smoothing, hopSeconds * 30.f);
    const float take = 1.f - keep;
    for (int b = 0; b < bins; ++b) {
        const float normalized = std::clamp(rawBins[b] / norm, 0.f, 1.f);
        m_spectrum[b] = m_smoothedSpectrum[b] * keep + normalized * take;
        m_smoothedSpectrum[b] = m_spectrum[b];
    }

    // ── Perceptual bands from Hz crossovers on the raw linear FFT bins ────────
    float lowSum = 0.f, midSum = 0.f, highSum = 0.f;
    int lowCount = 0, midCount = 0, highCount = 0;
    for (int i = 1; i <= numFftBins; ++i) {
        const float re = freqData[i].r;
        const float im = freqData[i].i;
        const float mag = std::sqrt(re * re + im * im) / norm;
        const float hz = static_cast<float>(i) * freqPerBin;
        if (hz < m_config.lowMidHz)       { lowSum += mag;  ++lowCount; }
        else if (hz < m_config.midHighHz) { midSum += mag;  ++midCount; }
        else                              { highSum += mag; ++highCount; }
    }
    const float lowRaw  = (lowCount  > 0) ? std::clamp(lowSum  / lowCount,  0.f, 1.f) : 0.f;
    const float midRaw  = (midCount  > 0) ? std::clamp(midSum  / midCount,  0.f, 1.f) : 0.f;
    const float highRaw = (highCount > 0) ? std::clamp(highSum / highCount, 0.f, 1.f) : 0.f;

    m_lowBand  = m_lowBand  * keep + lowRaw  * take;
    m_midBand  = m_midBand  * keep + midRaw  * take;
    m_highBand = m_highBand * keep + highRaw * take;

    // Beat pulse: the raw low band rising above its recent average (~0.25 s); beatSensitivity
    // scales the threshold. Re-arms once the onset falls back, so one hit fires once. The pulse
    // jumps up on a hit and falls by 3 per second.
    m_beatPulse = std::max(0.0f, m_beatPulse - 3.0f * hopSeconds);
    const float onset = lowRaw - m_prevLowEnergy;
    const float dynamicThreshold = (0.06f + m_level * 0.08f) / m_config.beatSensitivity;
    if (onset > dynamicThreshold && m_beatArmed) {
        m_beatPulse = std::max(m_beatPulse, std::clamp(onset * 6.0f, 0.0f, 1.0f));
        m_beatArmed = false;
    } else if (onset < dynamicThreshold * 0.5f) {
        m_beatArmed = true;
    }
    m_prevLowEnergy += (lowRaw - m_prevLowEnergy) * (1.f - std::exp(-hopSeconds / 0.25f));

    m_hasData = true;
}
