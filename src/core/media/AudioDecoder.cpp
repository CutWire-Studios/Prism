#include "core/media/AudioDecoder.h"
#include <QDebug>
#include <QStringList>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}

AudioDecoder::AudioDecoder() = default;

AudioDecoder::~AudioDecoder() {
    close();
}

bool AudioDecoder::open(const QString &filePath) {
    close();

    const QByteArray utf8Path = filePath.toUtf8();
    if (avformat_open_input(&m_formatCtx, utf8Path.constData(), nullptr, nullptr) < 0) {
        qWarning() << "AudioDecoder: unable to open file:" << filePath;
        return false;
    }

    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        qWarning() << "AudioDecoder: unable to read stream info";
        close();
        return false;
    }

    m_audioStreamIndex = av_find_best_stream(m_formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_audioStreamIndex < 0) {
        qWarning() << "AudioDecoder: no audio stream found";
        close();
        return false;
    }

    m_audioStream = m_formatCtx->streams[m_audioStreamIndex];
    const AVCodec *decoder = avcodec_find_decoder(m_audioStream->codecpar->codec_id);
    if (!decoder) {
        qWarning() << "AudioDecoder: unsupported audio codec";
        close();
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(decoder);
    if (!m_codecCtx || avcodec_parameters_to_context(m_codecCtx, m_audioStream->codecpar) < 0) {
        qWarning() << "AudioDecoder: unable to initialize codec context";
        close();
        return false;
    }

    if (avcodec_open2(m_codecCtx, decoder, nullptr) < 0) {
        qWarning() << "AudioDecoder: unable to open audio codec";
        close();
        return false;
    }

    if (!initResampler()) {
        close();
        return false;
    }

    m_packet = av_packet_alloc();
    m_frame = av_frame_alloc();
    m_filtFrame = av_frame_alloc();
    if (!m_packet || !m_frame || !m_filtFrame) {
        qWarning() << "AudioDecoder: unable to allocate decode buffers";
        close();
        return false;
    }

    if (!initFilterGraph()) {
        close();
        return false;
    }

    m_sentFlushPacket = false;
    m_eof = false;
    return true;
}

void AudioDecoder::close() {
    freeFilterGraph();
    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }
    if (m_frame) {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }
    if (m_filtFrame) {
        av_frame_free(&m_filtFrame);
        m_filtFrame = nullptr;
    }
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }
    if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
    }

    m_audioStream = nullptr;
    m_audioStreamIndex = -1;
    m_sentFlushPacket = false;
    m_eof = false;
    m_seekTrimTarget = -1.0;
}

bool AudioDecoder::initResampler() {
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, kOutputChannels);
    const bool ok = swr_alloc_set_opts2(&m_swrCtx,
                                        &outLayout,
                                        AV_SAMPLE_FMT_FLT,
                                        kOutputSampleRate,
                                        &m_codecCtx->ch_layout,
                                        m_codecCtx->sample_fmt,
                                        m_codecCtx->sample_rate,
                                        0,
                                        nullptr) >= 0
                    && m_swrCtx
                    && swr_init(m_swrCtx) >= 0;
    av_channel_layout_uninit(&outLayout);
    if (!ok)
        qWarning() << "AudioDecoder: unable to initialize resampler";
    return ok;
}

void AudioDecoder::freeFilterGraph() {
    if (m_filterGraph)
        avfilter_graph_free(&m_filterGraph);
    m_filterGraph = nullptr;
    m_filterSrc = nullptr;
    m_filterSink = nullptr;
    m_filterFlushed = false;
}

bool AudioDecoder::initFilterGraph() {
    freeFilterGraph();
    if (qFuzzyCompare(m_speed, 1.0))
        return true;   // no graph needed, plain swr path

    m_filterGraph = avfilter_graph_alloc();
    if (!m_filterGraph) return false;

    char layoutDesc[64];
    av_channel_layout_describe(&m_codecCtx->ch_layout, layoutDesc, sizeof(layoutDesc));
    const QByteArray srcArgs =
        QStringLiteral("time_base=1/%1:sample_rate=%1:sample_fmt=%2:channel_layout=%3")
            .arg(m_codecCtx->sample_rate)
            .arg(QLatin1String(av_get_sample_fmt_name(m_codecCtx->sample_fmt)),
                 QLatin1String(layoutDesc))
            .toUtf8();

    if (avfilter_graph_create_filter(&m_filterSrc, avfilter_get_by_name("abuffer"),
                                     "in", srcArgs.constData(), nullptr, m_filterGraph) < 0 ||
        avfilter_graph_create_filter(&m_filterSink, avfilter_get_by_name("abuffersink"),
                                     "out", nullptr, nullptr, m_filterGraph) < 0) {
        qWarning() << "AudioDecoder: unable to create atempo filter endpoints";
        freeFilterGraph();
        return false;
    }

    // atempo accepts [0.5, 100] per instance; chain instances for slower rates.
    QStringList stages;
    double t = m_speed;
    while (t < 0.5) {
        stages << QStringLiteral("atempo=0.5");
        t *= 2.0;
    }
    stages << QStringLiteral("atempo=%1").arg(t, 0, 'f', 4);
    const QByteArray chain =
        (stages.join(QLatin1Char(',')) +
         QStringLiteral(",aresample=%1,aformat=sample_fmts=flt:channel_layouts=stereo")
             .arg(kOutputSampleRate))
            .toUtf8();

    AVFilterInOut *inputs = avfilter_inout_alloc();
    AVFilterInOut *outputs = avfilter_inout_alloc();
    outputs->name = av_strdup("in");
    outputs->filter_ctx = m_filterSrc;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = m_filterSink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    const int ret = avfilter_graph_parse_ptr(m_filterGraph, chain.constData(),
                                             &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (ret < 0 || avfilter_graph_config(m_filterGraph, nullptr) < 0) {
        qWarning() << "AudioDecoder: unable to configure atempo filter graph";
        freeFilterGraph();
        return false;
    }
    return true;
}

void AudioDecoder::setPlaybackSpeed(double speed) {
    if (speed <= 0.0) speed = 1.0;
    if (qFuzzyCompare(m_speed, speed)) return;
    m_speed = speed;
    if (isOpen())
        initFilterGraph();
}

bool AudioDecoder::seek(double seconds) {
    if (!isOpen()) return false;

    const int64_t targetTs = static_cast<int64_t>(seconds * AV_TIME_BASE);
    if (av_seek_frame(m_formatCtx, -1, targetTs, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    m_seekTrimTarget = seconds;

    avcodec_flush_buffers(m_codecCtx);
    swr_close(m_swrCtx);
    swr_init(m_swrCtx);
    initFilterGraph();   // drop audio buffered inside the atempo chain
    m_sentFlushPacket = false;
    m_eof = false;
    return true;
}

bool AudioDecoder::decodeNextChunk(QByteArray &outChunk) {
    outChunk.clear();
    if (!isOpen() || m_eof) return false;

    while (true) {
        // Time-stretch path: drain the atempo graph before decoding more.
        if (m_filterGraph) {
            const int sinkRet = av_buffersink_get_frame(m_filterSink, m_filtFrame);
            if (sinkRet == 0) {
                const int bytes = m_filtFrame->nb_samples * kOutputChannels
                                  * static_cast<int>(sizeof(float));
                outChunk = QByteArray(reinterpret_cast<const char *>(m_filtFrame->data[0]), bytes);
                av_frame_unref(m_filtFrame);
                if (outChunk.isEmpty()) continue;
                return true;
            }
            if (sinkRet != AVERROR(EAGAIN)) {
                m_eof = true;
                return false;
            }
        }

        const int receiveRet = avcodec_receive_frame(m_codecCtx, m_frame);
        if (receiveRet == 0) {
            if (m_seekTrimTarget >= 0.0) {
                if (m_frame->pts != AV_NOPTS_VALUE && m_frame->sample_rate > 0) {
                    const double frameStart =
                        m_frame->pts * av_q2d(m_audioStream->time_base);
                    const int skip = static_cast<int>(
                        llround((m_seekTrimTarget - frameStart) * m_frame->sample_rate));
                    if (skip >= m_frame->nb_samples)
                        continue;   // frame is entirely before the target
                    if (skip > 0) {
                        const auto fmt = static_cast<AVSampleFormat>(m_frame->format);
                        const int bps = av_get_bytes_per_sample(fmt);
                        if (av_sample_fmt_is_planar(fmt)) {
                            for (int ch = 0; ch < m_frame->ch_layout.nb_channels; ++ch)
                                m_frame->extended_data[ch] += static_cast<size_t>(skip) * bps;
                        } else {
                            m_frame->extended_data[0] += static_cast<size_t>(skip) * bps
                                                         * m_frame->ch_layout.nb_channels;
                        }
                        m_frame->nb_samples -= skip;
                    }
                }
                m_seekTrimTarget = -1.0;
            }

            if (m_filterGraph) {
                if (av_buffersrc_add_frame(m_filterSrc, m_frame) < 0) {
                    qWarning() << "AudioDecoder: atempo filter rejected frame";
                    m_eof = true;
                    return false;
                }
                continue;
            }

            const int dstSamples = av_rescale_rnd(
                swr_get_delay(m_swrCtx, m_codecCtx->sample_rate) + m_frame->nb_samples,
                kOutputSampleRate,
                m_codecCtx->sample_rate,
                AV_ROUND_UP);

            outChunk.resize(dstSamples * kOutputChannels * static_cast<int>(sizeof(float)));
            auto *dst = reinterpret_cast<uint8_t *>(outChunk.data());
            uint8_t *dstData[1] = { dst };

            const int converted = swr_convert(m_swrCtx,
                                              dstData,
                                              dstSamples,
                                              const_cast<const uint8_t **>(m_frame->extended_data),
                                              m_frame->nb_samples);
            if (converted <= 0) continue;

            outChunk.resize(converted * kOutputChannels * static_cast<int>(sizeof(float)));
            return true;
        }

        if (receiveRet == AVERROR_EOF) {
            if (m_filterGraph && !m_filterFlushed) {
                (void)av_buffersrc_add_frame(m_filterSrc, nullptr);
                m_filterFlushed = true;
                continue;   // drain what the filter still holds
            }
            m_eof = true;
            return false;
        }

        if (receiveRet != AVERROR(EAGAIN)) {
            qWarning() << "AudioDecoder: decode error:" << receiveRet;
            m_eof = true;
            return false;
        }

        bool sentPacket = false;
        while (!sentPacket) {
            const int readRet = av_read_frame(m_formatCtx, m_packet);
            if (readRet < 0) {
                if (!m_sentFlushPacket) {
                    avcodec_send_packet(m_codecCtx, nullptr);
                    m_sentFlushPacket = true;
                    sentPacket = true;
                } else {
                    m_eof = true;
                    return false;
                }
                break;
            }

            if (m_packet->stream_index == m_audioStreamIndex) {
                if (avcodec_send_packet(m_codecCtx, m_packet) == 0)
                    sentPacket = true;
            }
            av_packet_unref(m_packet);
        }
    }
}
