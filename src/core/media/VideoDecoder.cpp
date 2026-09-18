#include "core/media/VideoDecoder.h"

#include "core/media/GpuVideoUploader.h"
#include "core/media/MediaProbe.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QtGlobal>

#include <chrono>
#include <cmath>
#include <cstdarg>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/log.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace hwaccel = prism::hwaccel;

namespace {

std::atomic<int> g_mode{static_cast<int>(VideoDecoder::Mode::Auto)};
std::atomic<int> g_pinnedBackend{static_cast<int>(hwaccel::Backend::None)};
std::atomic<quint64> g_hwFallbackCount{0};

// Why the last decoder gave up on hardware. The count alone says that it happened, which is not
// the half a bug report needs.
QMutex g_hwFailureMutex;
QString g_lastHwFailure;
// The most recent error FFmpeg logged. A hwaccel usually explains itself only there ("Failed
// setup for format", a CUDA error name) while the call that fails returns a bare EINVAL.
QString g_lastFfmpegError;

void recordingLogCallback(void *avcl, int level, const char *fmt, va_list vl)
{
    // Only while errors are being logged at all: HwAccel's device probes drop the level to
    // AV_LOG_QUIET, and what they complain about is not a decode failure.
    if (level <= AV_LOG_ERROR && av_log_get_level() >= AV_LOG_ERROR) {
        va_list copy;
        va_copy(copy, vl);
        char line[512];
        int printPrefix = 1;
        av_log_format_line2(avcl, level, fmt, copy, line, sizeof(line), &printPrefix);
        va_end(copy);
        const QString text = QString::fromUtf8(line).trimmed();
        if (!text.isEmpty()) {
            QMutexLocker lock(&g_hwFailureMutex);
            g_lastFfmpegError = text;
        }
    }
    av_log_default_callback(avcl, level, fmt, vl);
}

void installRecordingLogCallback()
{
    static std::once_flag once;
    std::call_once(once, [] { av_log_set_callback(recordingLogCallback); });
}

// One CUDA device for every decoder. Each av_hwdevice_ctx_create makes a CUDA context of its own,
// while GpuVideoUploader's interop textures are registered with exactly one; two hardware clips on
// separate contexts made every switch between them fail to map. NVDEC decoders share a context
// without trouble. Never released: tearing a CUDA context down during the driver's own exit
// teardown aborts the process.
AVBufferRef *sharedCudaDevice()
{
    static QMutex mutex;
    static AVBufferRef *device = nullptr;
    QMutexLocker lock(&mutex);
    if (!device && av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0) < 0) {
        av_buffer_unref(&device);
        return nullptr;
    }
    return av_buffer_ref(device);
}

bool isHardwarePixelFormat(AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    return desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL);
}

int swsColorspaceFromFrame(const AVFrame *frame)
{
    switch (frame->colorspace) {
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        return SWS_CS_ITU601;
    case AVCOL_SPC_SMPTE240M:
        return SWS_CS_SMPTE240M;
    case AVCOL_SPC_FCC:
        return SWS_CS_FCC;
    default:
        return SWS_CS_ITU709;
    }
}

bool isFullRange(const AVFrame *frame)
{
    if (frame->color_range == AVCOL_RANGE_JPEG)
        return true;
    switch (frame->format) {
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_YUVJ440P:
    case AV_PIX_FMT_YUVJ411P:
        return true;
    default:
        return false;
    }
}

// Builds the decoder's surface pool here rather than letting libavcodec do it, so the extra
// surfaces the frame queue wants are fitted under a driver's hard cap instead of added on top of
// it. Anything unexpected leaves hw_frames_ctx unset, which is simply the uncapped default.
void fitHwSurfacePool(AVCodecContext *ctx, AVPixelFormat format,
                      VideoDecoder::HwFormatRequest *request)
{
    av_buffer_unref(&ctx->hw_frames_ctx);
    if (!ctx->hw_device_ctx)
        return;

    const int wantedExtra = qMax(0, ctx->extra_hw_frames);
    ctx->extra_hw_frames = 0;
    AVBufferRef *frames = nullptr;
    const int rc = avcodec_get_hw_frames_parameters(ctx, ctx->hw_device_ctx, format, &frames);
    ctx->extra_hw_frames = wantedExtra;
    if (rc < 0 || !frames)
        return;

    auto *framesCtx = reinterpret_cast<AVHWFramesContext *>(frames->data);
    if (framesCtx->initial_pool_size <= 0) {
        av_buffer_unref(&frames);
        return;
    }
    const int base = framesCtx->initial_pool_size + 3;
    const int spare = qBound(0, request->maxSurfaces - base, wantedExtra);
    framesCtx->initial_pool_size = base + spare;
    if (av_hwframe_ctx_init(frames) < 0) {
        av_buffer_unref(&frames);
        return;
    }
    ctx->hw_frames_ctx = frames;
}

// Prefer the hardware surface format when the decoder offers it; otherwise pick the first
// software format so get_format never hard-fails with AV_PIX_FMT_NONE (that path leaves the
// hwaccel decoder half-initialised).
AVPixelFormat hwGetFormat(AVCodecContext *ctx, const AVPixelFormat *pixFmts)
{
    auto *request = ctx && ctx->opaque ? static_cast<VideoDecoder::HwFormatRequest *>(ctx->opaque)
                                       : nullptr;
    const AVPixelFormat prefer = request ? request->pixFmt : AV_PIX_FMT_NONE;

    for (const AVPixelFormat *p = pixFmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == prefer) {
            if (request->maxSurfaces > 0)
                fitHwSurfacePool(ctx, *p, request);
            return *p;
        }
    }
    for (const AVPixelFormat *p = pixFmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (!isHardwarePixelFormat(*p))
            return *p;
    }
    return pixFmts ? pixFmts[0] : AV_PIX_FMT_NONE;
}

QString avErrorText(int rc)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(rc, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

void freeFrame(AVFrame *f)
{
    av_frame_free(&f);
}

// Hardware decode is cheap, but reading a surface back costs more than software decoding a light
// stream — which is what happens whenever zero-copy import is unavailable. Auto keeps light clips
// on the CPU and sends 4K / high-bitrate ones to the GPU.
constexpr double kHwAccelMinKbitPerFrame = 250.0;
// 1080p60 sits just over this; 1080p30 and 720p60 sit under it.
constexpr double kHwAccelMinPixelsPerSecond = 1920.0 * 1080.0 * 50.0;

} // namespace

VideoDecoder::VideoDecoder() = default;

VideoDecoder::~VideoDecoder()
{
    close();
}

int VideoDecoder::interruptCallback(void *opaque)
{
    return static_cast<VideoDecoder *>(opaque)->m_stop.load(std::memory_order_relaxed) ? 1 : 0;
}

bool VideoDecoder::open(const QString &path)
{
    close();
    static const bool networkReady = (avformat_network_init(), true);
    Q_UNUSED(networkReady);

    m_stop = false;
    AVFormatContext *fmt = avformat_alloc_context();
    fmt->interrupt_callback.callback = &VideoDecoder::interruptCallback;
    fmt->interrupt_callback.opaque = this;
    const QByteArray utf8 = path.toUtf8();
    if (avformat_open_input(&fmt, utf8.constData(), nullptr, nullptr) < 0) {
        qWarning() << "VideoDecoder: could not open" << path;
        return false;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    int index = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVStream *s = fmt->streams[i];
        if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
            && !(s->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            index = int(i);
            break;
        }
    }
    if (index < 0 || !avcodec_find_decoder(fmt->streams[index]->codecpar->codec_id)) {
        avformat_close_input(&fmt);
        return false;
    }

    m_fmt = fmt;
    m_streamIndex = index;
    const AVStream *stream = fmt->streams[index];
    m_codedSize = QSize(stream->codecpar->width, stream->codecpar->height);
    m_rotation = displayRotationOf(stream);

    if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0)
        m_durationUs = av_rescale_q(stream->duration, stream->time_base, {1, 1000000});
    else if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
        m_durationUs = fmt->duration;
    else
        m_durationUs = 0;

    double fps = 0.0;
    if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0)
        fps = av_q2d(stream->avg_frame_rate);
    else if (stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0)
        fps = av_q2d(stream->r_frame_rate);
    if (fps > 0.0 && fps < 1000.0)
        m_frameDurationUs = int64_t(std::llround(1e6 / fps));
    // About 300 ms decoded ahead, bounded so 4K software frames don't pile up.
    m_queueDepth = qBound(3, int(std::lround((fps > 0.0 ? fps : 30.0) * 0.3)), 8);

    m_worker = std::thread(&VideoDecoder::workerLoop, this);
    return true;
}

void VideoDecoder::close()
{
    if (m_worker.joinable()) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_wake.notify_all();
        m_worker.join();
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
        m_seekPending = false;
        m_reopenPending = false;
        m_eof = false;
        m_failed = false;
        m_backendName.clear();
        ++m_generation;
    }
    if (m_fmt)
        avformat_close_input(&m_fmt);
    m_streamIndex = -1;
    m_codedSize = {};
    m_rotation = 0;
    m_durationUs = 0;
}

void VideoDecoder::seek(double seconds)
{
    if (!m_fmt)
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
        m_seekPending = true;
        m_seekTargetUs = int64_t(qMax(0.0, seconds) * 1e6);
        m_eof = false;
        ++m_generation;
    }
    m_wake.notify_all();
}

void VideoDecoder::reopen(double seconds)
{
    if (!m_fmt)
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
        m_reopenPending = true;
        m_seekPending = true;
        m_seekTargetUs = int64_t(qMax(0.0, seconds) * 1e6);
        m_eof = false;
        m_failed = false;
        ++m_generation;
    }
    m_wake.notify_all();
}

bool VideoDecoder::popNext(DecodedVideoFrame &out, int waitMs)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_queue.empty() && waitMs > 0) {
        m_produced.wait_for(lock, std::chrono::milliseconds(waitMs), [this] {
            return !m_queue.empty() || m_eof || m_failed || m_stop;
        });
    }
    if (m_queue.empty())
        return false;
    out = std::move(m_queue.front());
    m_queue.pop_front();
    lock.unlock();
    m_wake.notify_all();
    return true;
}

bool VideoDecoder::popUpTo(double seconds, DecodedVideoFrame &out)
{
    // A millisecond of slack so a frame whose pts equals the clock is not held back by rounding.
    const int64_t limitUs = int64_t(seconds * 1e6) + 1000;
    std::unique_lock<std::mutex> lock(m_mutex);
    bool got = false;
    while (!m_queue.empty() && m_queue.front().ptsUs <= limitUs) {
        out = std::move(m_queue.front());
        m_queue.pop_front();
        got = true;
    }
    lock.unlock();
    if (got)
        m_wake.notify_all();
    return got;
}

bool VideoDecoder::drained() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.empty() && (m_eof || m_failed) && !m_seekPending;
}

QString VideoDecoder::activeBackendName() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_backendName;
}

void VideoDecoder::setMode(Mode mode, hwaccel::Backend backend)
{
    g_mode.store(static_cast<int>(mode), std::memory_order_relaxed);
    g_pinnedBackend.store(static_cast<int>(backend), std::memory_order_relaxed);
}

VideoDecoder::Mode VideoDecoder::mode()
{
    return static_cast<Mode>(g_mode.load(std::memory_order_relaxed));
}

hwaccel::Backend VideoDecoder::pinnedBackend()
{
    return static_cast<hwaccel::Backend>(g_pinnedBackend.load(std::memory_order_relaxed));
}

QString VideoDecoder::modeSetting()
{
    return QSettings().value(QStringLiteral("playback/decodeMode"), QStringLiteral("auto")).toString();
}

void VideoDecoder::storeModeSetting(const QString &value)
{
    QSettings().setValue(QStringLiteral("playback/decodeMode"), value);
}

void VideoDecoder::applyModeSetting(const QString &value)
{
    if (value == QLatin1String("software")) {
        setMode(Mode::Software, hwaccel::Backend::None);
    } else if (value.startsWith(QLatin1String("hw:"))) {
        const hwaccel::Backend backend = hwaccel::backendFromId(value.mid(3));
        setMode(backend == hwaccel::Backend::None ? Mode::Auto : Mode::Hardware, backend);
    } else {
        setMode(Mode::Auto, hwaccel::Backend::None);
    }
}

quint64 VideoDecoder::hardwareFallbackCount()
{
    return g_hwFallbackCount.load(std::memory_order_relaxed);
}

QString VideoDecoder::lastHardwareFailure()
{
    QMutexLocker lock(&g_hwFailureMutex);
    return g_lastHwFailure;
}

// ── Worker ────────────────────────────────────────────────────────────────────

void VideoDecoder::workerLoop()
{
    m_packet = av_packet_alloc();
    AVFrame *decoded = av_frame_alloc();

    const auto publishDecoderState = [this](bool ok) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failed = !ok;
        m_backendName = !ok ? QString()
                      : m_hwActive ? QString::fromLatin1(hwaccel::name(m_hwBackend))
                                   : QStringLiteral("Software");
    };
    publishDecoderState(openDecoder());
    m_produced.notify_all();

    // Frames that end before this are decoded but not delivered: exact seeks, and not repeating
    // frames after a hardware fallback resumes from the previous keyframe.
    int64_t dropBeforeUs = INT64_MIN;
    int64_t lastDeliveredUs = INT64_MIN;
    // After a hardware failure: reopen in software and pick up where delivery left off.
    const auto resumeInSoftware = [&](const QString &why) {
        if (!fallbackToSoftware(why))
            return false;
        if (lastDeliveredUs != INT64_MIN) {
            seekInternal(lastDeliveredUs);
            dropBeforeUs = lastDeliveredUs + m_frameDurationUs;
        } else {
            seekInternal(qMax<int64_t>(0, dropBeforeUs));
        }
        return true;
    };

    while (!m_stop) {
        uint64_t generation = 0;
        bool reopen = false;
        bool doSeek = false;
        int64_t seekTo = 0;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] {
                return m_stop || m_seekPending || m_reopenPending
                    || (!m_eof && !m_failed && int(m_queue.size()) < m_queueDepth);
            });
            if (m_stop)
                break;
            reopen = m_reopenPending;
            m_reopenPending = false;
            doSeek = m_seekPending;
            m_seekPending = false;
            seekTo = m_seekTargetUs;
            generation = m_generation;
        }

        if (reopen) {
            teardownDecoder();
            m_hwDisabled = false;
            m_hwScalerFailed = false;
            publishDecoderState(openDecoder());
            if (!m_ctx) {
                m_produced.notify_all();
                continue;
            }
        }
        if (doSeek) {
            seekInternal(seekTo);
            dropBeforeUs = seekTo;
            lastDeliveredUs = INT64_MIN;
        }

        const DecodeResult result = decodeOne(decoded);
        if (result == DecodeResult::Again)
            continue;
        if (result == DecodeResult::Error && m_hwActive
            && resumeInSoftware(QStringLiteral("decode failed")))
            continue;
        if (result != DecodeResult::Frame) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (generation == m_generation)
                    m_eof = true;
            }
            m_produced.notify_all();
            continue;
        }

        const int64_t pts = ptsToUs(decoded);
        m_lastPtsUs = pts;
        if (pts + m_frameDurationUs <= dropBeforeUs) {
            av_frame_unref(decoded);
            continue;
        }

        AVFrame *presentable = toPresentable(decoded);
        av_frame_unref(decoded);
        if (!presentable) {
            if (m_hwActive)
                resumeInSoftware(QStringLiteral("surface readback failed"));
            continue;
        }

        DecodedVideoFrame item;
        item.frame.reset(presentable, freeFrame);
        item.ptsUs = pts;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (generation != m_generation)
                continue;
            m_queue.push_back(std::move(item));
        }
        lastDeliveredUs = pts;
        m_produced.notify_all();
    }

    av_frame_free(&decoded);
    teardownDecoder();
    av_packet_free(&m_packet);
}

bool VideoDecoder::openDecoder()
{
    if (tryOpenHardwareDecoder())
        return true;
    return openSoftwareDecoder();
}

bool VideoDecoder::openSoftwareDecoder()
{
    const AVCodecParameters *par = m_fmt->streams[m_streamIndex]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec)
        return false;
    m_ctx = avcodec_alloc_context3(codec);
    if (!m_ctx || avcodec_parameters_to_context(m_ctx, par) < 0) {
        avcodec_free_context(&m_ctx);
        return false;
    }
    m_ctx->thread_count = 0;
    m_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (avcodec_open2(m_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&m_ctx);
        return false;
    }
    m_hwActive = false;
    m_hwBackend = hwaccel::Backend::None;
    return true;
}

bool VideoDecoder::hardwareDecodeIsWorthIt() const
{
    const AVStream *stream = m_fmt->streams[m_streamIndex];
    const AVCodecParameters *par = stream->codecpar;

    if (int64_t(par->width) * par->height >= 3840LL * 2160)
        return true;
    // dav1d spends several times the CPU per pixel of H.264, so AV1 skips the floors below.
    if (par->codec_id == AV_CODEC_ID_AV1)
        return true;

    const AVRational rate = stream->avg_frame_rate;
    const double fps = (rate.num > 0 && rate.den > 0) ? av_q2d(rate) : 0.0;
    if (fps > 0.0 && double(par->width) * par->height * fps >= kHwAccelMinPixelsPerSecond)
        return true;

    int64_t bitRate = par->bit_rate;
    if (bitRate <= 0)
        bitRate = m_fmt->bit_rate; // Matroska usually omits the per-stream value
    if (bitRate <= 0 || fps <= 0.0)
        return true;
    return (double(bitRate) / fps / 1000.0) >= kHwAccelMinKbitPerFrame;
}

bool VideoDecoder::openHardwareDecoderWith(hwaccel::Backend backend)
{
    const AVHWDeviceType type = hwaccel::deviceType(backend);
    if (!hwaccel::deviceAvailable(type))
        return false;

    const AVCodecParameters *par = m_fmt->streams[m_streamIndex]->codecpar;
    AVPixelFormat pixFmt = AV_PIX_FMT_NONE;
    const AVCodec *codec = hwaccel::findDecoder(par->codec_id, type, &pixFmt);
    if (!codec)
        return false;

    installRecordingLogCallback();
    if (type == AV_HWDEVICE_TYPE_CUDA) {
        m_hwDevice = sharedCudaDevice();
        if (!m_hwDevice)
            return false;
    } else {
        const QByteArray device = hwaccel::deviceString(type);
        if (av_hwdevice_ctx_create(&m_hwDevice, type,
                                   device.isEmpty() ? nullptr : device.constData(), nullptr, 0)
            < 0) {
            av_buffer_unref(&m_hwDevice);
            return false;
        }
    }

    m_ctx = avcodec_alloc_context3(codec);
    if (!m_ctx || avcodec_parameters_to_context(m_ctx, par) < 0) {
        avcodec_free_context(&m_ctx);
        av_buffer_unref(&m_hwDevice);
        return false;
    }

    m_ctx->hw_device_ctx = av_buffer_ref(m_hwDevice);
    m_hwRequest = {};
    m_hwRequest.pixFmt = pixFmt;
#if defined(Q_OS_WIN)
    // NVDEC on Windows will not create a decoder with more than 32 surfaces.
    if (backend == hwaccel::Backend::Cuda)
        m_hwRequest.maxSurfaces = 32;
#endif
    m_ctx->opaque = &m_hwRequest;
    m_ctx->get_format = hwGetFormat;
    // The queue, the frame on screen and the scaler all hold surfaces; without spare pool slots
    // the decoder stalls once those refs are outstanding.
    m_ctx->extra_hw_frames = m_queueDepth + 4;

    if (avcodec_open2(m_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&m_ctx);
        av_buffer_unref(&m_hwDevice);
        return false;
    }

    m_hwBackend = backend;
    m_hwActive = true;
    {
        // Whatever was logged before a decoder that opened cleanly is not why a later one fails.
        QMutexLocker lock(&g_hwFailureMutex);
        g_lastFfmpegError.clear();
    }
    return true;
}

bool VideoDecoder::tryOpenHardwareDecoder()
{
    if (m_hwDisabled || hwaccel::disabledByEnv())
        return false;

    const Mode decodeMode = mode();
    if (decodeMode == Mode::Software)
        return false;
    if (decodeMode == Mode::Auto && !hardwareDecodeIsWorthIt())
        return false;

    // An explicit pick is honoured on its own: falling back to a backend the user did not choose
    // would hide exactly the problem they picked around.
    const hwaccel::Backend pinned = pinnedBackend();
    const bool pinnedOnly = decodeMode == Mode::Hardware && pinned != hwaccel::Backend::None;
    for (const hwaccel::Backend backend :
         hwaccel::decodeAttemptOrder(pinned, pinnedOnly, hwaccel::renderVendor())) {
        if (openHardwareDecoderWith(backend))
            return true;
    }
    // Nothing here takes this stream. Sticky so a reopen does not re-walk the codec list.
    m_hwDisabled = true;
    return false;
}

void VideoDecoder::teardownDecoder()
{
    teardownHwScaler();
    if (m_sws) {
        sws_freeContext(m_sws);
        m_sws = nullptr;
    }
    avcodec_free_context(&m_ctx);
    av_buffer_unref(&m_hwDevice);
    m_hwActive = false;
    m_hwBackend = hwaccel::Backend::None;
    m_hwRequest = {};
}

void VideoDecoder::recordHardwareFailure(const QString &what)
{
    const QString codec = QString::fromUtf8(m_ctx && m_ctx->codec ? m_ctx->codec->name : "?");
    QString text = QStringLiteral("%1 %2: %3")
                       .arg(QString::fromLatin1(hwaccel::name(m_hwBackend)), codec, what);
    {
        QMutexLocker lock(&g_hwFailureMutex);
        if (!g_lastFfmpegError.isEmpty())
            text += QStringLiteral(" (FFmpeg: %1)").arg(g_lastFfmpegError);
        g_lastHwFailure = text;
    }
    qWarning("VideoDecoder: hardware decode failed, falling back to software: %s",
             qUtf8Printable(text));
}

bool VideoDecoder::fallbackToSoftware(const QString &why)
{
    recordHardwareFailure(why);
    g_hwFallbackCount.fetch_add(1, std::memory_order_relaxed);
    teardownDecoder();
    m_hwDisabled = true;
    const bool ok = openSoftwareDecoder();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_backendName = ok ? QStringLiteral("Software") : QString();
    m_failed = !ok;
    return ok;
}

void VideoDecoder::seekInternal(int64_t targetUs)
{
    if (!m_ctx)
        return;
    const AVStream *stream = m_fmt->streams[m_streamIndex];
    const int64_t start = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    const int64_t ts = start + av_rescale_q(targetUs, {1, 1000000}, stream->time_base);
    if (av_seek_frame(m_fmt, m_streamIndex, ts, AVSEEK_FLAG_BACKWARD) < 0)
        av_seek_frame(m_fmt, m_streamIndex, ts, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
    avcodec_flush_buffers(m_ctx);
    m_inputEnded = false;
}

VideoDecoder::DecodeResult VideoDecoder::decodeOne(AVFrame *out)
{
    if (!m_ctx)
        return DecodeResult::End;
    while (!m_stop) {
        int rc = avcodec_receive_frame(m_ctx, out);
        if (rc == 0)
            return DecodeResult::Frame;
        if (rc == AVERROR_EOF)
            return DecodeResult::End;
        if (rc != AVERROR(EAGAIN))
            return DecodeResult::Error;

        rc = av_read_frame(m_fmt, m_packet);
        if (rc < 0) {
            if (m_inputEnded)
                return DecodeResult::End;
            // End of input or a read error: drain what the decoder still holds.
            m_inputEnded = true;
            avcodec_send_packet(m_ctx, nullptr);
            continue;
        }
        if (m_packet->stream_index != m_streamIndex) {
            av_packet_unref(m_packet);
            continue;
        }
        rc = avcodec_send_packet(m_ctx, m_packet);
        av_packet_unref(m_packet);
        // A corrupt packet is skipped in software, as players do. In hardware it is the signal
        // that the hwaccel cannot take this stream.
        if (rc < 0 && rc != AVERROR(EAGAIN) && (m_hwActive || rc != AVERROR_INVALIDDATA)) {
            if (!m_hwActive)
                qWarning("VideoDecoder: send_packet failed: %s", qUtf8Printable(avErrorText(rc)));
            return DecodeResult::Error;
        }
    }
    return DecodeResult::End;
}

int64_t VideoDecoder::ptsToUs(const AVFrame *frame) const
{
    const AVStream *stream = m_fmt->streams[m_streamIndex];
    int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp
                                                                 : frame->pts;
    if (pts == AV_NOPTS_VALUE)
        return m_lastPtsUs + m_frameDurationUs;
    if (stream->start_time != AV_NOPTS_VALUE)
        pts -= stream->start_time;
    return av_rescale_q(pts, stream->time_base, {1, 1000000});
}

void VideoDecoder::teardownHwScaler()
{
    if (m_vppGraph)
        avfilter_graph_free(&m_vppGraph);
    m_vppSrc = nullptr;
    m_vppSink = nullptr;
    av_buffer_unref(&m_vppFramesCtx);
}

// 10-bit surfaces (P010) → NV12 on the GPU, since every import path and the convert shader take
// NV12. Returns an owned frame, or nullptr when this backend has no scaler or it fails.
AVFrame *VideoDecoder::scaleHwToNv12(const AVFrame *hwFrame)
{
    if (m_hwScalerFailed || !hwFrame->hw_frames_ctx)
        return nullptr;
    const char *scalerName = hwaccel::scaleFilter(m_hwBackend);
    if (!scalerName) {
        m_hwScalerFailed = true;
        return nullptr;
    }

    if (!m_vppGraph || !m_vppFramesCtx || m_vppFramesCtx->data != hwFrame->hw_frames_ctx->data) {
        teardownHwScaler();
        const AVFilter *bufferFilter = avfilter_get_by_name("buffer");
        const AVFilter *sinkFilter = avfilter_get_by_name("buffersink");
        const AVFilter *scaleFilter = avfilter_get_by_name(scalerName);
        m_vppGraph = avfilter_graph_alloc();
        bool ok = bufferFilter && sinkFilter && scaleFilter && m_vppGraph;
        if (ok) {
            m_vppSrc = avfilter_graph_alloc_filter(m_vppGraph, bufferFilter, "in");
            AVBufferSrcParameters *params = av_buffersrc_parameters_alloc();
            ok = m_vppSrc && params;
            if (ok) {
                params->format = hwFrame->format;
                params->width = hwFrame->width;
                params->height = hwFrame->height;
                params->time_base = m_fmt->streams[m_streamIndex]->time_base;
                params->hw_frames_ctx = hwFrame->hw_frames_ctx;
                ok = av_buffersrc_parameters_set(m_vppSrc, params) >= 0
                    && avfilter_init_str(m_vppSrc, nullptr) >= 0;
            }
            av_free(params);
        }
        AVFilterContext *scale = nullptr;
        if (ok) {
            ok = avfilter_graph_create_filter(&scale, scaleFilter, "vpp", "format=nv12", nullptr,
                                              m_vppGraph) >= 0
                && avfilter_graph_create_filter(&m_vppSink, sinkFilter, "out", nullptr, nullptr,
                                                m_vppGraph) >= 0
                && avfilter_link(m_vppSrc, 0, scale, 0) >= 0
                && avfilter_link(scale, 0, m_vppSink, 0) >= 0
                && avfilter_graph_config(m_vppGraph, nullptr) >= 0;
        }
        if (!ok) {
            teardownHwScaler();
            m_hwScalerFailed = true;
            return nullptr;
        }
        m_vppFramesCtx = av_buffer_ref(hwFrame->hw_frames_ctx);
    }

    AVFrame *scaled = av_frame_alloc();
    if (av_buffersrc_add_frame_flags(m_vppSrc, const_cast<AVFrame *>(hwFrame),
                                     AV_BUFFERSRC_FLAG_KEEP_REF) < 0
        || av_buffersink_get_frame(m_vppSink, scaled) < 0) {
        av_frame_free(&scaled);
        teardownHwScaler();
        m_hwScalerFailed = true;
        return nullptr;
    }
    return scaled;
}

AVFrame *VideoDecoder::toPresentable(AVFrame *decoded)
{
    AVFrame *owned = nullptr; // frame that `src` points at when it is ours to free
    const AVFrame *src = decoded;

    if (isHardwarePixelFormat(static_cast<AVPixelFormat>(decoded->format))) {
        const auto *fc = decoded->hw_frames_ctx
            ? reinterpret_cast<const AVHWFramesContext *>(decoded->hw_frames_ctx->data)
            : nullptr;
        if (fc && fc->sw_format != AV_PIX_FMT_NV12) {
            if (AVFrame *scaled = scaleHwToNv12(decoded)) {
                av_frame_copy_props(scaled, decoded);
                owned = scaled;
                src = scaled;
            }
        }
        if (prism::GpuVideoUploader::mayImport(src))
            return owned ? owned : av_frame_clone(src);

        // No zero-copy path takes this surface: read it back here, off the GUI thread.
        AVFrame *sw = av_frame_alloc();
        const int rc = av_hwframe_transfer_data(sw, src, 0);
        if (rc < 0) {
            av_frame_free(&sw);
            av_frame_free(&owned);
            return nullptr;
        }
        av_frame_copy_props(sw, decoded);
        av_frame_free(&owned);
        owned = sw;
        src = sw;
    }

    if (src->format == AV_PIX_FMT_NV12)
        return owned ? owned : av_frame_clone(src);

    // Software frames of any other layout → NV12 at the same size. Even dimensions only: the
    // chroma plane is half size and the importer rejects odd sizes.
    const int w = src->width & ~1;
    const int h = src->height & ~1;
    if (w < 2 || h < 2) {
        av_frame_free(&owned);
        return nullptr;
    }
    m_sws = sws_getCachedContext(m_sws, src->width, src->height,
                                 static_cast<AVPixelFormat>(src->format), w, h, AV_PIX_FMT_NV12,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws) {
        av_frame_free(&owned);
        return nullptr;
    }
    const int range = isFullRange(src) ? 1 : 0;
    const int *coeff = sws_getCoefficients(swsColorspaceFromFrame(src));
    sws_setColorspaceDetails(m_sws, coeff, range, coeff, range, 0, 1 << 16, 1 << 16);

    AVFrame *nv12 = av_frame_alloc();
    nv12->format = AV_PIX_FMT_NV12;
    nv12->width = w;
    nv12->height = h;
    if (av_frame_get_buffer(nv12, 0) < 0) {
        av_frame_free(&nv12);
        av_frame_free(&owned);
        return nullptr;
    }
    sws_scale(m_sws, src->data, src->linesize, 0, src->height, nv12->data, nv12->linesize);
    av_frame_copy_props(nv12, src);
    nv12->color_range = range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    av_frame_free(&owned);
    return nv12;
}
