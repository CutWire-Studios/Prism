#pragma once

#include "core/sources/MediaSource.h"
#include "core/media/VideoDecoder.h"

#include <QByteArray>
#include <memory>

class QOpenGLFramebufferObject;

// MediaSource implementation backed by an FFmpeg video file or URL.
//
// Decoding runs on VideoDecoder's worker thread (hardware where available); each presented frame
// is converted to RGBA on the GPU by GpuVideoUploader and handed to consumers through glTexture().
// frameData() reads that texture back on demand for the few CPU consumers. When the GPU path is
// unavailable, frames are converted to RGB24 on the CPU instead and glTexture() stays 0.
class VideoFileSource : public MediaSource {
public:
    VideoFileSource();
    ~VideoFileSource() override;

    bool open(const QString &filePath);

    Type    type()        const override { return Type::VideoFile; }
    bool    isReady()     const override;
    QSize   frameSize()   const override { return m_size; }
    const uint8_t *frameData() const override;
    int     frameBytesPerLine() const override { return m_size.width() * 3; }
    bool    nextFrame()         override;
    bool    presentAt(double seconds) override;
    double  duration()    const override;
    double  currentTime() const override { return m_time; }
    void    seek(double s)      override;
    QString displayName() const override { return m_name; }
    // The last frame stays up across seeks and reopens until the next one lands.
    unsigned int glTexture() const override { return m_texture; }

    // Reopens every live decoder, e.g. after the decode mode preference changed.
    static void reopenAll();

private:
    bool present(DecodedVideoFrame &frame);
    bool convertOnCpu(const AVFrame *frame);
    void noteEndIfDrained();

    VideoDecoder m_decoder;
    DecodedVideoFrame m_frame;
    std::unique_ptr<QOpenGLFramebufferObject> m_target;
    unsigned int m_texture = 0;
    bool m_hasFrame = false;
    bool m_useCpu = false;
    double m_time = 0.0;
    QSize m_size;
    QString m_name;

    mutable QByteArray m_rgb;
    mutable bool m_rgbStale = false;
    struct SwsContext *m_sws = nullptr;
};
