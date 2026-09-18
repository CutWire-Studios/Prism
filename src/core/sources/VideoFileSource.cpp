#include "core/sources/VideoFileSource.h"
#include "core/media/GpuVideoUploader.h"

#include <QFileInfo>
#include <QImage>
#include <QOpenGLFramebufferObject>
#include <QSet>
#include <QTransform>

#include <cstring>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace {

// How long nextFrame() waits for the first frame after open or seek. Callers (deck load, seek
// while paused, thumbnails) expect that frame synchronously, as they did when decoding was.
constexpr int kFirstFrameWaitMs = 2000;

QSet<VideoFileSource *> &liveSources()
{
    static QSet<VideoFileSource *> sources;
    return sources;
}

QSize displaySize(int w, int h, int rotation)
{
    w &= ~1;
    h &= ~1;
    return (rotation == 90 || rotation == 270) ? QSize(h, w) : QSize(w, h);
}

} // namespace

VideoFileSource::VideoFileSource()
{
    liveSources().insert(this);
}

VideoFileSource::~VideoFileSource()
{
    liveSources().remove(this);
    m_decoder.close();
    m_frame = {};
    prism::GpuVideoUploader::instance().release(m_target);
    if (m_sws)
        sws_freeContext(m_sws);
}

void VideoFileSource::reopenAll()
{
    for (VideoFileSource *source : liveSources()) {
        source->m_decoder.reopen(source->m_time);
        source->m_hasFrame = false;
    }
}

bool VideoFileSource::open(const QString &filePath)
{
    m_name = QFileInfo(filePath).fileName();
    m_hasFrame = false;
    m_time = 0.0;
    if (!m_decoder.open(filePath))
        return false;
    const QSize coded = m_decoder.codedSize();
    m_size = displaySize(coded.width(), coded.height(), m_decoder.rotation());
    // Black until the first frame lands, so frameData() is never null for a ready source.
    m_rgb = QByteArray(m_size.width() * m_size.height() * 3, '\0');
    m_rgbStale = false;
    m_useCpu = !prism::GpuVideoUploader::instance().available();
    return true;
}

bool VideoFileSource::isReady() const
{
    return m_decoder.isOpen();
}

double VideoFileSource::duration() const
{
    return m_decoder.duration();
}

void VideoFileSource::seek(double s)
{
    m_decoder.seek(s);
    m_time = s;
    m_hasFrame = false;
}

bool VideoFileSource::nextFrame()
{
    DecodedVideoFrame frame;
    if (!m_decoder.popNext(frame, m_hasFrame ? 0 : kFirstFrameWaitMs)) {
        noteEndIfDrained();
        return false;
    }
    return present(frame);
}

bool VideoFileSource::presentAt(double seconds)
{
    DecodedVideoFrame frame;
    if (!m_decoder.popUpTo(seconds, frame)) {
        noteEndIfDrained();
        return false;
    }
    return present(frame);
}

void VideoFileSource::noteEndIfDrained()
{
    // The last frame's pts is one frame short of the duration; report the end once the stream is
    // exhausted so non-repeating decks stop and repeating ones loop.
    if (m_hasFrame && m_decoder.drained() && duration() > 0.0 && m_time < duration())
        m_time = duration();
}

bool VideoFileSource::present(DecodedVideoFrame &frame)
{
    const AVFrame *av = frame.frame.get();
    const int rotation = m_decoder.rotation();

    bool ok = false;
    if (!m_useCpu) {
        const GLuint tex = prism::GpuVideoUploader::instance().render(av, rotation, m_target);
        if (tex) {
            m_texture = tex;
            m_size = displaySize(av->width, av->height, rotation);
            m_rgbStale = true;
            ok = true;
        }
    }
    if (!ok) {
        m_texture = 0;
        ok = convertOnCpu(av);
    }
    if (!ok)
        return false;

    m_frame = std::move(frame);
    m_time = m_frame.ptsUs / 1e6;
    m_hasFrame = true;
    return true;
}

bool VideoFileSource::convertOnCpu(const AVFrame *frame)
{
    AVFrame *transferred = nullptr;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(AVPixelFormat(frame->format));
    if (desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) {
        transferred = av_frame_alloc();
        if (av_hwframe_transfer_data(transferred, frame, 0) < 0) {
            av_frame_free(&transferred);
            return false;
        }
        frame = transferred;
    }

    const int w = frame->width & ~1;
    const int h = frame->height & ~1;
    m_sws = sws_getCachedContext(m_sws, frame->width, frame->height,
                                 AVPixelFormat(frame->format), w, h, AV_PIX_FMT_RGB24,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws || w < 2 || h < 2) {
        av_frame_free(&transferred);
        return false;
    }
    QImage rgb(w, h, QImage::Format_RGB888);
    uint8_t *dst[4] = {rgb.bits(), nullptr, nullptr, nullptr};
    int dstStride[4] = {int(rgb.bytesPerLine()), 0, 0, 0};
    sws_scale(m_sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
    av_frame_free(&transferred);

    if (const int rotation = m_decoder.rotation())
        rgb = rgb.transformed(QTransform().rotate(rotation));

    m_size = rgb.size();
    const int rowBytes = m_size.width() * 3;
    m_rgb.resize(rowBytes * m_size.height());
    for (int y = 0; y < m_size.height(); ++y)
        memcpy(m_rgb.data() + y * rowBytes, rgb.constScanLine(y), size_t(rowBytes));
    m_rgbStale = false;
    return true;
}

const uint8_t *VideoFileSource::frameData() const
{
    if (m_rgbStale && m_target) {
        m_rgb.resize(m_size.width() * m_size.height() * 3);
        prism::GpuVideoUploader::instance().readback(
            m_target.get(), reinterpret_cast<uint8_t *>(m_rgb.data()));
        m_rgbStale = false;
    }
    return reinterpret_cast<const uint8_t *>(m_rgb.constData());
}
