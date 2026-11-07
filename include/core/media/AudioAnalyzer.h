#pragma once

#include <QString>
#include <vector>

class AudioDecoder;

/// Streams a file's audio in lock-step with playback and produces a smoothed
/// FFT magnitude spectrum + overall level, consumed by audio-reactive shaders.
/// Tunable FFT / band parameters for an AudioAnalyzer. Defaults reproduce the
/// original fixed behaviour, so untouched nodes analyse exactly as before.
struct AudioAnalyzerConfig {
    int   fftSize        = 1024;   // power-of-two window size
    int   binCount       = 64;     // log-spaced display spectrum bins
    float smoothing      = 0.5f;   // 0 = raw, →1 = heavily smoothed
    float beatSensitivity = 1.0f;  // scales beat detection (higher = more beats)
    float lowMidHz       = 250.0f; // low/mid crossover
    float midHighHz      = 4000.0f;// mid/high crossover
};

class AudioAnalyzer {
public:
    static constexpr int kFftSize    = 1024;  // default window
    static constexpr int kBins       = 64;    // default bin count
    static constexpr int kMaxFftSize = 8192;
    static constexpr int kMaxBins    = 256;   // texture/upload capacity

    AudioAnalyzer();
    ~AudioAnalyzer();
    AudioAnalyzer(const AudioAnalyzer &) = delete;
    AudioAnalyzer &operator=(const AudioAnalyzer &) = delete;

    /// Apply tunable parameters. Reallocates FFT buffers; safe to call while open.
    void setConfig(const AudioAnalyzerConfig &cfg);
    const AudioAnalyzerConfig &config() const { return m_config; }
    int binCount() const { return m_config.binCount; }

    bool open(const QString &filePath, double startTime = 0.0);
    void close();
    void advance(double deltaSeconds);
    /// Fade all outputs toward zero without consuming audio (playback paused).
    void decay(double deltaSeconds);
    bool seek(double seconds);
    void setPlaybackSpeed(double speed);
    double currentTime() const { return m_playheadSeconds; }

    const std::vector<float> &spectrum() const { return m_spectrum; }
    float level() const { return m_level; }
    float lowBand() const { return m_lowBand; }
    float midBand() const { return m_midBand; }
    float highBand() const { return m_highBand; }
    float beatPulse() const { return m_beatPulse; }
    bool hasData() const { return m_hasData; }

    /// Real-time PCM from live capture/mic nodes (float32 stereo interleaved).
    bool beginLive();
    void feedStereoPcm(const QByteArray &pcm);
    bool isLive() const { return m_live; }

private:
    void appendMonoSample(float sample);
    void computeSpectrum();
    int hopSize() const;
    void allocateBuffers();   // (re)size buffers + FFT cfg from m_config

    AudioAnalyzerConfig m_config;

    AudioDecoder *m_decoder = nullptr;
    void         *m_fftCfg  = nullptr;  // kiss_fftr_cfg

    std::vector<float> m_ringBuffer;
    std::vector<float> m_hannWindow;
    std::vector<float> m_fftInput;
    std::vector<float> m_spectrum;
    std::vector<float> m_smoothedSpectrum;

    int   m_ringWrite = 0;
    int   m_ringFilled = 0;
    float m_level = 0.f;
    float m_lowBand = 0.f;
    float m_midBand = 0.f;
    float m_highBand = 0.f;
    float m_beatPulse = 0.f;
    float m_prevLowEnergy = 0.f;   // recent average of the raw low band, for beat onsets
    int   m_sinceHop = 0;
    bool  m_beatArmed = true;
    double m_playheadSeconds = 0.0;
    double m_playbackSpeed = 1.0;
    bool  m_hasData = false;
    bool  m_live = false;
};
