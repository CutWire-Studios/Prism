#include "ui/recording/ProgramRecorder.h"

#include "core/media/HwAccel.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
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
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>

namespace {

enum class HwBackend { None, Nvenc, Qsv, Amf, Vaapi };

struct EncoderDef {
    const char *id;
    const char *label;
    const char *encoder;
    HwBackend hw;
};

// Hardware first: Auto tries them in this order and settles on the first that opens. x264 is the
// floor every build has once CI ships the GPL FFmpeg.
const EncoderDef kEncoders[] = {
    {"nvenc", "NVIDIA NVENC", "h264_nvenc", HwBackend::Nvenc},
    {"qsv", "Intel Quick Sync", "h264_qsv", HwBackend::Qsv},
    {"amf", "AMD AMF", "h264_amf", HwBackend::Amf},
    {"vaapi", "VAAPI", "h264_vaapi", HwBackend::Vaapi},
    {"x264", "Software (x264)", "libx264", HwBackend::None},
};
constexpr int kEncoderCount = int(sizeof(kEncoders) / sizeof(kEncoders[0]));

// Constant quality roughly matching x264's default look at recording bitrates.
constexpr int kHwQuality = 23;

const QString kEncoderSettingKey = QStringLiteral("recording/videoEncoder");

AVHWDeviceType hwDeviceType(HwBackend hw)
{
    switch (hw) {
    case HwBackend::Nvenc: return AV_HWDEVICE_TYPE_CUDA;
    case HwBackend::Qsv:   return AV_HWDEVICE_TYPE_QSV;
    case HwBackend::Amf:   return AV_HWDEVICE_TYPE_D3D11VA;
    case HwBackend::Vaapi: return AV_HWDEVICE_TYPE_VAAPI;
    case HwBackend::None:  break;
    }
    return AV_HWDEVICE_TYPE_NONE;
}

bool hwBackendOnThisOs(HwBackend hw)
{
#if defined(Q_OS_WIN)
    return hw == HwBackend::Nvenc || hw == HwBackend::Qsv || hw == HwBackend::Amf;
#elif defined(Q_OS_MACOS)
    Q_UNUSED(hw);
    return false;
#else
    return hw == HwBackend::Nvenc || hw == HwBackend::Qsv || hw == HwBackend::Vaapi;
#endif
}

bool encoderUsable(const EncoderDef &def)
{
    if (!avcodec_find_encoder_by_name(def.encoder))
        return false;
    if (def.hw == HwBackend::None)
        return true;
    return hwBackendOnThisOs(def.hw) && !prism::hwaccel::disabledByEnv()
        && prism::hwaccel::deviceAvailable(hwDeviceType(def.hw));
}

// NV12 when the encoder takes system-memory frames (NVENC, QSV, AMF all do), otherwise its
// hardware surface format (VAAPI), which needs an upload per frame.
AVPixelFormat pickEncodePixFmt(const AVCodec *codec, HwBackend hw)
{
    const void *configs = nullptr;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs,
                                     nullptr) >= 0 && configs) {
        for (auto *p = static_cast<const AVPixelFormat *>(configs); *p != AV_PIX_FMT_NONE; ++p) {
            if (*p == AV_PIX_FMT_NV12)
                return AV_PIX_FMT_NV12;
        }
    }
    const AVHWDeviceType type = hwDeviceType(hw);
    for (int i = 0;; ++i) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config)
            break;
        if ((config->methods
             & (AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX | AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX))
            && config->device_type == type && config->pix_fmt != AV_PIX_FMT_NONE)
            return config->pix_fmt;
    }
    return AV_PIX_FMT_NV12;
}

bool setupHwFrames(AVCodecContext *ctx, AVBufferRef *device, AVPixelFormat hwPixFmt, int w, int h)
{
    AVBufferRef *framesRef = av_hwframe_ctx_alloc(device);
    if (!framesRef)
        return false;
    auto *frames = reinterpret_cast<AVHWFramesContext *>(framesRef->data);
    frames->format = hwPixFmt;
    frames->sw_format = AV_PIX_FMT_NV12;
    frames->width = w;
    frames->height = h;
    frames->initial_pool_size = 20;
    if (av_hwframe_ctx_init(framesRef) < 0) {
        av_buffer_unref(&framesRef);
        return false;
    }
    ctx->hw_frames_ctx = framesRef;
    return true;
}

// Low-latency, constant-quality settings per vendor, adapted from Drift's export rate control.
void applyEncoderOptions(AVCodecContext *ctx, HwBackend hw, AVDictionary **opts)
{
    void *priv = ctx->priv_data;
    switch (hw) {
    case HwBackend::Nvenc:
        av_opt_set(priv, "preset", "p1", 0);
        av_opt_set(priv, "tune", "ll", 0);
        av_opt_set(priv, "rc", "vbr", 0);
        av_opt_set_int(priv, "cq", kHwQuality, 0);
        ctx->bit_rate = 0;
        break;
    case HwBackend::Qsv:
        av_opt_set(priv, "preset", "veryfast", 0);
        ctx->global_quality = kHwQuality;
        break;
    case HwBackend::Amf:
        av_opt_set(priv, "usage", "lowlatency", 0);
        av_opt_set(priv, "quality", "speed", 0);
        av_opt_set(priv, "rc", "cqp", 0);
        av_opt_set_int(priv, "qp_i", kHwQuality, 0);
        av_opt_set_int(priv, "qp_p", kHwQuality, 0);
        break;
    case HwBackend::Vaapi:
        av_opt_set(priv, "rc_mode", "CQP", 0);
        av_opt_set_int(priv, "qp", kHwQuality, 0);
        ctx->global_quality = kHwQuality;
        break;
    case HwBackend::None:
        av_dict_set(opts, "preset", "ultrafast", 0);
        av_dict_set(opts, "tune", "zerolatency", 0);
        av_dict_set(opts, "profile", "baseline", 0);
        break;
    }
}

} // namespace

QList<ProgramRecorder::EncoderOption> ProgramRecorder::availableEncoders()
{
    QList<EncoderOption> out;
    out.append({QStringLiteral("auto"), QObject::tr("Auto (hardware when available)")});
    for (const EncoderDef &def : kEncoders) {
        if (encoderUsable(def))
            out.append({QString::fromLatin1(def.id), QString::fromLatin1(def.label)});
    }
    return out;
}

QString ProgramRecorder::encoderSetting()
{
    return QSettings().value(kEncoderSettingKey, QStringLiteral("auto")).toString();
}

void ProgramRecorder::setEncoderSetting(const QString &id)
{
    QSettings().setValue(kEncoderSettingKey, id);
}

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
    dir = QDir(dir).filePath(QStringLiteral("Prism"));
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
                                     bool writeMarkersOnStop, int width, int height) {
    if (m_recording)
        stopRecording();

    // libx264/yuv420p requires even dimensions.
    m_width  = std::max(2, width  - (width  % 2));
    m_height = std::max(2, height - (height % 2));

    m_outputPath  = outputPath.isEmpty() ? defaultOutputPath() : outputPath;
    m_trackLabel  = trackLabel;
    m_writeMarkersOnStop = writeMarkersOnStop;
    m_markersPath = m_outputPath;
    m_markersPath.replace(QRegularExpression(QStringLiteral("\\.[^.]+$")),
                          QStringLiteral(".markers.json"));
    m_markers.clear();
    m_frameIndex = 0;
    m_lastPts = -1;
    m_lastDurationMs = 0;

    // The chosen encoder first, then everything after it in the table as fallbacks, ending at
    // x264. An explicit software choice skips hardware entirely.
    m_candidates.clear();
    m_candidate = -1;
    const QString choice = encoderSetting();
    int first = 0;
    for (int i = 0; i < kEncoderCount; ++i) {
        if (choice == QLatin1String(kEncoders[i].id))
            first = i;
    }
    for (int i = first; i < kEncoderCount; ++i) {
        if (encoderUsable(kEncoders[i]))
            m_candidates.append(i);
    }

    if (!openPipeline()) {
        cleanup();
        return false;
    }

    m_timer.start();
    m_recording = true;
    emit recordingChanged(true);
    return true;
}

// Opens the output file and the next encoder candidate that works. Called again, after cleanup(),
// when a hardware encoder opened but refused the first frame — nothing has been written then.
bool ProgramRecorder::openPipeline() {
    const QByteArray pathUtf8 = m_outputPath.toUtf8();

    if (avformat_alloc_output_context2(&m_fmtCtx, nullptr, "matroska", pathUtf8.constData()) < 0) {
        emit errorOccurred(tr("Could not create output file format."));
        return false;
    }

    bool opened = false;
    while (!opened && ++m_candidate < m_candidates.size())
        opened = openEncoder(m_candidates[m_candidate]);
    if (!opened) {
        // Last resort for builds without libx264: whatever H.264 encoder FFmpeg has.
        opened = openEncoder(-1);
    }
    if (!opened) {
        emit errorOccurred(tr("Could not open an H.264 encoder."));
        return false;
    }

    m_stream = avformat_new_stream(m_fmtCtx, nullptr);
    if (!m_stream) {
        emit errorOccurred(tr("Could not create video stream."));
        return false;
    }
    m_stream->time_base = m_codecCtx->time_base;
    m_stream->avg_frame_rate = AVRational{kFps, 1};
    m_stream->r_frame_rate   = AVRational{kFps, 1};
    avcodec_parameters_from_context(m_stream->codecpar, m_codecCtx);

    if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&m_fmtCtx->pb, pathUtf8.constData(), AVIO_FLAG_WRITE) < 0) {
            emit errorOccurred(tr("Could not open output file:\n%1").arg(m_outputPath));
            return false;
        }
    }

    if (avformat_write_header(m_fmtCtx, nullptr) < 0) {
        emit errorOccurred(tr("Could not write video header."));
        return false;
    }

    // System-memory frame the RGB input is converted into: NV12 for hardware encoders (and the
    // staging copy for VAAPI's upload), planar 4:2:0 for x264.
    const AVPixelFormat swFormat = m_hwFrame || m_codecCtx->pix_fmt == AV_PIX_FMT_NV12
        ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
    m_yuvFrame = av_frame_alloc();
    m_yuvFrame->format = swFormat;
    m_yuvFrame->width  = m_width;
    m_yuvFrame->height = m_height;
    if (av_frame_get_buffer(m_yuvFrame, 32) < 0) {
        emit errorOccurred(tr("Could not allocate video frame."));
        return false;
    }

    m_swsCtx = sws_getContext(m_width, m_height, AV_PIX_FMT_RGB24,
                              m_width, m_height, swFormat,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsCtx) {
        emit errorOccurred(tr("Could not create color converter."));
        return false;
    }

    m_packet = av_packet_alloc();
    return true;
}

// encoderIndex -1 means FFmpeg's default H.264 encoder.
bool ProgramRecorder::openEncoder(int encoderIndex) {
    const EncoderDef *def = encoderIndex >= 0 ? &kEncoders[encoderIndex] : nullptr;
    const HwBackend hw = def ? def->hw : HwBackend::None;
    const AVCodec *codec = def ? avcodec_find_encoder_by_name(def->encoder)
                               : avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec)
        return false;

    m_codecCtx = avcodec_alloc_context3(codec);
    m_codecCtx->width      = m_width;
    m_codecCtx->height     = m_height;
    m_codecCtx->time_base  = AVRational{1, kFps};
    m_codecCtx->framerate  = AVRational{kFps, 1};
    m_codecCtx->gop_size   = kFps;
    m_codecCtx->max_b_frames = 0;
    m_codecCtx->pix_fmt    = hw == HwBackend::None ? AV_PIX_FMT_YUV420P : pickEncodePixFmt(codec, hw);
    if (m_fmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
        m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(m_codecCtx->pix_fmt);
    const bool needsUpload = desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL);
    if (needsUpload) {
        const QByteArray device = prism::hwaccel::deviceString(hwDeviceType(hw));
        if (av_hwdevice_ctx_create(&m_hwDevice, hwDeviceType(hw),
                                   device.isEmpty() ? nullptr : device.constData(), nullptr, 0) < 0
            || !setupHwFrames(m_codecCtx, m_hwDevice, m_codecCtx->pix_fmt, m_width, m_height)) {
            avcodec_free_context(&m_codecCtx);
            av_buffer_unref(&m_hwDevice);
            return false;
        }
    }

    AVDictionary *opts = nullptr;
    applyEncoderOptions(m_codecCtx, hw, &opts);
    const int rc = avcodec_open2(m_codecCtx, codec, &opts);
    av_dict_free(&opts);
    if (rc < 0) {
        qWarning("ProgramRecorder: %s did not open, trying the next encoder", codec->name);
        avcodec_free_context(&m_codecCtx);
        av_buffer_unref(&m_hwDevice);
        return false;
    }
    if (needsUpload)
        m_hwFrame = av_frame_alloc();
    m_encoderLabel = def ? QString::fromLatin1(def->label) : QString::fromLatin1(codec->name);
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
    // Nothing to drain, and h264_vaapi crashes when flushed before its first frame.
    if (!m_codecCtx || !m_packet || m_frameIndex == 0) return true;

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
    if (rgb.width() != m_width || rgb.height() != m_height) {
        rgb = rgb.scaled(m_width, m_height, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        if (rgb.format() != QImage::Format_RGB888)
            rgb = rgb.convertToFormat(QImage::Format_RGB888);
    }

    const uint8_t *srcSlice[1] = { rgb.constBits() };
    int srcStride[1] = { static_cast<int>(rgb.bytesPerLine()) };
    sws_scale(m_swsCtx, srcSlice, srcStride, 0, m_height,
              m_yuvFrame->data, m_yuvFrame->linesize);

    // Timestamp by wall clock rather than a frame counter: under load the
    // dispatch thread coalesces (drops) frames, so a plain counter would make
    // playback run fast. Wall-clock PTS keeps real-time duration; clamp to stay
    // strictly monotonic when two frames land in the same tick.
    int64_t pts = m_timer.elapsed() * kFps / 1000;
    if (pts <= m_lastPts)
        pts = m_lastPts + 1;
    m_lastPts = pts;
    m_yuvFrame->pts = pts;

    if (sendFrame(m_yuvFrame)) {
        ++m_frameIndex;
        return;
    }
    // A hardware encoder can open cleanly and still refuse its first frame (an unsupported size,
    // a busy session limit). Nothing is in the file yet, so start it over on the next candidate.
    if (m_frameIndex > 0 || m_candidate >= m_candidates.size())
        return;
    qWarning("ProgramRecorder: %s refused the first frame, falling back",
             qUtf8Printable(m_encoderLabel));
    cleanup();
    if (!openPipeline()) {
        cleanup();
        return;
    }
    const uint8_t *retrySlice[1] = { rgb.constBits() };
    sws_scale(m_swsCtx, retrySlice, srcStride, 0, m_height, m_yuvFrame->data, m_yuvFrame->linesize);
    m_yuvFrame->pts = pts;
    if (sendFrame(m_yuvFrame))
        ++m_frameIndex;
}

bool ProgramRecorder::sendFrame(AVFrame *frame) {
    AVFrame *toSend = frame;
    if (m_hwFrame) {
        av_frame_unref(m_hwFrame);
        if (av_hwframe_get_buffer(m_codecCtx->hw_frames_ctx, m_hwFrame, 0) < 0
            || av_hwframe_transfer_data(m_hwFrame, frame, 0) < 0)
            return false;
        m_hwFrame->pts = frame->pts;
        toSend = m_hwFrame;
    }
    if (avcodec_send_frame(m_codecCtx, toSend) < 0)
        return false;

    while (avcodec_receive_packet(m_codecCtx, m_packet) == 0) {
        av_packet_rescale_ts(m_packet, m_codecCtx->time_base, m_stream->time_base);
        m_packet->stream_index = m_stream->index;
        av_interleaved_write_frame(m_fmtCtx, m_packet);
        av_packet_unref(m_packet);
    }
    return true;
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
    if (m_hwFrame)
        av_frame_free(&m_hwFrame);
    av_buffer_unref(&m_hwDevice);
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
