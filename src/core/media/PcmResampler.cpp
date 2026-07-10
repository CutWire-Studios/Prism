#include "core/media/PcmResampler.h"

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

namespace {

AVSampleFormat toAvSampleFormat(QAudioFormat::SampleFormat fmt) {
    switch (fmt) {
    case QAudioFormat::UInt8: return AV_SAMPLE_FMT_U8;
    case QAudioFormat::Int16: return AV_SAMPLE_FMT_S16;
    case QAudioFormat::Int32: return AV_SAMPLE_FMT_S32;
    case QAudioFormat::Float: return AV_SAMPLE_FMT_FLT;
    default: return AV_SAMPLE_FMT_FLT;
    }
}

} // namespace

PcmResampler::PcmResampler() = default;

PcmResampler::~PcmResampler() {
    reset();
}

void PcmResampler::reset() {
    if (m_swr)
        swr_free(&m_swr);
    m_swr = nullptr;
    m_inChannels = 0;
    m_inBytesPerSample = 0;
    m_outChannels = 0;
    m_outBytesPerSample = 0;
}

bool PcmResampler::configure(int inRate, int inChannels, QAudioFormat::SampleFormat inSampleFormat,
                              int outRate, int outChannels, QAudioFormat::SampleFormat outSampleFormat) {
    reset();
    if (inRate <= 0 || inChannels <= 0 || outRate <= 0 || outChannels <= 0)
        return false;

    const AVSampleFormat inFmt = toAvSampleFormat(inSampleFormat);
    const AVSampleFormat outFmt = toAvSampleFormat(outSampleFormat);

    AVChannelLayout inLayout;
    AVChannelLayout outLayout;
    av_channel_layout_default(&inLayout, inChannels);
    av_channel_layout_default(&outLayout, outChannels);

    const bool ok = swr_alloc_set_opts2(&m_swr,
                                        &outLayout, outFmt, outRate,
                                        &inLayout, inFmt, inRate,
                                        0, nullptr) >= 0
                    && m_swr
                    && swr_init(m_swr) >= 0;
    av_channel_layout_uninit(&inLayout);
    av_channel_layout_uninit(&outLayout);

    if (!ok) {
        reset();
        return false;
    }

    m_inChannels = inChannels;
    m_inBytesPerSample = av_get_bytes_per_sample(inFmt);
    m_outChannels = outChannels;
    m_outBytesPerSample = av_get_bytes_per_sample(outFmt);
    return true;
}

QByteArray PcmResampler::convert(const QByteArray &input) {
    if (!m_swr || m_inChannels <= 0 || m_inBytesPerSample <= 0)
        return {};

    const int inFrameBytes = m_inChannels * m_inBytesPerSample;
    const int inSampleCount = inFrameBytes > 0 ? input.size() / inFrameBytes : 0;

    const auto *inData = reinterpret_cast<const uint8_t *>(input.constData());
    const uint8_t *inPlanes[1] = { inData };

    const int64_t maxOutSamples = swr_get_out_samples(m_swr, inSampleCount);
    if (maxOutSamples <= 0) {
        if (inSampleCount > 0)
            swr_convert(m_swr, nullptr, 0, inPlanes, inSampleCount);
        return {};
    }

    QByteArray out;
    out.resize(static_cast<qsizetype>(maxOutSamples) * m_outChannels * m_outBytesPerSample);
    uint8_t *outPlanes[1] = { reinterpret_cast<uint8_t *>(out.data()) };

    const int converted = swr_convert(m_swr, outPlanes, static_cast<int>(maxOutSamples),
                                       inSampleCount > 0 ? inPlanes : nullptr, inSampleCount);
    if (converted <= 0)
        return {};

    out.resize(static_cast<qsizetype>(converted) * m_outChannels * m_outBytesPerSample);
    return out;
}
