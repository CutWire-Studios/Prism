#include "ui/recording/ProgramRecorder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

ProgramRecorder::ProgramRecorder(QObject *parent)
    : QObject(parent)
{
}

ProgramRecorder::~ProgramRecorder() {
    stopRecording();
}

QString ProgramRecorder::defaultOutputDir() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    dir = QDir(dir).filePath(QStringLiteral("SwitchX"));
    QDir().mkpath(dir);
    return dir;
}

QString ProgramRecorder::makeOutputPath(const QString &dir, const QString &stem, const QString &suffix) {
    const QString base = suffix.isEmpty() ? stem : stem + QLatin1Char('_') + suffix;
    return QDir(dir).filePath(base + QStringLiteral(".mkv"));
}

QString ProgramRecorder::defaultOutputPath() {
    const QString dir = defaultOutputDir();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    return makeOutputPath(dir, stamp, QStringLiteral("program"));
}

qint64 ProgramRecorder::recordingDurationMs() const {
    if (m_lastDurationMs > 0)
        return m_lastDurationMs;
    return m_recording ? m_timer.elapsed() : 0;
}

bool ProgramRecorder::startRecording(const QString &outputPath, const QString &trackLabel,
                                     bool writeMarkersOnStop) {
    if (m_recording)
        stopRecording();

    m_outputPath  = outputPath.isEmpty() ? defaultOutputPath() : outputPath;
    m_trackLabel  = trackLabel;
    m_writeMarkersOnStop = writeMarkersOnStop;
    m_markersPath = m_outputPath;
    m_markersPath.replace(QRegularExpression(QStringLiteral("\\.[^.]+$")),
                          QStringLiteral(".markers.json"));
    m_markers.clear();
    m_frameIndex = 0;
    m_lastDurationMs = 0;

    const QByteArray pathUtf8 = m_outputPath.toUtf8();

    if (avformat_alloc_output_context2(&m_fmtCtx, nullptr, "matroska", pathUtf8.constData()) < 0) {
        emit errorOccurred(tr("Could not create output file format."));
        cleanup();
        return false;
    }

    const AVCodec *codec = avcodec_find_encoder_by_name("libx264");
    if (!codec)
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        emit errorOccurred(tr("H.264 encoder not found."));
        cleanup();
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    m_codecCtx->width      = kWidth;
    m_codecCtx->height     = kHeight;
    m_codecCtx->time_base  = AVRational{1, kFps};
    m_codecCtx->framerate  = AVRational{kFps, 1};
    m_codecCtx->gop_size   = kFps;
    m_codecCtx->max_b_frames = 0;
    m_codecCtx->pix_fmt    = AV_PIX_FMT_YUV420P;

    if (m_fmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
        m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "preset", "ultrafast", 0);
    av_dict_set(&opts, "tune", "zerolatency", 0);
    av_dict_set(&opts, "profile", "baseline", 0);
    if (avcodec_open2(m_codecCtx, codec, &opts) < 0) {
        av_dict_free(&opts);
        emit errorOccurred(tr("Could not open H.264 encoder."));
        cleanup();
        return false;
    }
    av_dict_free(&opts);

    m_stream = avformat_new_stream(m_fmtCtx, nullptr);
    if (!m_stream) {
        emit errorOccurred(tr("Could not create video stream."));
        cleanup();
        return false;
    }
    m_stream->time_base = m_codecCtx->time_base;
    m_stream->avg_frame_rate = AVRational{kFps, 1};
    m_stream->r_frame_rate   = AVRational{kFps, 1};
    avcodec_parameters_from_context(m_stream->codecpar, m_codecCtx);

    if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&m_fmtCtx->pb, pathUtf8.constData(), AVIO_FLAG_WRITE) < 0) {
            emit errorOccurred(tr("Could not open output file:\n%1").arg(m_outputPath));
            cleanup();
            return false;
        }
    }

    if (avformat_write_header(m_fmtCtx, nullptr) < 0) {
        emit errorOccurred(tr("Could not write video header."));
        cleanup();
        return false;
    }

    m_yuvFrame = av_frame_alloc();
    m_yuvFrame->format = AV_PIX_FMT_YUV420P;
    m_yuvFrame->width  = kWidth;
    m_yuvFrame->height = kHeight;
    if (av_frame_get_buffer(m_yuvFrame, 32) < 0) {
        emit errorOccurred(tr("Could not allocate video frame."));
        cleanup();
        return false;
    }

    m_swsCtx = sws_getContext(kWidth, kHeight, AV_PIX_FMT_RGB24,
                              kWidth, kHeight, AV_PIX_FMT_YUV420P,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsCtx) {
        emit errorOccurred(tr("Could not create color converter."));
        cleanup();
        return false;
    }

    m_packet = av_packet_alloc();
    m_timer.start();
    m_recording = true;
    emit recordingChanged(true);
    return true;
}

void ProgramRecorder::stopRecording() {
    if (!m_recording) return;

    const int64_t framesWritten = m_frameIndex;
    const QString videoPath = m_outputPath;
    const QString markersPath = m_markersPath;

    flushEncoder();
    if (m_fmtCtx && framesWritten > 0) {
        av_write_trailer(m_fmtCtx);
    }
    if (m_writeMarkersOnStop && framesWritten > 0)
        writeMarkersFile();

    m_lastDurationMs = m_timer.elapsed();
    cleanup();

    m_recording = false;
    emit recordingChanged(false);

    if (framesWritten == 0) {
        QFile::remove(videoPath);
        QFile::remove(markersPath);
        emit errorOccurred(tr(
            "Recording captured no video frames, so the file was discarded.\n\n"
            "Load media on a deck or start a live source, then try again."));
    }
}

bool ProgramRecorder::flushEncoder() {
    if (!m_codecCtx || !m_packet) return true;

    avcodec_send_frame(m_codecCtx, nullptr);
    while (true) {
        const int ret = avcodec_receive_packet(m_codecCtx, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0)
            return false;

        av_packet_rescale_ts(m_packet, m_codecCtx->time_base, m_stream->time_base);
        m_packet->stream_index = m_stream->index;
        av_interleaved_write_frame(m_fmtCtx, m_packet);
        av_packet_unref(m_packet);
    }
    return true;
}

void ProgramRecorder::submitFrame(const QImage &frame) {
    if (!m_recording || !m_codecCtx || !m_swsCtx || !m_yuvFrame) return;

    QImage rgb = frame;
    if (rgb.format() != QImage::Format_RGB888) {
        rgb = rgb.convertToFormat(QImage::Format_RGB888);
    }
    if (rgb.width() != kWidth || rgb.height() != kHeight) {
        rgb = rgb.scaled(kWidth, kHeight, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        if (rgb.format() != QImage::Format_RGB888)
            rgb = rgb.convertToFormat(QImage::Format_RGB888);
    }

    const uint8_t *srcSlice[1] = { rgb.constBits() };
    int srcStride[1] = { static_cast<int>(rgb.bytesPerLine()) };
    sws_scale(m_swsCtx, srcSlice, srcStride, 0, kHeight,
              m_yuvFrame->data, m_yuvFrame->linesize);

    m_yuvFrame->pts = m_frameIndex++;
    if (avcodec_send_frame(m_codecCtx, m_yuvFrame) < 0)
        return;

    while (avcodec_receive_packet(m_codecCtx, m_packet) == 0) {
        av_packet_rescale_ts(m_packet, m_codecCtx->time_base, m_stream->time_base);
        m_packet->stream_index = m_stream->index;
        av_interleaved_write_frame(m_fmtCtx, m_packet);
        av_packet_unref(m_packet);
    }
}

void ProgramRecorder::addMarker(const QString &label) {
    if (!m_recording || label.isEmpty()) return;
    m_markers.append({m_timer.elapsed(), label});
}

void ProgramRecorder::writeMarkersFile() const {
    const QJsonDocument doc = buildMarkersJson(
        m_outputPath, m_markers, m_trackLabel,
        m_lastDurationMs > 0 ? m_lastDurationMs : m_timer.elapsed(), kFps);

    QFile file(m_markersPath);
    if (file.open(QIODevice::WriteOnly))
        file.write(doc.toJson(QJsonDocument::Indented));
}

QJsonDocument ProgramRecorder::buildMarkersJson(const QString &videoPath, const QVector<Marker> &markers,
                                                const QString &trackLabel, qint64 durationMs, int frameRate)
{
    QJsonArray markersArr;
    for (const Marker &m : markers) {
        QJsonObject o;
        o.insert(QStringLiteral("timeMs"), m.timeMs);
        o.insert(QStringLiteral("label"), m.label);
        markersArr.append(o);
    }

    QJsonObject root;
    root.insert(QStringLiteral("video"), videoPath);
    if (!trackLabel.isEmpty())
        root.insert(QStringLiteral("track"), trackLabel);
    root.insert(QStringLiteral("durationMs"), durationMs);
    root.insert(QStringLiteral("frameRate"), frameRate);
    root.insert(QStringLiteral("markers"), markersArr);

    return QJsonDocument(root);
}

void ProgramRecorder::cleanup() {
    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }
    if (m_yuvFrame) {
        av_frame_free(&m_yuvFrame);
        m_yuvFrame = nullptr;
    }
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }
    if (m_fmtCtx) {
        if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE) && m_fmtCtx->pb)
            avio_closep(&m_fmtCtx->pb);
        avformat_free_context(m_fmtCtx);
        m_fmtCtx = nullptr;
    }
    m_stream = nullptr;
}
