#pragma once

#include "core/media/HwAccel.h"

#include <QSize>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

extern "C" {
#include <libavutil/pixfmt.h>
}

struct AVBufferRef;
struct AVCodecContext;
struct AVFilterContext;
struct AVFilterGraph;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

// A decoded frame ready for GpuVideoUploader: a hardware surface it can import, or software NV12.
struct DecodedVideoFrame {
    std::shared_ptr<AVFrame> frame;
    int64_t ptsUs = 0; // relative to the stream's start_time
};

// FFmpeg video decoding on a worker thread.
//
// open() probes on the caller's thread so size, rotation and duration are known at once; the
// decoder itself (hardware device creation can take a while) opens on the worker, which then keeps
// a short queue of frames decoded ahead. seek() is asynchronous and exact: the worker seeks to the
// preceding keyframe and decodes forward, dropping frames that end before the target.
//
// Hardware decode follows Drift's ClipReader: backends are tried in hwaccel order, Auto only uses
// them where they beat software, and any hardware error drops this decoder to software for good.
class VideoDecoder {
public:
    enum class Mode { Auto, Software, Hardware };

    VideoDecoder();
    ~VideoDecoder();

    bool open(const QString &path);
    void close();
    bool isOpen() const { return m_fmt != nullptr; }

    QSize codedSize() const { return m_codedSize; }
    int rotation() const { return m_rotation; }
    double duration() const { return m_durationUs / 1e6; }

    // Flushes the queue and restarts decoding from `seconds`.
    void seek(double seconds);

    // The next frame in decode order. Waits up to waitMs when none is queued yet.
    bool popNext(DecodedVideoFrame &out, int waitMs);

    // The latest queued frame with pts <= seconds, dropping any older ones. Never waits. Returns
    // false when nothing is due yet.
    bool popUpTo(double seconds, DecodedVideoFrame &out);

    // True once the worker has hit the end of the stream and everything queued was taken.
    bool drained() const;

    // Reopens the decoder under the current global mode, resuming from `seconds`.
    void reopen(double seconds);

    QString activeBackendName() const;

    // Process-wide decode preference. Hardware pins `backend` when it is not None.
    static void setMode(Mode mode, prism::hwaccel::Backend backend);
    static Mode mode();
    static prism::hwaccel::Backend pinnedBackend();

    // QSettings "playback/decodeMode": "auto", "software" or "hw:<backend id>".
    static QString modeSetting();
    static void storeModeSetting(const QString &value);
    // Parses a setting value into setMode(). Unknown values mean Auto.
    static void applyModeSetting(const QString &value);

    // Hardware decoders that fell back to software since startup, and why the last one did.
    static quint64 hardwareFallbackCount();
    static QString lastHardwareFailure();

    // Opaque for get_format; see VideoDecoder.cpp.
    struct HwFormatRequest {
        AVPixelFormat pixFmt = AV_PIX_FMT_NONE;
        int maxSurfaces = 0;
    };

private:
    enum class DecodeResult { Frame, Again, End, Error };

    static int interruptCallback(void *opaque);
    void workerLoop();
    bool openDecoder();
    bool openSoftwareDecoder();
    bool openHardwareDecoderWith(prism::hwaccel::Backend backend);
    bool tryOpenHardwareDecoder();
    bool hardwareDecodeIsWorthIt() const;
    void teardownDecoder();
    bool fallbackToSoftware(const QString &why);
    void recordHardwareFailure(const QString &what);
    void seekInternal(int64_t targetUs);
    DecodeResult decodeOne(AVFrame *out);
    AVFrame *toPresentable(AVFrame *decoded);
    AVFrame *scaleHwToNv12(const AVFrame *hwFrame);
    void teardownHwScaler();
    int64_t ptsToUs(const AVFrame *frame) const;

    // Set once in open(), read-only afterwards.
    AVFormatContext *m_fmt = nullptr;
    int m_streamIndex = -1;
    QSize m_codedSize;
    int m_rotation = 0;
    int64_t m_durationUs = 0;
    int64_t m_frameDurationUs = 33333;
    int m_queueDepth = 4;

    // Worker-thread state.
    AVCodecContext *m_ctx = nullptr;
    AVPacket *m_packet = nullptr;
    AVBufferRef *m_hwDevice = nullptr;
    HwFormatRequest m_hwRequest;
    prism::hwaccel::Backend m_hwBackend = prism::hwaccel::Backend::None;
    bool m_hwActive = false;
    bool m_hwDisabled = false;
    bool m_inputEnded = false;
    int64_t m_lastPtsUs = 0;
    SwsContext *m_sws = nullptr;
    AVFilterGraph *m_vppGraph = nullptr;
    AVFilterContext *m_vppSrc = nullptr;
    AVFilterContext *m_vppSink = nullptr;
    AVBufferRef *m_vppFramesCtx = nullptr;
    bool m_hwScalerFailed = false;

    // Shared between the worker and the caller, under m_mutex.
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;     // worker waits on this
    std::condition_variable m_produced; // popNext waits on this
    std::deque<DecodedVideoFrame> m_queue;
    uint64_t m_generation = 0;
    bool m_seekPending = false;
    int64_t m_seekTargetUs = 0;
    bool m_reopenPending = false;
    bool m_eof = false;
    bool m_failed = false;
    QString m_backendName;
    std::atomic<bool> m_stop{false};
    std::thread m_worker;
};
