#pragma once

#include <QString>
#include <QByteArray>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

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

private:
    AVFormatContext *m_formatCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    SwrContext *m_swrCtx = nullptr;
    AVPacket *m_packet = nullptr;
    AVFrame *m_frame = nullptr;
    AVStream *m_audioStream = nullptr;
    int m_audioStreamIndex = -1;
    bool m_sentFlushPacket = false;
    bool m_eof = false;
};
