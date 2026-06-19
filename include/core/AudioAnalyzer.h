#pragma once

#include <QString>
#include <vector>

class AudioDecoder;

class AudioAnalyzer {
public:
    static constexpr int kFftSize = 1024;
    static constexpr int kBins    = 64;

    AudioAnalyzer();
    ~AudioAnalyzer();
    AudioAnalyzer(const AudioAnalyzer &) = delete;
    AudioAnalyzer &operator=(const AudioAnalyzer &) = delete;

    bool open(const QString &filePath, double startTime = 0.0);
    void close();
    void advance(double deltaSeconds);

    const std::vector<float> &spectrum() const { return m_spectrum; }
    float level() const { return m_level; }
    bool hasData() const { return m_hasData; }

private:
    void appendMonoSample(float sample);
    void computeSpectrum();

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
    bool  m_hasData = false;
};
