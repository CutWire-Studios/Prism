#pragma once

#include <QString>
#include <QByteArray>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

struct AVFilterGraph;
struct AVFilterContext;

/// FFmpeg audio-file decoder. Resamples any input to interleaved 44.1 kHz
/// stereo float PCM, handed out one chunk at a time (used by AudioPlayer and
/// AudioAnalyzer).
class AudioDecoder {
public:
    static constexpr int kOutputSampleRate = 44100;
    static constexpr int kOutputChannels = 2;

    AudioDecoder();
    ~AudioDecoder();

    bool open(const QString &filePath);
    void close();
    bool isOpen() const { return m_formatCtx != nullptr; }

    bool seek(double seconds);
    bool decodeNextChunk(QByteArray &outChunk);
    bool atEnd() const { return m_eof; }

    /// Time-stretch via FFmpeg's atempo filter: the media plays `speed`× faster
    /// or slower with the pitch preserved.
    void setPlaybackSpeed(double speed);
    double playbackSpeed() const { return m_speed; }

private:
    bool initResampler();
    bool initFilterGraph();
    void freeFilterGraph();

    AVFormatContext *m_formatCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    SwrContext *m_swrCtx = nullptr;
    AVPacket *m_packet = nullptr;
    AVFrame *m_frame = nullptr;
    AVStream *m_audioStream = nullptr;
    int m_audioStreamIndex = -1;
    bool m_sentFlushPacket = false;
    bool m_eof = false;
    // av_seek_frame lands on the packet before the target; the first decoded
    // frames are trimmed to this time so seeks are sample-accurate.
    double m_seekTrimTarget = -1.0;

    // atempo time-stretch graph, present only while m_speed != 1.0.
    AVFilterGraph *m_filterGraph = nullptr;
    AVFilterContext *m_filterSrc = nullptr;
    AVFilterContext *m_filterSink = nullptr;
    AVFrame *m_filtFrame = nullptr;
    bool m_filterFlushed = false;
    double m_speed = 1.0;   // persists across open/close
};
