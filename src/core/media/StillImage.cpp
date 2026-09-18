#include "core/media/StillImage.h"
#include "core/media/MediaProbe.h"

#include <QImageReader>
#include <QTransform>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace StillImage {

QImage decode(const QString &path, int maxWidth, int maxHeight) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (maxWidth > 0 && maxHeight > 0) {
        QSize size = reader.size();
        if (size.isValid() && (size.width() > maxWidth || size.height() > maxHeight)) {
            size.scale(maxWidth, maxHeight, Qt::KeepAspectRatio);
            reader.setScaledSize(size);
        }
    }
    const QImage image = reader.read();
    if (!image.isNull())
        return image;
    // libavformat has an svg demuxer but libavcodec has no svg decoder.
    if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
        return {};
    return decodeVideoFrame(path, 0.0, maxWidth, maxHeight);
}

QImage decodeVideoFrame(const QString &path, double seconds, int maxWidth, int maxHeight) {
    AVFormatContext *fmt = nullptr;
    const QByteArray utf8 = path.toUtf8();
    if (avformat_open_input(&fmt, utf8.constData(), nullptr, nullptr) < 0)
        return {};
    AVCodecContext *dec = nullptr;
    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    QImage out;

    auto cleanup = [&] {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
    };

    if (avformat_find_stream_info(fmt, nullptr) < 0) { cleanup(); return {}; }

    int idx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVStream *s = fmt->streams[i];
        if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
            && !(s->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            idx = int(i);
            break;
        }
    }
    if (idx < 0) { cleanup(); return {}; }
    AVStream *stream = fmt->streams[idx];

    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) { cleanup(); return {}; }
    dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, stream->codecpar);
    dec->thread_count = 0;
    if (avcodec_open2(dec, codec, nullptr) < 0) { cleanup(); return {}; }

    const int64_t start = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    int64_t target = start;
    if (seconds > 0.0) {
        target = start + av_rescale_q(int64_t(seconds * AV_TIME_BASE), AV_TIME_BASE_Q,
                                      stream->time_base);
        av_seek_frame(fmt, idx, target, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec);
    }

    bool got = false;
    bool drained = false;
    while (!got) {
        const int r = avcodec_receive_frame(dec, frame);
        if (r == 0) {
            // Decode forward to the requested time; keep the last frame as a
            // fallback in case the file ends first.
            const int64_t pts = frame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE || pts >= target || seconds <= 0.0) {
                got = true;
                break;
            }
            av_frame_unref(frame);
            continue;
        }
        if (r == AVERROR_EOF || drained)
            break;
        if (av_read_frame(fmt, pkt) < 0) {
            avcodec_send_packet(dec, nullptr);
            drained = true;
            continue;
        }
        if (pkt->stream_index == idx)
            avcodec_send_packet(dec, pkt);
        av_packet_unref(pkt);
    }

    if (got) {
        QSize size(frame->width, frame->height);
        if (maxWidth > 0 && maxHeight > 0
            && (size.width() > maxWidth || size.height() > maxHeight))
            size.scale(maxWidth, maxHeight, Qt::KeepAspectRatio);
        size = size.expandedTo(QSize(1, 1));

        SwsContext *sws = sws_getContext(frame->width, frame->height,
                                         AVPixelFormat(frame->format),
                                         size.width(), size.height(), AV_PIX_FMT_RGBA,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (sws) {
            out = QImage(size, QImage::Format_RGBA8888);
            uint8_t *dst[4] = {out.bits(), nullptr, nullptr, nullptr};
            int dstStride[4] = {int(out.bytesPerLine()), 0, 0, 0};
            sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
            sws_freeContext(sws);

            const int rotation = displayRotationOf(stream);
            if (rotation != 0)
                out = out.transformed(QTransform().rotate(rotation));
        }
    }

    cleanup();
    return out;
}

} // namespace StillImage
