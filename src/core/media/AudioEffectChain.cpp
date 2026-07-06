#include "core/media/AudioEffectChain.h"
#include "ui/nodes/AudioEffects.h"

#include <QDebug>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}

AudioEffectChain::AudioEffectChain() {
    m_inFrame = av_frame_alloc();
    m_outFrame = av_frame_alloc();
}

AudioEffectChain::~AudioEffectChain() {
    freeGraph();
    if (m_inFrame)
        av_frame_free(&m_inFrame);
    if (m_outFrame)
        av_frame_free(&m_outFrame);
}

void AudioEffectChain::setEffects(const QVector<AudioEffectRef> &effects) {
    const QString newChain = AudioEffects::buildFilterChain(effects);
    if (m_effects == effects && m_filterChain == newChain)
        return;
    m_effects = effects;
    m_filterChain = newChain;
    reset();
}

void AudioEffectChain::reset() {
    m_pts = 0;
    m_inputBuffer.clear();
    if (m_filterChain.isEmpty())
        freeGraph();
    else
        rebuildGraph();
}

void AudioEffectChain::freeGraph() {
    if (m_graph)
        avfilter_graph_free(&m_graph);
    m_graph = nullptr;
    m_src = nullptr;
    m_sink = nullptr;
}

bool AudioEffectChain::rebuildGraph() {
    freeGraph();
    if (m_filterChain.isEmpty())
        return true;

    m_graph = avfilter_graph_alloc();
    if (!m_graph)
        return false;

    const QByteArray srcArgs =
        QStringLiteral("time_base=1/%1:sample_rate=%1:sample_fmt=flt:channel_layout=stereo")
            .arg(kSampleRate)
            .toUtf8();

    if (avfilter_graph_create_filter(&m_src, avfilter_get_by_name("abuffer"),
                                     "ae_in", srcArgs.constData(), nullptr, m_graph) < 0 ||
        avfilter_graph_create_filter(&m_sink, avfilter_get_by_name("abuffersink"),
                                     "ae_out", nullptr, nullptr, m_graph) < 0) {
        qWarning() << "AudioEffectChain: unable to create filter endpoints";
        freeGraph();
        return false;
    }

    const QByteArray chain =
        (m_filterChain + QStringLiteral(",aformat=sample_fmts=flt:channel_layouts=stereo"))
            .toUtf8();

    AVFilterInOut *inputs = avfilter_inout_alloc();
    AVFilterInOut *outputs = avfilter_inout_alloc();
    outputs->name = av_strdup("in");
    outputs->filter_ctx = m_src;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = m_sink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    const int ret = avfilter_graph_parse_ptr(m_graph, chain.constData(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (ret < 0 || avfilter_graph_config(m_graph, nullptr) < 0) {
        qWarning() << "AudioEffectChain: unable to configure filter graph:" << m_filterChain;
        freeGraph();
        return false;
    }
    return true;
}

bool AudioEffectChain::drainSink(QByteArray &out) {
    if (!m_sink || !m_outFrame)
        return false;

    bool gotAny = false;
    while (true) {
        const int ret = av_buffersink_get_frame(m_sink, m_outFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
            return false;

        const int bytes = m_outFrame->nb_samples * kChannels * static_cast<int>(sizeof(float));
        out.append(reinterpret_cast<const char *>(m_outFrame->data[0]), bytes);
        av_frame_unref(m_outFrame);
        gotAny = true;
    }
    return gotAny;
}

bool AudioEffectChain::pushSamples(const float *samples, int nbSamples, QByteArray &out) {
    if (!m_src || !m_inFrame || nbSamples <= 0)
        return false;

    av_frame_unref(m_inFrame);
    m_inFrame->format = AV_SAMPLE_FMT_FLT;
    av_channel_layout_uninit(&m_inFrame->ch_layout);
    av_channel_layout_from_string(&m_inFrame->ch_layout, "stereo");
    m_inFrame->sample_rate = kSampleRate;
    m_inFrame->nb_samples = nbSamples;
    m_inFrame->time_base = AVRational{1, kSampleRate};
    m_inFrame->pts = m_pts;
    m_pts += nbSamples;

    if (av_frame_get_buffer(m_inFrame, 0) < 0)
        return false;

    const int bytes = nbSamples * kChannels * static_cast<int>(sizeof(float));
    memcpy(m_inFrame->data[0], samples, static_cast<size_t>(bytes));

    for (;;) {
        const int ret = av_buffersrc_add_frame_flags(m_src, m_inFrame, 0);
        if (ret >= 0) {
            av_frame_unref(m_inFrame);
            return true;
        }
        if (ret == AVERROR(EAGAIN)) {
            if (!drainSink(out))
                return false;
            continue;
        }
        char err[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, err, sizeof(err));
        qWarning() << "AudioEffectChain: abuffer rejected frame:" << err
                   << "samples=" << nbSamples << "pts=" << (m_pts - nbSamples);
        return false;
    }
}

bool AudioEffectChain::process(const QByteArray &in, QByteArray &out) {
    out.clear();
    if (in.isEmpty())
        return false;

    if (!m_graph || m_filterChain.isEmpty()) {
        out = in;
        return true;
    }

    const int frameBytes = kChannels * static_cast<int>(sizeof(float));
    if (in.size() % frameBytes != 0)
        return false;

    m_inputBuffer.append(in);

    // Drain anything already in the pipeline before feeding more input.
    drainSink(out);

    while (m_inputBuffer.size() >= kFrameSamples * frameBytes) {
        const auto *samples = reinterpret_cast<const float *>(m_inputBuffer.constData());
        if (!pushSamples(samples, kFrameSamples, out))
            break;
        m_inputBuffer.remove(0, kFrameSamples * frameBytes);
        drainSink(out);
    }

    return true;
}
