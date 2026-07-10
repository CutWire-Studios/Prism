#pragma once

#include <QByteArray>
#include <QAudioFormat>

struct SwrContext;

/// Wraps libswresample to convert interleaved PCM between arbitrary
/// rate/channel/sample-format combinations, streaming chunk by chunk. Used at
/// the boundary between Prism's fixed-format internal audio pipeline (44.1kHz
/// stereo float) and audio devices that don't natively support that format.
class PcmResampler {
public:
    PcmResampler();
    ~PcmResampler();

    bool configure(int inRate, int inChannels, QAudioFormat::SampleFormat inSampleFormat,
                    int outRate, int outChannels, QAudioFormat::SampleFormat outSampleFormat);

    /// Converts one chunk of interleaved input PCM. May return an empty array
    /// (e.g. while downsampling, several input chunks can accumulate before
    /// enough samples exist to produce one output chunk) — internal state is
    /// carried across calls, so this is safe to call repeatedly on a stream.
    QByteArray convert(const QByteArray &input);

    void reset();

private:
    SwrContext *m_swr = nullptr;
    int m_inChannels = 0;
    int m_inBytesPerSample = 0;
    int m_outChannels = 0;
    int m_outBytesPerSample = 0;
};
