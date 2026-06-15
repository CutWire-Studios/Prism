#include "core/VideoPlayer.h"
#include <QDebug>

VideoPlayer::VideoPlayer() = default;

VideoPlayer::~VideoPlayer() {
    close();
}

bool VideoPlayer::open(const QString &filePath) {
    close();

    const char *filename = filePath.toUtf8().constData();

    if (avformat_open_input(&formatContext, filename, nullptr, nullptr) < 0) {
        qWarning() << "Could not open file:" << filePath;
        return false;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        qWarning() << "Could not find stream info";
        avformat_close_input(&formatContext);
        formatContext = nullptr;
        return false;
    }

    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            videoStream = formatContext->streams[i];
            break;
        }
    }

    if (videoStreamIndex < 0) {
        qWarning() << "No video stream found";
        avformat_close_input(&formatContext);
        formatContext = nullptr;
        return false;
    }

    const AVCodec *codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!codec) {
        qWarning() << "Unsupported codec";
        avformat_close_input(&formatContext);
        formatContext = nullptr;
        return false;
    }

    codecContext = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecContext, videoStream->codecpar);
    codecContext->thread_count = 0; // auto-detect thread count

    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        qWarning() << "Could not open codec";
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        formatContext = nullptr;
        return false;
    }

    frameRGB = av_frame_alloc();
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codecContext->width, codecContext->height, 1);
    buffer = (uint8_t *)av_malloc(numBytes);

    av_image_fill_arrays(&frameRGB->data[0], &frameRGB->linesize[0], buffer, AV_PIX_FMT_RGB24,
                         codecContext->width, codecContext->height, 1);

    swsContext = sws_getContext(codecContext->width, codecContext->height, codecContext->pix_fmt,
                                codecContext->width, codecContext->height, AV_PIX_FMT_RGB24,
                                SWS_BILINEAR, nullptr, nullptr, nullptr);

    qDebug() << "Video opened:" << filePath << "(" << codecContext->width << "x" << codecContext->height << ")";
    return true;
}

void VideoPlayer::close() {
    if (swsContext) {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }
    if (frameRGB) {
        av_frame_free(&frameRGB);
        frameRGB = nullptr;
    }
    if (buffer) {
        av_free(buffer);
        buffer = nullptr;
    }
    if (codecContext) {
        avcodec_free_context(&codecContext);
        codecContext = nullptr;
    }
    if (formatContext) {
        avformat_close_input(&formatContext);
        formatContext = nullptr;
    }
    videoStreamIndex = -1;
    frameCount = 0;
}

int VideoPlayer::getWidth() const {
    return codecContext ? codecContext->width : 0;
}

int VideoPlayer::getHeight() const {
    return codecContext ? codecContext->height : 0;
}

double VideoPlayer::getDuration() const {
    if (!formatContext) return 0.0;

    // Stream-level duration (most accurate when present)
    if (videoStream && videoStream->duration != AV_NOPTS_VALUE)
        return static_cast<double>(videoStream->duration) * av_q2d(videoStream->time_base);

    // Container-level fallback (covers H.264/MP4 and most modern formats)
    if (formatContext->duration != AV_NOPTS_VALUE)
        return static_cast<double>(formatContext->duration) / AV_TIME_BASE;

    return 0.0;
}

double VideoPlayer::getCurrentTime() const {
    if (!formatContext) {
        return 0.0;
    }
    return (double)frameCount / av_q2d(videoStream->r_frame_rate);
}

bool VideoPlayer::decodeFrame() {
    if (!formatContext || videoStreamIndex < 0) {
        return false;
    }

    AVPacket packet;
    AVFrame *frame = av_frame_alloc();
    bool frameDecoded = false;

    while (av_read_frame(formatContext, &packet) >= 0) {
        if (packet.stream_index == videoStreamIndex) {
            int ret = avcodec_send_packet(codecContext, &packet);
            if (ret < 0) {
                av_packet_unref(&packet);
                av_frame_free(&frame);
                return false;
            }

            ret = avcodec_receive_frame(codecContext, frame);
            if (ret == 0) {
                sws_scale(swsContext, frame->data, frame->linesize, 0, codecContext->height,
                         frameRGB->data, frameRGB->linesize);
                frameCount++;
                frameDecoded = true;
                av_packet_unref(&packet);
                av_frame_free(&frame);
                return true;
            }
        }
        av_packet_unref(&packet);
    }

    av_frame_free(&frame);
    return frameDecoded;
}

const uint8_t *VideoPlayer::getFrameData() const {
    return frameRGB ? frameRGB->data[0] : nullptr;
}

QSize VideoPlayer::getFrameSize() const {
    return QSize(getWidth(), getHeight());
}

void VideoPlayer::seek(double seconds) {
    if (!formatContext || videoStreamIndex < 0) {
        return;
    }

    int64_t timestamp = (int64_t)(seconds / av_q2d(videoStream->time_base));
    av_seek_frame(formatContext, videoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codecContext);
    frameCount = (int64_t)(seconds * av_q2d(videoStream->r_frame_rate));
}
