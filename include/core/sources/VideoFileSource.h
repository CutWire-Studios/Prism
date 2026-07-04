#pragma once

#include "core/sources/MediaSource.h"
#include "core/media/VideoPlayer.h"
#include <memory>

// MediaSource implementation backed by an FFmpeg video file.
class VideoFileSource : public MediaSource {
public:
    VideoFileSource();
    ~VideoFileSource() override = default;

    bool open(const QString &filePath);

    Type    type()        const override { return Type::VideoFile; }
    bool    isReady()     const override;
    QSize   frameSize()   const override;
    const uint8_t *frameData() const override;
    int     frameBytesPerLine() const override;
    bool    nextFrame()         override;
    double  duration()    const override;
    double  currentTime() const override;
    void    seek(double s)      override;
    QString displayName() const override { return m_name; }

private:
    std::unique_ptr<VideoPlayer> m_player;
    QString m_name;
};
