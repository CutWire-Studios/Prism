#include "ui/recording/ProgramAudioRecorder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int kBytesPerSampleFrame = sizeof(float) * ProgramAudioRecorder::kChannels;

int takeBlockBytes(QByteArray &queue, int blockBytes, float *outSamples) {
    const int available = queue.size();
    const int takeBytes = std::min(available, blockBytes);
    if (takeBytes > 0)
        std::memcpy(outSamples, queue.constData(), static_cast<size_t>(takeBytes));
    if (takeBytes < blockBytes)
        std::memset(reinterpret_cast<char *>(outSamples) + takeBytes, 0,
                    static_cast<size_t>(blockBytes - takeBytes));
    if (takeBytes > 0)
        queue.remove(0, takeBytes);
    return takeBytes;
}

}

ProgramAudioRecorder::ProgramAudioRecorder(QObject *parent)
    : QObject(parent)
{
}

ProgramAudioRecorder::~ProgramAudioRecorder() {
    stopRecording();
}

QString ProgramAudioRecorder::makeOutputPath(const QString &dir, const QString &stem, const QString &suffix) {
    const QString base = suffix.isEmpty() ? stem : stem + QLatin1Char('_') + suffix;
    return QDir(dir).filePath(base + QStringLiteral(".flac"));
}

qint64 ProgramAudioRecorder::recordingDurationMs() const {
    if (m_lastDurationMs > 0)
        return m_lastDurationMs;
    return m_recording ? m_timer.elapsed() : 0;
}

bool ProgramAudioRecorder::startRecording(const QString &outputPath, const QString &trackLabel,
                                          bool writeMarkersOnStop) {
    if (m_recording)
        stopRecording();

    m_outputPath  = outputPath;
    m_trackLabel  = trackLabel;
    m_writeMarkersOnStop = writeMarkersOnStop;
    m_markersPath = m_outputPath;
    m_markersPath.replace(QRegularExpression(QStringLiteral("\\.[^.]+$")),
                          QStringLiteral(".audio.markers.json"));
    m_markers.clear();
    m_sampleIndex = 0;
    m_lastDurationMs = 0;
    m_deckQueueA.clear();
    m_deckQueueB.clear();

    const QByteArray pathUtf8 = m_outputPath.toUtf8();

    if (avformat_alloc_output_context2(&m_fmtCtx, nullptr, "flac", pathUtf8.constData()) < 0) {
        emit errorOccurred(tr("Could not create audio output file format."));
        cleanup();
        return false;
    }

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_FLAC);
    if (!codec) {
        emit errorOccurred(tr("FLAC encoder not found."));
        cleanup();
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    m_codecCtx->sample_rate = kSampleRate;
    av_channel_layout_default(&m_codecCtx->ch_layout, kChannels);
    m_codecCtx->sample_fmt  = AV_SAMPLE_FMT_S32;
    m_codecCtx->time_base   = AVRational{1, kSampleRate};

    if (m_fmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
        m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        emit errorOccurred(tr("Could not open FLAC encoder."));
        cleanup();
        return false;
    }

    m_stream = avformat_new_stream(m_fmtCtx, nullptr);
    if (!m_stream) {
        emit errorOccurred(tr("Could not create audio stream."));
        cleanup();
        return false;
    }
    m_stream->time_base = m_codecCtx->time_base;
    avcodec_parameters_from_context(m_stream->codecpar, m_codecCtx);

    if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&m_fmtCtx->pb, pathUtf8.constData(), AVIO_FLAG_WRITE) < 0) {
            emit errorOccurred(tr("Could not open output file:\n%1").arg(m_outputPath));
            cleanup();
            return false;
        }
    }

    if (avformat_write_header(m_fmtCtx, nullptr) < 0) {
        emit errorOccurred(tr("Could not write audio header."));
        cleanup();
        return false;
    }

    AVChannelLayout inLayout{};
    av_channel_layout_default(&inLayout, kChannels);
    if (swr_alloc_set_opts2(&m_swrCtx,
                            &m_codecCtx->ch_layout, m_codecCtx->sample_fmt, kSampleRate,
                            &inLayout, AV_SAMPLE_FMT_FLT, kSampleRate,
                            0, nullptr) < 0
        || swr_init(m_swrCtx) < 0) {
        av_channel_layout_uninit(&inLayout);
        emit errorOccurred(tr("Could not create audio resampler."));
        cleanup();
        return false;
    }
    av_channel_layout_uninit(&inLayout);

    m_pcmFrame = av_frame_alloc();
    m_pcmFrame->format      = m_codecCtx->sample_fmt;
    m_pcmFrame->sample_rate = kSampleRate;
    av_channel_layout_copy(&m_pcmFrame->ch_layout, &m_codecCtx->ch_layout);
    m_pcmFrame->nb_samples = kMixBlockSamples;
    if (av_frame_get_buffer(m_pcmFrame, 0) < 0) {
        emit errorOccurred(tr("Could not allocate audio frame."));
        cleanup();
        return false;
    }

    m_packet = av_packet_alloc();
    m_timer.start();
    m_recording = true;
    emit recordingChanged(true);
    return true;
}

void ProgramAudioRecorder::stopRecording() {
    if (!m_recording) return;

    drainMixQueues();
    drainPcmQueue(true);
    flushEncoder();

    const int64_t samplesWritten = m_sampleIndex;
    const QString audioPath = m_outputPath;
    const QString markersPath = m_markersPath;

    if (m_fmtCtx && samplesWritten > 0)
        av_write_trailer(m_fmtCtx);
    if (m_writeMarkersOnStop && samplesWritten > 0)
        writeMarkersFile();

    m_lastDurationMs = m_timer.elapsed();
    cleanup();

    m_recording = false;
    emit recordingChanged(false);

    if (samplesWritten == 0) {
        QFile::remove(audioPath);
        QFile::remove(markersPath);
        emit errorOccurred(tr(
            "Audio recording captured no samples, so the file was discarded.\n\n"
            "Route clip audio to a Master Audio Output node and load a video clip, then try again."));
    }
}

void ProgramAudioRecorder::submitDeckChunk(int deckIndex, const QByteArray &pcm) {
    if (!m_recording || pcm.isEmpty() || (deckIndex != 0 && deckIndex != 1))
        return;

    if (deckIndex == 0)
        m_deckQueueA.append(pcm);
    else
        m_deckQueueB.append(pcm);

    drainMixQueues();
}

void ProgramAudioRecorder::submitPcm(const QByteArray &pcm) {
    if (!m_recording || pcm.isEmpty())
        return;

    m_pcmQueue.append(pcm);
    drainPcmQueue(false);
}

void ProgramAudioRecorder::submitMicChunk(const QByteArray &pcm) {
    if (!m_recording || pcm.isEmpty())
        return;

    m_micQueue.append(pcm);
    drainMixQueues();
}

void ProgramAudioRecorder::drainPcmQueue(bool flushPartial) {
    const int blockBytes = kMixBlockSamples * kBytesPerSampleFrame;

    while (m_pcmQueue.size() >= blockBytes) {
        encodeMixedBlock(reinterpret_cast<const float *>(m_pcmQueue.constData()), kMixBlockSamples);
        m_pcmQueue.remove(0, blockBytes);
    }

    if (!flushPartial || m_pcmQueue.isEmpty())
        return;

    QVector<float> padded(kMixBlockSamples * kChannels, 0.0f);
    const int sampleCount = m_pcmQueue.size() / static_cast<int>(sizeof(float));
    std::memcpy(padded.data(), m_pcmQueue.constData(), static_cast<size_t>(m_pcmQueue.size()));
    m_pcmQueue.clear();
    encodeMixedBlock(padded.constData(), kMixBlockSamples);
}

void ProgramAudioRecorder::drainMixQueues() {
    const int blockBytes = kMixBlockSamples * kBytesPerSampleFrame;
    QVector<float> mixed(kMixBlockSamples * kChannels);
    QVector<float> blockA(kMixBlockSamples * kChannels);
    QVector<float> blockB(kMixBlockSamples * kChannels);
    QVector<float> blockM(kMixBlockSamples * kChannels);

    while (!m_deckQueueA.isEmpty() || !m_deckQueueB.isEmpty() || !m_micQueue.isEmpty()) {
        const int takenA = takeBlockBytes(m_deckQueueA, blockBytes, blockA.data());
        const int takenB = takeBlockBytes(m_deckQueueB, blockBytes, blockB.data());
        const int takenM = takeBlockBytes(m_micQueue, blockBytes, blockM.data());
        if (takenA == 0 && takenB == 0 && takenM == 0)
            break;

        for (int i = 0; i < kMixBlockSamples * kChannels; ++i) {
            const float sum = blockA[i] + blockB[i] + blockM[i];
            mixed[i] = std::clamp(sum, -1.0f, 1.0f);
        }
        encodeMixedBlock(mixed.constData(), kMixBlockSamples);
    }
}

void ProgramAudioRecorder::encodeMixedBlock(const float *samples, int sampleCount) {
    if (!m_codecCtx || !m_swrCtx || !m_pcmFrame || sampleCount <= 0)
        return;

    const uint8_t *inData[1] = { reinterpret_cast<const uint8_t *>(samples) };
    if (swr_convert(m_swrCtx, m_pcmFrame->data, sampleCount, inData, sampleCount) < 0)
        return;

    m_pcmFrame->nb_samples = sampleCount;
    m_pcmFrame->pts = m_sampleIndex;
    m_sampleIndex += sampleCount;

    if (avcodec_send_frame(m_codecCtx, m_pcmFrame) < 0)
        return;

    while (avcodec_receive_packet(m_codecCtx, m_packet) == 0) {
        av_packet_rescale_ts(m_packet, m_codecCtx->time_base, m_stream->time_base);
        m_packet->stream_index = m_stream->index;
        av_interleaved_write_frame(m_fmtCtx, m_packet);
        av_packet_unref(m_packet);
    }
}

bool ProgramAudioRecorder::flushEncoder() {
    if (!m_codecCtx || !m_packet) return true;

    avcodec_send_frame(m_codecCtx, nullptr);
    while (true) {
        const int ret = avcodec_receive_packet(m_codecCtx, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
            return false;

        av_packet_rescale_ts(m_packet, m_codecCtx->time_base, m_stream->time_base);
        m_packet->stream_index = m_stream->index;
        av_interleaved_write_frame(m_fmtCtx, m_packet);
        av_packet_unref(m_packet);
    }
    return true;
}

void ProgramAudioRecorder::addMarker(const QString &label) {
    if (!m_recording || label.isEmpty()) return;
    m_markers.append({m_timer.elapsed(), label});
}

void ProgramAudioRecorder::writeMarkersFile() const {
    const QJsonDocument doc = buildMarkersJson(
        m_outputPath, m_markers, m_trackLabel,
        m_lastDurationMs > 0 ? m_lastDurationMs : m_timer.elapsed());

    QFile file(m_markersPath);
    if (file.open(QIODevice::WriteOnly))
        file.write(doc.toJson(QJsonDocument::Indented));
}

QJsonDocument ProgramAudioRecorder::buildMarkersJson(const QString &audioPath,
                                                   const QVector<Marker> &markers,
                                                   const QString &trackLabel, qint64 durationMs)
{
    QJsonArray markersArr;
    for (const Marker &m : markers) {
        QJsonObject o;
        o.insert(QStringLiteral("timeMs"), m.timeMs);
        o.insert(QStringLiteral("label"), m.label);
        markersArr.append(o);
    }

    QJsonObject root;
    root.insert(QStringLiteral("audio"), audioPath);
    if (!trackLabel.isEmpty())
        root.insert(QStringLiteral("track"), trackLabel);
    root.insert(QStringLiteral("durationMs"), durationMs);
    root.insert(QStringLiteral("sampleRate"), kSampleRate);
    root.insert(QStringLiteral("markers"), markersArr);

    return QJsonDocument(root);
}

void ProgramAudioRecorder::cleanup() {
    m_deckQueueA.clear();
    m_deckQueueB.clear();
    m_micQueue.clear();
    m_pcmQueue.clear();

    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }
    if (m_pcmFrame) {
        av_frame_free(&m_pcmFrame);
        m_pcmFrame = nullptr;
    }
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }
    if (m_fmtCtx) {
        if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE) && m_fmtCtx->pb)
            avio_closep(&m_fmtCtx->pb);
        avformat_free_context(m_fmtCtx);
        m_fmtCtx = nullptr;
    }
    m_stream = nullptr;
}
