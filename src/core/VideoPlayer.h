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
    QSize getFrameSize() const;

    void seek(double seconds);

private:
    AVFormatContext *formatContext = nullptr;
    AVStream *videoStream = nullptr;
    AVCodecContext *codecContext = nullptr;
    SwsContext *swsContext = nullptr;

    AVFrame *frameRGB = nullptr;
    uint8_t *buffer = nullptr;

    int videoStreamIndex = -1;
    int64_t frameCount = 0;
};
