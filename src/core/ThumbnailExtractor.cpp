#include "core/ThumbnailExtractor.h"
#include <QDebug>
#include <QImage>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

QPixmap ThumbnailExtractor::extract(const QString &filePath, int width, int height) {
    AVFormatContext *formatCtx = nullptr;
    if (avformat_open_input(&formatCtx, filePath.toUtf8().constData(), nullptr, nullptr) < 0)
        return {};

    if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
        avformat_close_input(&formatCtx);
        return {};
    }

    int videoIdx = -1;
    for (unsigned int i = 0; i < formatCtx->nb_streams; ++i) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIdx = static_cast<int>(i);
            break;
        }
    }

    if (videoIdx < 0) {
        avformat_close_input(&formatCtx);
        return {};
    }

    AVStream *stream = formatCtx->streams[videoIdx];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&formatCtx);
        return {};
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, stream->codecpar);
    codecCtx->thread_count = 1;

    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return {};
    }

    // Seek to ~10% into the file for a more representative frame
    if (formatCtx->duration > 0) {
        int64_t seekTarget = formatCtx->duration / 10;
        av_seek_frame(formatCtx, -1, seekTarget, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codecCtx);
    }

    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    bool gotFrame = false;

    while (!gotFrame && av_read_frame(formatCtx, packet) >= 0) {
        if (packet->stream_index == videoIdx) {
            if (avcodec_send_packet(codecCtx, packet) == 0) {
                if (avcodec_receive_frame(codecCtx, frame) == 0)
                    gotFrame = true;
            }
        }
        av_packet_unref(packet);
    }

    QPixmap result;

    if (gotFrame) {
        SwsContext *swsCtx = sws_getContext(
            codecCtx->width, codecCtx->height, codecCtx->pix_fmt,
            width, height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (swsCtx) {
            int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width, height, 1);
            uint8_t *buf = static_cast<uint8_t *>(av_malloc(numBytes));

            AVFrame *rgbFrame = av_frame_alloc();
            av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buf,
                                 AV_PIX_FMT_RGB24, width, height, 1);

            sws_scale(swsCtx, frame->data, frame->linesize, 0,
                      codecCtx->height, rgbFrame->data, rgbFrame->linesize);

            QImage img(rgbFrame->data[0], width, height, rgbFrame->linesize[0],
                       QImage::Format_RGB888);
            result = QPixmap::fromImage(img.copy());

            av_frame_free(&rgbFrame);
            av_free(buf);
            sws_freeContext(swsCtx);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&formatCtx);

    return result;
}
