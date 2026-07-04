#pragma once

#include <QString>
#include <QSize>
#include <memory>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

/// FFmpeg-based video-file decoder. Decodes frames to RGB24 on demand and
/// exposes duration/seek; wrapped by VideoFileSource to feed a deck.
class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    bool open(const QString &filePath);
    void close();
    bool isOpen() const { return formatContext != nullptr; }

    int getWidth() const;
    int getHeight() const;
    double getDuration() const;
    double getCurrentTime() const;

    bool decodeFrame();
    const uint8_t *getFrameData() const;
    int getFrameBytesPerLine() const;
    QSize getFrameSize() const;

    void seek(double seconds);

    static bool fileHasAudio(const QString &filePath);

private:
    AVFormatContext *formatContext = nullptr;
    AVStream *videoStream = nullptr;
    AVCodecContext *codecContext = nullptr;
    SwsContext *swsContext = nullptr;

    AVFrame *frameRGB = nullptr;
    uint8_t *buffer = nullptr;

    // Reused across decodeFrame() calls to avoid per-frame alloc/free churn.
    AVPacket *packet = nullptr;
    AVFrame  *decodedFrame = nullptr;

    int videoStreamIndex = -1;
    int64_t frameCount = 0;
};
