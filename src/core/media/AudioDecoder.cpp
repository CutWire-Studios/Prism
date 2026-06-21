#include "core/media/AudioDecoder.h"
#include <QDebug>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
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

    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, kOutputChannels);
    if (swr_alloc_set_opts2(&m_swrCtx,
                            &outLayout,
                            AV_SAMPLE_FMT_FLT,
                            kOutputSampleRate,
                            &m_codecCtx->ch_layout,
                            m_codecCtx->sample_fmt,
                            m_codecCtx->sample_rate,
                            0,
                            nullptr) < 0 ||
        !m_swrCtx ||
        swr_init(m_swrCtx) < 0) {
        qWarning() << "AudioDecoder: unable to initialize resampler";
        av_channel_layout_uninit(&outLayout);
        close();
        return false;
    }
    av_channel_layout_uninit(&outLayout);

    m_packet = av_packet_alloc();
    m_frame = av_frame_alloc();
    if (!m_packet || !m_frame) {
        qWarning() << "AudioDecoder: unable to allocate decode buffers";
        close();
        return false;
    }

    m_sentFlushPacket = false;
    m_eof = false;
    return true;
}

void AudioDecoder::close() {
    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }
    if (m_frame) {
        av_frame_free(&m_frame);
        m_frame = nullptr;
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
}

bool AudioDecoder::seek(double seconds) {
    if (!isOpen()) return false;

    const int64_t targetTs = static_cast<int64_t>(seconds * AV_TIME_BASE);
    if (av_seek_frame(m_formatCtx, -1, targetTs, AVSEEK_FLAG_BACKWARD) < 0)
        return false;

    avcodec_flush_buffers(m_codecCtx);
    swr_close(m_swrCtx);
    swr_init(m_swrCtx);
    m_sentFlushPacket = false;
    m_eof = false;
    return true;
}

bool AudioDecoder::decodeNextChunk(QByteArray &outChunk) {
    outChunk.clear();
    if (!isOpen() || m_eof) return false;

    while (true) {
        const int receiveRet = avcodec_receive_frame(m_codecCtx, m_frame);
        if (receiveRet == 0) {
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
