#include "core/media/GpuVideoUploader.h"

#include "core/media/HwAccel.h"
#if defined(Q_OS_WIN)
#include "core/media/D3d11GlInterop.h"
#else
namespace prism::gl { class D3d11GlInterop {}; }
#endif

#include <QMutex>
#include <QMutexLocker>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QScopeGuard>
#include <QSettings>
#include <QSurfaceFormat>
#include <QMatrix3x3>
#include <QVector3D>

#include <atomic>
#include <cstring>
#include <mutex>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif !defined(Q_OS_MACOS)
#include <dlfcn.h>
#endif

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
#define PRISM_VAAPI_IMPORT 1
#include <unistd.h>
#endif

#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif

namespace prism {

ZeroCopyMode zeroCopyMode()
{
    // Env is read live; QSettings is what must not run per frame.
    if (qEnvironmentVariableIsSet("PRISM_ZEROCOPY"))
        return qgetenv("PRISM_ZEROCOPY") != "0" ? ZeroCopyMode::On : ZeroCopyMode::Off;
    static ZeroCopyMode mode = ZeroCopyMode::Auto;
    static std::once_flag once;
    std::call_once(once, [] {
        const QString value = QSettings().value(QStringLiteral("playback/zeroCopy")).toString();
        if (value == QLatin1String("on"))
            mode = ZeroCopyMode::On;
        else if (value == QLatin1String("off"))
            mode = ZeroCopyMode::Off;
    });
    return mode;
}

void applyZeroCopyXcbEgl()
{
#if defined(PRISM_VAAPI_IMPORT)
    // Only on an explicit request: this changes how every window renders, and there is no way
    // to fall back from it once the app is up.
    if (zeroCopyMode() != ZeroCopyMode::On)
        return;
    if (!qEnvironmentVariableIsEmpty("QT_XCB_GL_INTEGRATION"))
        return;
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
#endif
}

namespace {

// Sticky per-process verdicts, readable from decoder threads through mayImport().
std::atomic<bool> g_cudaImportOk{true};
std::atomic<bool> g_vaapiImportOk{true};
std::atomic<bool> g_d3d11ImportOk{true};

constexpr const char *kNv12VertexShader = R"(#version 120
attribute vec2 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;
void main() {
    v_texCoord = a_texCoord;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

// YUV -> RGBA with caller-supplied matrix, range and a UV affine for display rotation.
constexpr const char *kNv12FragShader = R"(#version 120
varying vec2 v_texCoord;
uniform sampler2D u_y;
uniform sampler2D u_uv;
uniform mat3 u_yuvToRgb;
uniform vec3 u_yuvOffset;
uniform vec3 u_yuvScale;
uniform mat3 u_texMap;
void main() {
    vec2 src = (u_texMap * vec3(v_texCoord, 1.0)).xy;
    float y = texture2D(u_y, src).r;
    vec2 chroma = texture2D(u_uv, src).rg;
    vec3 yuv = (vec3(y, chroma) - u_yuvOffset) * u_yuvScale;
    vec3 rgb = u_yuvToRgb * yuv;
    gl_FragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)";

// Triangle strip with v=0 at the NDC bottom. Planes are uploaded top row first, so the FBO's
// row 0 ends up holding the picture's top row — the same layout a glTexImage2D of a QImage
// gives, which is what VideoWidget's texture paths assume.
constexpr float kQuad[] = {
    -1.f, -1.f, 0.f, 0.f,
     1.f, -1.f, 1.f, 0.f,
    -1.f,  1.f, 0.f, 1.f,
     1.f,  1.f, 1.f, 1.f,
};

QMatrix3x3 yuvToRgbMatrix(int colorspace)
{
    // Row-major, multiplies vec3(Y, Cb, Cr) after range expansion.
    float m[9] = {1.f, 0.f, 1.5748f, 1.f, -0.1873f, -0.4681f, 1.f, 1.8556f, 0.f};
    switch (colorspace) {
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        m[2] = 1.402f;
        m[4] = -0.344f;
        m[5] = -0.714f;
        m[7] = 1.772f;
        break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        m[2] = 1.4746f;
        m[4] = -0.1646f;
        m[5] = -0.5714f;
        m[7] = 1.8814f;
        break;
    default:
        break;
    }
    return QMatrix3x3(m);
}

QMatrix3x3 texMapForRotation(int rotation)
{
    // codedUV = (mat * vec3(displayUV, 1)).xy. 90/270 are clockwise, matching Qt.
    float m[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
    if (rotation == 90) {
        const float r[9] = {0.f, 1.f, 0.f, -1.f, 0.f, 1.f, 0.f, 0.f, 1.f};
        memcpy(m, r, sizeof(m));
    } else if (rotation == 180) {
        const float r[9] = {-1.f, 0.f, 1.f, 0.f, -1.f, 1.f, 0.f, 0.f, 1.f};
        memcpy(m, r, sizeof(m));
    } else if (rotation == 270) {
        const float r[9] = {0.f, -1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
        memcpy(m, r, sizeof(m));
    }
    return QMatrix3x3(m);
}

void yuvRangeUniforms(int colorRange, QVector3D *offset, QVector3D *scale)
{
    if (colorRange == AVCOL_RANGE_JPEG) {
        *offset = QVector3D(0.f, 128.f / 255.f, 128.f / 255.f);
        *scale = QVector3D(1.f, 1.f, 1.f);
        return;
    }
    *offset = QVector3D(16.f / 255.f, 128.f / 255.f, 128.f / 255.f);
    *scale = QVector3D(255.f / 219.f, 255.f / 224.f, 255.f / 224.f);
}

bool isHwPixelFormat(AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    return desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL);
}

AVPixelFormat hwSwFormat(const AVFrame *frame)
{
    if (!frame || !frame->hw_frames_ctx)
        return AV_PIX_FMT_NONE;
    const auto *fc = reinterpret_cast<const AVHWFramesContext *>(frame->hw_frames_ctx->data);
    return fc ? fc->sw_format : AV_PIX_FMT_NONE;
}

#if !defined(Q_OS_MACOS)
using CUresult = int;
using CUdeviceptr = void *;
using CUarray = void *;
using CUcontext = void *;
using CUstream = void *;
using CUgraphicsResource = void *;

enum { kCuSuccess = 0, kCuMemoryDevice = 2, kCuMemoryArray = 3, kCuRegisterWriteDiscard = 0x02 };

// CUDA_MEMCPY2D as the *_v2 entry points expect it. The unversioned cuMemcpy2D symbol that
// libcuda still exports is the v1 ABI, whose equivalent fields are unsigned int rather than
// size_t — a completely different layout on 64-bit. cuda.h hides this behind
// `#define cuMemcpy2D cuMemcpy2D_v2`, so source that includes it gets v2 automatically and
// only a dlsym of the bare name lands on v1. Load the _v2 names below to match this struct.
struct CudaMemcpy2D
{
    size_t srcXInBytes = 0;
    size_t srcY = 0;
    int srcMemoryType = 0;
    int srcPad = 0;
    const void *srcHost = nullptr;
    CUdeviceptr srcDevice = nullptr;
    CUarray srcArray = nullptr;
    size_t srcPitch = 0;
    size_t dstXInBytes = 0;
    size_t dstY = 0;
    int dstMemoryType = 0;
    int dstPad = 0;
    void *dstHost = nullptr;
    CUdeviceptr dstDevice = nullptr;
    CUarray dstArray = nullptr;
    size_t dstPitch = 0;
    size_t WidthInBytes = 0;
    size_t Height = 0;
};

struct CudaGlApi
{
    void *lib = nullptr;
    CUresult (*cuInit)(unsigned int) = nullptr;
    CUresult (*cuCtxPushCurrent)(CUcontext) = nullptr;
    CUresult (*cuCtxPopCurrent)(CUcontext *) = nullptr;
    CUresult (*cuGraphicsGLRegisterImage)(CUgraphicsResource *, unsigned int, unsigned int,
                                          unsigned int) = nullptr;
    CUresult (*cuGraphicsUnregisterResource)(CUgraphicsResource) = nullptr;
    CUresult (*cuGraphicsMapResources)(unsigned int, CUgraphicsResource *, CUstream) = nullptr;
    CUresult (*cuGraphicsUnmapResources)(unsigned int, CUgraphicsResource *, CUstream) = nullptr;
    CUresult (*cuGraphicsSubResourceGetMappedArray)(CUarray *, CUgraphicsResource, unsigned int,
                                                    unsigned int) = nullptr;
    CUresult (*cuMemcpy2DAsync)(const CudaMemcpy2D *, CUstream) = nullptr;
    CUresult (*cuStreamSynchronize)(CUstream) = nullptr;
    bool ok = false;
};

CudaGlApi &cudaGlApi()
{
    static CudaGlApi api;
    static std::once_flag once;
    std::call_once(once, [] {
#if defined(Q_OS_WIN)
        api.lib = static_cast<void *>(LoadLibraryW(L"nvcuda.dll"));
        auto sym = [&](const char *name) -> void * {
            return api.lib ? static_cast<void *>(GetProcAddress(static_cast<HMODULE>(api.lib), name))
                           : nullptr;
        };
#else
        api.lib = dlopen("libcuda.so.1", RTLD_LAZY | RTLD_LOCAL);
        auto sym = [&](const char *name) -> void * { return api.lib ? dlsym(api.lib, name) : nullptr; };
#endif
        if (!api.lib)
            return;
#define PRISM_CUDA_SYM(field, name) \
    api.field = reinterpret_cast<decltype(api.field)>(sym(name)); \
    if (!api.field) \
        return;
        // The _v2 suffixes are not optional: libcuda exports both ABIs, and the bare names
        // are the deprecated v1 ones. Anything that takes or returns a struct or a device
        // pointer must use the versioned symbol or it will read the arguments at the wrong
        // offsets. If a driver is old enough to lack them, ok stays false and the preview
        // falls back to the hardware-transfer path.
        PRISM_CUDA_SYM(cuInit, "cuInit");
        PRISM_CUDA_SYM(cuCtxPushCurrent, "cuCtxPushCurrent_v2");
        PRISM_CUDA_SYM(cuCtxPopCurrent, "cuCtxPopCurrent_v2");
        PRISM_CUDA_SYM(cuGraphicsGLRegisterImage, "cuGraphicsGLRegisterImage");
        PRISM_CUDA_SYM(cuGraphicsUnregisterResource, "cuGraphicsUnregisterResource");
        PRISM_CUDA_SYM(cuGraphicsMapResources, "cuGraphicsMapResources");
        PRISM_CUDA_SYM(cuGraphicsUnmapResources, "cuGraphicsUnmapResources");
        PRISM_CUDA_SYM(cuGraphicsSubResourceGetMappedArray, "cuGraphicsSubResourceGetMappedArray");
        PRISM_CUDA_SYM(cuMemcpy2DAsync, "cuMemcpy2DAsync_v2");
        PRISM_CUDA_SYM(cuStreamSynchronize, "cuStreamSynchronize");
#undef PRISM_CUDA_SYM
        if (api.cuInit(0) != kCuSuccess)
            return;
        api.ok = true;
    });
    return api;
}

bool cudaContextOf(const AVFrame *frame, CUcontext *ctx, CUstream *stream)
{
    if (!frame || !frame->hw_frames_ctx)
        return false;
    const auto *fc = reinterpret_cast<const AVHWFramesContext *>(frame->hw_frames_ctx->data);
    if (!fc || !fc->device_ctx || fc->device_ctx->type != AV_HWDEVICE_TYPE_CUDA || !fc->device_ctx->hwctx)
        return false;
    const char *hwctx = static_cast<const char *>(fc->device_ctx->hwctx);
    *ctx = *reinterpret_cast<CUcontext const *>(hwctx);
    *stream = *reinterpret_cast<CUstream const *>(hwctx + sizeof(void *));
    return *ctx != nullptr;
}

bool cudaSwFormatIsNv12(const AVFrame *frame)
{
    if (!frame || !frame->hw_frames_ctx)
        return false;
    const auto *fc = reinterpret_cast<const AVHWFramesContext *>(frame->hw_frames_ctx->data);
    return fc && fc->sw_format == AV_PIX_FMT_NV12;
}

CUresult queueCudaPlaneCopy(CudaGlApi &api, CUstream stream, CUarray dst, CUdeviceptr src,
                            size_t srcPitch, size_t widthBytes, size_t height)
{
    CudaMemcpy2D op{};
    op.srcMemoryType = kCuMemoryDevice;
    op.srcDevice = src;
    op.srcPitch = srcPitch;
    op.dstMemoryType = kCuMemoryArray;
    op.dstArray = dst;
    op.WidthInBytes = widthBytes;
    op.Height = height;
    // On the frame's own stream, not the null stream. FFmpeg creates its decoder stream with
    // CU_STREAM_NON_BLOCKING, which by definition does not synchronise against the legacy
    // null stream — so a copy issued there was unordered with respect to the map and unmap
    // around it, and GL could sample the WRITE_DISCARD textures before the pixels arrived.
    return api.cuMemcpy2DAsync(&op, stream);
}

// Both NV12 planes in one map/unmap pair and one stream wait, rather than a pair each: the
// two copies are independent, and the only thing that has to be true before GL samples is
// that both have landed.
// On failure `why` names the CUDA call that refused and its error code: "failed" alone told a
// Windows bug report nothing about which of the five steps to look at.
bool copyCudaNv12ToTextures(CudaGlApi &api, CUstream stream, CUgraphicsResource yRes,
                            CUgraphicsResource uvRes, const AVFrame *frame, int width, int height,
                            QString *why)
{
    const auto fail = [why](const char *call, CUresult rc) {
        *why = QStringLiteral("%1 returned CUDA error %2").arg(QLatin1String(call)).arg(rc);
        return false;
    };

    CUgraphicsResource resources[2] = {yRes, uvRes};
    CUresult rc = api.cuGraphicsMapResources(2, resources, stream);
    if (rc != kCuSuccess)
        return fail("cuGraphicsMapResources", rc);

    CUarray yArray = nullptr;
    CUarray uvArray = nullptr;
    bool ok = true;
    rc = api.cuGraphicsSubResourceGetMappedArray(&yArray, yRes, 0, 0);
    if (rc == kCuSuccess)
        rc = api.cuGraphicsSubResourceGetMappedArray(&uvArray, uvRes, 0, 0);
    if (rc != kCuSuccess || !yArray || !uvArray)
        ok = fail("cuGraphicsSubResourceGetMappedArray", rc);

    if (ok) {
        // The interleaved UV plane is full-width in bytes over half the rows: width/2 texels
        // of two bytes each.
        rc = queueCudaPlaneCopy(api, stream, yArray, frame->data[0],
                                size_t(qMax(0, frame->linesize[0])), size_t(width), size_t(height));
        if (rc == kCuSuccess)
            rc = queueCudaPlaneCopy(api, stream, uvArray, frame->data[1],
                                    size_t(qMax(0, frame->linesize[1])), size_t(width),
                                    size_t(height / 2));
        if (rc != kCuSuccess)
            ok = fail("cuMemcpy2DAsync", rc);
    }

    api.cuGraphicsUnmapResources(2, resources, stream);

    // Unmap only orders the copies on the stream; it does not wait for them. This does, and
    // it is the sync point that makes the textures safe for the convert shader. One wait on
    // one stream per frame, against a full hardware-transfer download plus PBO upload if this
    // path is not taken.
    if (ok) {
        rc = api.cuStreamSynchronize(stream);
        if (rc != kCuSuccess)
            ok = fail("cuStreamSynchronize", rc);
    }
    if (!ok) {
        *why += QStringLiteral(" (%1x%2, pitch %3/%4, stream %5)")
                    .arg(width)
                    .arg(height)
                    .arg(frame->linesize[0])
                    .arg(frame->linesize[1])
                    .arg(stream ? QStringLiteral("set") : QStringLiteral("null"));
    }
    return ok;
}
#endif

QMutex g_previewImportMutex;
GpuVideoUploader::UploadPath g_previewUploadPath = GpuVideoUploader::UploadPath::None;
QString g_zeroCopyDeclineReason;

void recordPreviewUploadPath(GpuVideoUploader::UploadPath path)
{
    QMutexLocker lock(&g_previewImportMutex);
    g_previewUploadPath = path;
}

void noteZeroCopyDecline(const QString &reason)
{
    QMutexLocker lock(&g_previewImportMutex);
    g_zeroCopyDeclineReason = reason;
}

void logVaapiImportOnce(const QString &reason)
{
    noteZeroCopyDecline(reason);
    static std::once_flag once;
    std::call_once(once, [reason] {
        qWarning("GpuVideoUploader: VAAPI zero-copy import unavailable (%s)", qUtf8Printable(reason));
    });
}

#if defined(PRISM_VAAPI_IMPORT)
// libva and the EGL dma-buf import extension, resolved at runtime for the same reason CUDA is:
// the binary has to start on a host with neither. Only the handful of declarations the import
// needs are reproduced here — a compile-time dependency on va/va.h and EGL/eglext.h would buy
// nothing but a build-time failure on machines that never decode with VAAPI.

using VASurfaceID = unsigned int;
using VAStatus = int;

enum {
    kVaStatusSuccess = 0,
    kVaMemTypeDrmPrime2 = 0x40000000,
    kVaExportReadOnly = 0x0001,
    kVaExportSeparateLayers = 0x0004,
};

// Mirrors VADRMPRIMESurfaceDescriptor from va/va_drmcommon.h. The objects[] entry pads to 16
// bytes (int + uint32_t, then an 8-aligned uint64_t); getting that wrong silently shifts every
// field after it, which is what the sanity check in importVaapiNv12 exists to catch.
struct VaDrmPrimeSurfaceDescriptor
{
    uint32_t fourcc;
    uint32_t width;
    uint32_t height;
    uint32_t num_objects;
    struct
    {
        int fd;
        uint32_t size;
        uint64_t drm_format_modifier;
    } objects[4];
    uint32_t num_layers;
    struct
    {
        uint32_t drm_format;
        uint32_t num_planes;
        uint32_t object_index[4];
        uint32_t offset[4];
        uint32_t pitch[4];
    } layers[4];
};

constexpr uint32_t fourcc(char a, char b, char c, char d)
{
    return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) | (uint32_t(uint8_t(c)) << 16)
        | (uint32_t(uint8_t(d)) << 24);
}

constexpr uint32_t kDrmFormatR8 = fourcc('R', '8', ' ', ' ');
constexpr uint32_t kDrmFormatGr88 = fourcc('G', 'R', '8', '8');
constexpr uint32_t kDrmFormatNv12 = fourcc('N', 'V', '1', '2');
constexpr uint64_t kDrmFormatModInvalid = 0x00ffffffffffffffull;
constexpr uint64_t kDrmFormatModLinear = 0;

using EGLDisplayHandle = void *;
using EGLImageHandle = void *;
using EGLClientBufferHandle = void *;

enum {
    kEglExtensions = 0x3055,
    kEglWidth = 0x3057,
    kEglHeight = 0x3056,
    kEglNone = 0x3038,
    kEglLinuxDmaBufExt = 0x3270,
    kEglLinuxDrmFourccExt = 0x3271,
    kEglDmaBufPlane0FdExt = 0x3272,
    kEglDmaBufPlane0OffsetExt = 0x3273,
    kEglDmaBufPlane0PitchExt = 0x3274,
    kEglDmaBufPlane0ModifierLoExt = 0x3443,
    kEglDmaBufPlane0ModifierHiExt = 0x3444,
};

struct VaEglApi
{
    void *va = nullptr;
    void *egl = nullptr;
    VAStatus (*vaExportSurfaceHandle)(void *, VASurfaceID, uint32_t, uint32_t, void *) = nullptr;
    VAStatus (*vaSyncSurface)(void *, VASurfaceID) = nullptr;
    // Optional: only used to recognise a driver Auto mode trusts, so a libva without it
    // degrades to "not verified" rather than to no zero-copy path at all.
    const char *(*vaQueryVendorString)(void *) = nullptr;
    EGLDisplayHandle (*eglGetCurrentDisplay)() = nullptr;
    const char *(*eglQueryString)(EGLDisplayHandle, int) = nullptr;
    // The KHR entry point takes 32-bit EGLint attributes, not the EGLAttrib (intptr_t) list
    // EGL 1.5's eglCreateImage takes. That is why the modifier arrives as two halves.
    EGLImageHandle (*eglCreateImageKHR)(EGLDisplayHandle, void *, unsigned int, EGLClientBufferHandle,
                                        const int32_t *) = nullptr;
    unsigned int (*eglDestroyImageKHR)(EGLDisplayHandle, EGLImageHandle) = nullptr;
    void (*glEGLImageTargetTexture2DOES)(unsigned int, EGLImageHandle) = nullptr;
    bool ok = false;
};

VaEglApi &vaEglApi()
{
    static VaEglApi api;
    static std::once_flag once;
    std::call_once(once, [] {
        api.va = dlopen("libva.so.2", RTLD_LAZY | RTLD_LOCAL);
        api.egl = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (!api.va || !api.egl)
            return;
#define PRISM_VA_SYM(handle, field, name) \
    api.field = reinterpret_cast<decltype(api.field)>(dlsym(handle, name)); \
    if (!api.field) \
        return;
        api.vaQueryVendorString = reinterpret_cast<decltype(api.vaQueryVendorString)>(
            dlsym(api.va, "vaQueryVendorString"));
        PRISM_VA_SYM(api.va, vaExportSurfaceHandle, "vaExportSurfaceHandle");
        PRISM_VA_SYM(api.va, vaSyncSurface, "vaSyncSurface");
        PRISM_VA_SYM(api.egl, eglGetCurrentDisplay, "eglGetCurrentDisplay");
        PRISM_VA_SYM(api.egl, eglQueryString, "eglQueryString");
#undef PRISM_VA_SYM

        // eglCreateImageKHR is an extension entry point, so it comes from eglGetProcAddress
        // rather than the library's symbol table. glEGLImageTargetTexture2DOES comes from Qt's
        // context, which knows which GL implementation is actually current.
        auto *getProc = reinterpret_cast<void *(*)(const char *)>(dlsym(api.egl, "eglGetProcAddress"));
        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        if (!getProc || !ctx)
            return;
        api.eglCreateImageKHR =
            reinterpret_cast<decltype(api.eglCreateImageKHR)>(getProc("eglCreateImageKHR"));
        api.eglDestroyImageKHR =
            reinterpret_cast<decltype(api.eglDestroyImageKHR)>(getProc("eglDestroyImageKHR"));
        api.glEGLImageTargetTexture2DOES =
            reinterpret_cast<decltype(api.glEGLImageTargetTexture2DOES)>(
                ctx->getProcAddress("glEGLImageTargetTexture2DOES"));
        if (!api.eglCreateImageKHR || !api.eglDestroyImageKHR || !api.glEGLImageTargetTexture2DOES)
            return;

        // Under libglvnd every one of those pointers is a dispatch stub that exists whether or
        // not the driver implements the entry point, so the extension strings are the only real
        // availability test.
        if (!ctx->hasExtension(QByteArrayLiteral("GL_OES_EGL_image")))
            return;
        api.ok = true;
    });
    return api;
}

// Reads AVVAAPIDeviceContext::display, whose VADisplay is the first and only member.
void *vaapiDisplayOf(const AVFrame *frame)
{
    if (!frame || !frame->hw_frames_ctx)
        return nullptr;
    const auto *fc = reinterpret_cast<const AVHWFramesContext *>(frame->hw_frames_ctx->data);
    if (!fc || !fc->device_ctx || fc->device_ctx->type != AV_HWDEVICE_TYPE_VAAPI
        || !fc->device_ctx->hwctx)
        return nullptr;
    return *reinterpret_cast<void *const *>(fc->device_ctx->hwctx);
}

bool vaapiSwFormatIsNv12(const AVFrame *frame)
{
    if (!frame || !frame->hw_frames_ctx)
        return false;
    const auto *fc = reinterpret_cast<const AVHWFramesContext *>(frame->hw_frames_ctx->data);
    return fc && fc->sw_format == AV_PIX_FMT_NV12;
}

// vaExportSurfaceHandle hands over owned fds. They have to be closed on every path out,
// including the ones that reject the descriptor before an EGLImage is ever created — at 60 fps
// a leak there exhausts the process fd table in under a minute, and it surfaces as unrelated
// file-open failures elsewhere in the app.
struct ExportedSurfaceFds
{
    VaDrmPrimeSurfaceDescriptor *desc = nullptr;

    ~ExportedSurfaceFds()
    {
        if (!desc)
            return;
        const uint32_t count = qMin(desc->num_objects, 4u);
        for (uint32_t i = 0; i < count; ++i) {
            if (desc->objects[i].fd >= 0)
                ::close(desc->objects[i].fd);
        }
    }
};

QString fourccString(uint32_t value)
{
    char s[5] = {char(value), char(value >> 8), char(value >> 16), char(value >> 24), 0};
    for (char &c : s) {
        if (c != 0 && (c < 32 || c > 126))
            c = '?';
    }
    return QString::fromLatin1(s);
}

QString describePrime(const VaDrmPrimeSurfaceDescriptor &desc)
{
    QString text = QStringLiteral("fourcc=%1 objects=%2 layers=%3")
                       .arg(fourccString(desc.fourcc))
                       .arg(desc.num_objects)
                       .arg(desc.num_layers);
    const uint32_t n = qMin(desc.num_layers, 4u);
    for (uint32_t i = 0; i < n; ++i) {
        text += QStringLiteral(" layer%1={format=%2 planes=%3}")
                    .arg(i)
                    .arg(fourccString(desc.layers[i].drm_format))
                    .arg(desc.layers[i].num_planes);
    }
    return text;
}
#endif // PRISM_VAAPI_IMPORT


} // namespace


GpuVideoUploader &GpuVideoUploader::instance()
{
    // Never destroyed: a CUDA context torn down during the driver's own exit teardown aborts
    // the process, and the OS reclaims everything anyway.
    static GpuVideoUploader *uploader = new GpuVideoUploader;
    return *uploader;
}

GpuVideoUploader::GpuVideoUploader() = default;
GpuVideoUploader::~GpuVideoUploader() = default;

bool GpuVideoUploader::ensureContext()
{
    if (m_state != 0)
        return m_state > 0;
    m_state = -1;

    QSurfaceFormat fmt;
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);

    m_surface = new QOffscreenSurface();
    m_surface->setFormat(fmt);
    m_surface->create();
    if (!m_surface->isValid()) {
        qWarning("GpuVideoUploader: offscreen surface unavailable; decoding to RGB on the CPU");
        return false;
    }

    m_context = new QOpenGLContext();
    m_context->setFormat(fmt);
    if (QOpenGLContext *share = QOpenGLContext::globalShareContext())
        m_context->setShareContext(share);
    QOpenGLContext *prevContext = QOpenGLContext::currentContext();
    QSurface *prevSurface = prevContext ? prevContext->surface() : nullptr;
    const auto restore = qScopeGuard([&] {
        if (prevContext && prevSurface)
            prevContext->makeCurrent(prevSurface);
        else if (m_context)
            m_context->doneCurrent();
    });
    if (!m_context->create() || !m_context->makeCurrent(m_surface)) {
        qWarning("GpuVideoUploader: GL context unavailable; decoding to RGB on the CPU");
        return false;
    }

    // R8/RG8 textures, PBO mapping and GLSL 1.20 all need GL 3.0 (or ES 3.0).
    const QSurfaceFormat actual = m_context->format();
    const bool versionOk = actual.majorVersion() >= 3;
    if (!versionOk) {
        qWarning("GpuVideoUploader: GL %d.%d is too old for the NV12 path; decoding to RGB on the CPU",
                 actual.majorVersion(), actual.minorVersion());
        return false;
    }

    m_program = new QOpenGLShaderProgram();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kNv12VertexShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kNv12FragShader);
    m_program->bindAttributeLocation("a_position", 0);
    m_program->bindAttributeLocation("a_texCoord", 1);
    if (!m_program->link()) {
        qWarning("GpuVideoUploader: NV12 shader failed: %s", qUtf8Printable(m_program->log()));
        return false;
    }

    if (const char *vendor = reinterpret_cast<const char *>(
            m_context->functions()->glGetString(GL_VENDOR))) {
        if (hwaccel::renderVendor().isEmpty())
            hwaccel::setRenderVendor(QString::fromUtf8(vendor));
    }

    m_state = 1;
    return true;
}

bool GpuVideoUploader::available()
{
    return ensureContext();
}

// Callers may have their own context current (VideoWidget primes a source between its own
// makeCurrent() and upload), so the previous one is put back rather than left unbound.
bool GpuVideoUploader::makeCurrent()
{
    m_prevContext = QOpenGLContext::currentContext();
    m_prevSurface = m_prevContext ? m_prevContext->surface() : nullptr;
    return ensureContext() && m_context->makeCurrent(m_surface);
}

void GpuVideoUploader::doneCurrent()
{
    if (m_prevContext && m_prevContext != m_context && m_prevSurface)
        m_prevContext->makeCurrent(m_prevSurface);
    else
        m_context->doneCurrent();
    m_prevContext = nullptr;
    m_prevSurface = nullptr;
}

bool GpuVideoUploader::mayImport(const AVFrame *frame)
{
    if (!frame)
        return false;
    const auto format = static_cast<AVPixelFormat>(frame->format);
    if (!isHwPixelFormat(format))
        return true;
    if (zeroCopyMode() == ZeroCopyMode::Off || hwSwFormat(frame) != AV_PIX_FMT_NV12)
        return false;
    switch (format) {
    case AV_PIX_FMT_CUDA:
        return g_cudaImportOk.load(std::memory_order_relaxed);
    case AV_PIX_FMT_VAAPI:
        return g_vaapiImportOk.load(std::memory_order_relaxed);
    case AV_PIX_FMT_D3D11:
        return g_d3d11ImportOk.load(std::memory_order_relaxed);
    default:
        return false;
    }
}

GpuVideoUploader::UploadPath GpuVideoUploader::lastUploadPath()
{
    QMutexLocker lock(&g_previewImportMutex);
    return g_previewUploadPath;
}

QString GpuVideoUploader::lastZeroCopyDeclineReason()
{
    QMutexLocker lock(&g_previewImportMutex);
    return g_zeroCopyDeclineReason;
}

void GpuVideoUploader::release(std::unique_ptr<QOpenGLFramebufferObject> &target)
{
    if (!target)
        return;
    if (makeCurrent()) {
        target.reset();
        doneCurrent();
    } else {
        // Without the context the FBO cannot be deleted cleanly; leak the GL names rather than
        // deleting them in whatever context happens to be current.
        (void)target.release();
    }
}

bool GpuVideoUploader::readback(QOpenGLFramebufferObject *target, uint8_t *rgb)
{
    if (!target || !rgb || !makeCurrent())
        return false;
    QOpenGLExtraFunctions *gl = m_context->extraFunctions();
    target->bind();
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl->glReadPixels(0, 0, target->width(), target->height(), GL_RGB, GL_UNSIGNED_BYTE, rgb);
    target->release();
    doneCurrent();
    return true;
}

GLuint GpuVideoUploader::render(const AVFrame *av, int rotation,
                                std::unique_ptr<QOpenGLFramebufferObject> &target)
{
    if (!av || av->width < 2 || av->height < 2 || !makeCurrent())
        return 0;
    const auto done = qScopeGuard([this] { doneCurrent(); });
    QOpenGLExtraFunctions *gl = m_context->extraFunctions();

    // CUDA copies into the pooled textures; VAAPI binds the decoder's own dma-buf into a separate
    // pair, and D3D11 hands back its interop pair. Anything none of them takes falls through to a
    // hardware transfer and a PBO upload.
    GLuint texY = 0;
    GLuint texUV = 0;
    bool d3d11Locked = false;
    bool uploaded = importCudaNv12(gl, av);
    if (uploaded) {
        texY = m_videoY;
        texUV = m_videoUV;
        recordPreviewUploadPath(UploadPath::CudaInterop);
    } else if (importVaapiNv12(gl, av)) {
        uploaded = true;
        texY = m_importY;
        texUV = m_importUV;
        recordPreviewUploadPath(UploadPath::VaapiDmaBuf);
    } else if (importD3d11Nv12(gl, av, &texY, &texUV)) {
        uploaded = true;
        d3d11Locked = true;
        recordPreviewUploadPath(UploadPath::D3d11Interop);
    }
    // D3D11 cannot render the next frame into the interop textures while GL holds them, so every
    // return below, drawn or not, unlocks.
    const auto unlockD3d11 = qScopeGuard([this, d3d11Locked] {
        if (d3d11Locked)
            unlockD3d11Import();
    });

    if (!uploaded) {
        AVFrame *nv12 = ensureSoftwareNv12(av);
        if (!nv12 || nv12->format != AV_PIX_FMT_NV12)
            return 0;
        const int w = nv12->width;
        const int h = nv12->height;
        if (!ensureVideoUploadTextures(gl, w, h))
            return 0;
        if (!uploadPlanePbo(gl, m_videoY, w, h, GL_R8, GL_RED, nv12->data[0], nv12->linesize[0], w)
            || !uploadPlanePbo(gl, m_videoUV, w / 2, h / 2, GL_RG8, GL_RG, nv12->data[1],
                               nv12->linesize[1], w))
            return 0;
        texY = m_videoY;
        texUV = m_videoUV;
        recordPreviewUploadPath(UploadPath::CpuRoundTrip);
    }

    const bool swap = rotation == 90 || rotation == 270;
    const int destW = qMax(2, (swap ? av->height : av->width) & ~1);
    const int destH = qMax(2, (swap ? av->width : av->height) & ~1);
    if (!target || target->width() != destW || target->height() != destH) {
        target = std::make_unique<QOpenGLFramebufferObject>(
            destW, destH, QOpenGLFramebufferObject::NoAttachment, GL_TEXTURE_2D, GL_RGBA8);
        if (!target->isValid()) {
            target.reset();
            return 0;
        }
        gl->glBindTexture(GL_TEXTURE_2D, target->texture());
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glBindTexture(GL_TEXTURE_2D, 0);
    }

    QVector3D offset;
    QVector3D scale;
    yuvRangeUniforms(av->color_range, &offset, &scale);

    target->bind();
    gl->glViewport(0, 0, destW, destH);
    gl->glDisable(GL_BLEND);
    m_program->bind();
    m_program->setUniformValue("u_y", 0);
    m_program->setUniformValue("u_uv", 1);
    m_program->setUniformValue("u_yuvToRgb", yuvToRgbMatrix(av->colorspace));
    m_program->setUniformValue("u_yuvOffset", offset);
    m_program->setUniformValue("u_yuvScale", scale);
    m_program->setUniformValue("u_texMap", texMapForRotation(rotation));
    gl->glActiveTexture(GL_TEXTURE1);
    gl->glBindTexture(GL_TEXTURE_2D, texUV);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, texY);
    m_program->enableAttributeArray(0);
    m_program->enableAttributeArray(1);
    m_program->setAttributeArray(0, GL_FLOAT, kQuad, 2, 4 * sizeof(float));
    m_program->setAttributeArray(1, GL_FLOAT, kQuad + 2, 2, 4 * sizeof(float));
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_program->disableAttributeArray(0);
    m_program->disableAttributeArray(1);
    m_program->release();
    gl->glActiveTexture(GL_TEXTURE1);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    target->release();

    // VideoWidget samples the texture from its own context. glFlush alone does not order the
    // two contexts, and an imported surface may be recycled by the decoder right after.
    gl->glFinish();
    return target->texture();
}

void GpuVideoUploader::markCudaFailed()
{
    m_cudaImportFailed = true;
    g_cudaImportOk.store(false, std::memory_order_relaxed);
}

void GpuVideoUploader::markVaapiFailed()
{
    m_vaapiImportFailed = true;
    g_vaapiImportOk.store(false, std::memory_order_relaxed);
}

void GpuVideoUploader::markD3d11Failed()
{
    m_d3d11ImportFailed = true;
    g_d3d11ImportOk.store(false, std::memory_order_relaxed);
}

bool GpuVideoUploader::ensureVideoUploadTextures(QOpenGLExtraFunctions *gl, int width, int height)
{
    if (!gl || width < 2 || height < 2 || (width % 2) || (height % 2))
        return false;
    if (m_videoY && m_videoUV && m_videoTexW == width && m_videoTexH == height)
        return true;

    unregisterCudaResources();
    if (m_videoY)
        gl->glDeleteTextures(1, &m_videoY);
    if (m_videoUV)
        gl->glDeleteTextures(1, &m_videoUV);
    m_videoY = m_videoUV = 0;

    gl->glGenTextures(1, &m_videoY);
    gl->glBindTexture(GL_TEXTURE_2D, m_videoY);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);

    gl->glGenTextures(1, &m_videoUV);
    gl->glBindTexture(GL_TEXTURE_2D, m_videoUV);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, width / 2, height / 2, 0, GL_RG, GL_UNSIGNED_BYTE,
                     nullptr);

    m_videoTexW = width;
    m_videoTexH = height;
    return m_videoY != 0 && m_videoUV != 0;
}

bool GpuVideoUploader::uploadPlanePbo(QOpenGLExtraFunctions *gl, GLuint texture, int texW, int texH,
                               GLenum internalFormat, GLenum format, const uint8_t *src, int srcPitch,
                               int packedWidth)
{
    Q_UNUSED(internalFormat);
    if (!gl || !texture || !src || texW <= 0 || texH <= 0 || packedWidth <= 0 || srcPitch <= 0)
        return false;
    const qsizetype packed = qsizetype(packedWidth) * texH;
    if (!m_videoPbo[0])
        gl->glGenBuffers(2, m_videoPbo);
    const GLuint pbo = m_videoPbo[m_videoPboIndex];
    m_videoPboIndex ^= 1;
    gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    gl->glBufferData(GL_PIXEL_UNPACK_BUFFER, packed, nullptr, GL_STREAM_DRAW);
    void *dst = gl->glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, packed,
                                     GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (!dst) {
        gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        return false;
    }
    if (srcPitch == packedWidth) {
        memcpy(dst, src, size_t(packed));
    } else {
        auto *out = static_cast<uint8_t *>(dst);
        const int rowBytes = qMin(packedWidth, srcPitch);
        for (int y = 0; y < texH; ++y)
            memcpy(out + size_t(y) * packedWidth, src + size_t(y) * srcPitch, size_t(rowBytes));
    }
    gl->glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    gl->glBindTexture(GL_TEXTURE_2D, texture);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texW, texH, format, GL_UNSIGNED_BYTE, nullptr);
    gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    return true;
}

void GpuVideoUploader::unregisterCudaResources()
{
#if !defined(Q_OS_MACOS)
    CudaGlApi &api = cudaGlApi();
    if (api.ok && (m_cudaYResource || m_cudaUvResource)) {
        // From the context they were registered in. Callers may have another one current —
        // importCudaNv12 has the incoming frame's pushed — and CUDA refuses an unregister from the
        // wrong context, which would leak the registration.
        CUcontext owner = nullptr;
        if (m_cudaResourceDevice) {
            const auto *device = reinterpret_cast<const AVHWDeviceContext *>(m_cudaResourceDevice->data);
            if (device && device->hwctx)
                owner = *reinterpret_cast<CUcontext const *>(device->hwctx);
        }
        const bool pushed = owner && api.cuCtxPushCurrent(owner) == kCuSuccess;
        if (m_cudaYResource)
            api.cuGraphicsUnregisterResource(static_cast<CUgraphicsResource>(m_cudaYResource));
        if (m_cudaUvResource)
            api.cuGraphicsUnregisterResource(static_cast<CUgraphicsResource>(m_cudaUvResource));
        if (pushed) {
            CUcontext popped = nullptr;
            api.cuCtxPopCurrent(&popped);
        }
    }
    m_cudaYResource = nullptr;
    m_cudaUvResource = nullptr;
    av_buffer_unref(&m_cudaResourceDevice);
    m_cudaTexW = 0;
    m_cudaTexH = 0;
#endif
}

bool GpuVideoUploader::importCudaNv12(QOpenGLExtraFunctions *gl, const AVFrame *frame)
{
#if defined(Q_OS_MACOS)
    Q_UNUSED(gl);
    Q_UNUSED(frame);
    return false;
#else
    if (m_cudaImportFailed || !gl || !frame || frame->format != AV_PIX_FMT_CUDA
        || !cudaSwFormatIsNv12(frame))
        return false;

    // CUDA can only register GL textures that live on its own GPU. On a hybrid laptop compositing
    // on the integrated GPU the registration below cannot succeed, and it would run inside FFmpeg's
    // CUDA context — the one NVDEC is decoding on — so it is not attempted at all. Decided once:
    // the GL context does not move between GPUs within a session.
    if (m_cudaGlVendorOk < 0) {
        const char *vendor = reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR));
        m_cudaGlVendorOk = (vendor && strstr(vendor, "NVIDIA")) ? 1 : 0;
        if (!m_cudaGlVendorOk) {
            noteZeroCopyDecline(
                QStringLiteral("OpenGL renders on %1; CUDA interop needs OpenGL on the NVIDIA GPU")
                    .arg(vendor ? QString::fromUtf8(vendor) : QStringLiteral("an unknown GPU")));
        }
    }
    if (!m_cudaGlVendorOk) {
        markCudaFailed();
        return false;
    }

    CudaGlApi &api = cudaGlApi();
    if (!api.ok) {
        noteZeroCopyDecline(QStringLiteral("the CUDA driver's GL interop entry points are unavailable"));
        markCudaFailed();
        return false;
    }
    CUcontext ctx = nullptr;
    CUstream stream = nullptr;
    if (!cudaContextOf(frame, &ctx, &stream))
        return false;

    const int w = frame->width;
    const int h = frame->height;
    if (!ensureVideoUploadTextures(gl, w, h))
        return false;

    if (api.cuCtxPushCurrent(ctx) != kCuSuccess)
        return false;

    // ClipReader shares one CUDA device between readers, so this normally never changes. When it
    // does — an exporter's device, or a device recreated after a failure — the registrations made
    // under the old context cannot be mapped from this one (CUDA_ERROR_INVALID_HANDLE), which is
    // exactly what used to switch interop off for the whole session.
    const AVBufferRef *frameDevice =
        reinterpret_cast<const AVHWFramesContext *>(frame->hw_frames_ctx->data)->device_ref;
    const bool deviceChanged =
        m_cudaResourceDevice && frameDevice && m_cudaResourceDevice->data != frameDevice->data;

    bool ok = false;
    if (deviceChanged || m_cudaTexW != w || m_cudaTexH != h || !m_cudaYResource
        || !m_cudaUvResource) {
        unregisterCudaResources();
        CUgraphicsResource yRes = nullptr;
        CUgraphicsResource uvRes = nullptr;
        CUresult rc =
            api.cuGraphicsGLRegisterImage(&yRes, m_videoY, GL_TEXTURE_2D, kCuRegisterWriteDiscard);
        if (rc == kCuSuccess)
            rc = api.cuGraphicsGLRegisterImage(&uvRes, m_videoUV, GL_TEXTURE_2D,
                                               kCuRegisterWriteDiscard);
        if (rc == kCuSuccess) {
            m_cudaYResource = yRes;
            m_cudaUvResource = uvRes;
            m_cudaResourceDevice = frameDevice ? av_buffer_ref(frameDevice) : nullptr;
            m_cudaTexW = w;
            m_cudaTexH = h;
        } else {
            if (yRes)
                api.cuGraphicsUnregisterResource(yRes);
            if (uvRes)
                api.cuGraphicsUnregisterResource(uvRes);
            noteZeroCopyDecline(
                QStringLiteral("cuGraphicsGLRegisterImage failed (CUDA error %1)").arg(rc));
            markCudaFailed();
        }
    }

    if (m_cudaYResource && m_cudaUvResource) {
        QString why;
        ok = copyCudaNv12ToTextures(api, stream, static_cast<CUgraphicsResource>(m_cudaYResource),
                                    static_cast<CUgraphicsResource>(m_cudaUvResource), frame, w, h,
                                    &why);
        if (!ok) {
            noteZeroCopyDecline(
                QStringLiteral("copying the NVDEC surface into GL textures failed: %1").arg(why));
            qWarning("GpuVideoUploader: CUDA interop copy failed: %s", qUtf8Printable(why));
            markCudaFailed();
        }
    }

    CUcontext popped = nullptr;
    api.cuCtxPopCurrent(&popped);
    return ok;
#endif
}

bool GpuVideoUploader::importD3d11Nv12(QOpenGLExtraFunctions *gl, const AVFrame *frame, GLuint *texY,
                                GLuint *texUV)
{
#if !defined(Q_OS_WIN)
    Q_UNUSED(gl);
    Q_UNUSED(frame);
    Q_UNUSED(texY);
    Q_UNUSED(texUV);
    return false;
#else
    if (m_d3d11ImportFailed || !gl || !frame || frame->format != AV_PIX_FMT_D3D11)
        return false;
    if (zeroCopyMode() == ZeroCopyMode::Off) {
        noteZeroCopyDecline(QStringLiteral(
            "zero-copy is turned off (playback/zeroCopy or PRISM_ZEROCOPY)"));
        return false;
    }
    if (!m_d3d11)
        m_d3d11 = std::make_unique<gl::D3d11GlInterop>();

    QString why;
    switch (m_d3d11->lock(gl, frame, texY, texUV, &why)) {
    case gl::D3d11GlInterop::Result::Locked:
        return true;
    case gl::D3d11GlInterop::Result::Declined:
        // A property of this frame, not of the machine: the next clip may import fine.
        if (!why.isEmpty())
            noteZeroCopyDecline(why);
        return false;
    case gl::D3d11GlInterop::Result::Failed:
        break;
    }
    noteZeroCopyDecline(why);
    qWarning("GpuVideoUploader: D3D11 zero-copy import unavailable (%s)", qUtf8Printable(why));
    markD3d11Failed();
    m_d3d11->release(gl);
    return false;
#endif
}

void GpuVideoUploader::unlockD3d11Import()
{
#if defined(Q_OS_WIN)
    if (m_d3d11)
        m_d3d11->unlock();
#endif
}

bool GpuVideoUploader::ensureImportTextureNames(QOpenGLExtraFunctions *gl)
{
    if (!gl)
        return false;
    if (m_importY && m_importUV)
        return true;

    // Lazily, not once at init: destroyVideoUploadState() deletes these, and releaseCaches()
    // calls it mid-session. Creating them up front would leave the next import binding name 0.
    for (GLuint *tex : {&m_importY, &m_importUV}) {
        if (*tex)
            continue;
        gl->glGenTextures(1, tex);
        gl->glBindTexture(GL_TEXTURE_2D, *tex);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    // No glTexImage2D: glEGLImageTargetTexture2DOES supplies the storage.
    return m_importY != 0 && m_importUV != 0;
}

#if defined(PRISM_VAAPI_IMPORT)
namespace {

// Auto engages zero-copy only where the whole chain — surface export, dma-buf import and
// sampling — has been checked end to end. Everything else in this function can detect its own
// failure and fall back; sampling an imported surface wrongly cannot, because it produces a
// picture that looks like a decode bug. An unrecognised driver therefore stays on the PBO
// path instead of being guessed at. Users who want it anyway can still set it explicitly.
bool vaapiDriverIsVerified(VaEglApi &api, void *display, QOpenGLExtraFunctions *gl)
{
    if (!api.vaQueryVendorString || !display || !gl)
        return false;
    const char *vaVendor = api.vaQueryVendorString(display);
    if (!vaVendor || !strstr(vaVendor, "iHD"))
        return false;
    const char *renderer = reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER));
    if (!renderer)
        return false;
    // Mesa's iris. i965, removed from Mesa in 22.0, reports "Mesa DRI Intel(R) ..." and has
    // never been through this path.
    return strstr(renderer, "Mesa") && strstr(renderer, "Intel") && !strstr(renderer, "DRI");
}

} // namespace
#endif // PRISM_VAAPI_IMPORT

bool GpuVideoUploader::importVaapiNv12(QOpenGLExtraFunctions *gl, const AVFrame *frame)
{
#if !defined(PRISM_VAAPI_IMPORT)
    Q_UNUSED(gl);
    Q_UNUSED(frame);
    return false;
#else
    if (m_vaapiImportFailed || !gl || !frame || frame->format != AV_PIX_FMT_VAAPI)
        return false;

    // Content and configuration mismatches fall through to the PBO path for this frame only.
    // Marking them sticky would let one 10-bit clip disable zero-copy for every 8-bit clip
    // beside it on the timeline: scale_vaapi's size-only retry keeps the native sw_format.
    if (!vaapiSwFormatIsNv12(frame))
        return false;
    void *display = vaapiDisplayOf(frame);
    if (!display)
        return false;

    // Every *detectable* failure below falls back to the PBO upload, but a driver that exports
    // a surface we import successfully and sample wrongly shows up as corrupt preview with
    // nothing to catch. So an explicit setting is honoured as given, and Auto engages only on
    // the driver this has been verified against — see vaapiDriverIsVerified.
    //
    // Neither refusal is sticky: a later test, or a restart after flipping the setting, must
    // still be able to engage the path. Real import failures below are.
    const ZeroCopyMode zeroCopy = zeroCopyMode();
    if (zeroCopy == ZeroCopyMode::Off)
        return false;

    VaEglApi &api = vaEglApi();
    if (!api.ok) {
        logVaapiImportOnce("libva/EGL entry points or GL_OES_EGL_image missing");
        markVaapiFailed();
        return false;
    }

    if (zeroCopy == ZeroCopyMode::Auto) {
        if (m_vaapiAutoVerified < 0)
            m_vaapiAutoVerified = vaapiDriverIsVerified(api, display, gl) ? 1 : 0;
        if (m_vaapiAutoVerified == 0) {
            logVaapiImportOnce("driver outside the verified zero-copy set; set "
                               "playback/zeroCopy to On to use it anyway");
            // Fixed for the session, so decoders may as well read surfaces back on their own
            // threads rather than hand them here for a GUI-thread transfer.
            g_vaapiImportOk.store(false, std::memory_order_relaxed);
            return false;
        }
    }

    // Only valid while the context is current, which render() guarantees. Under Qt's xcb plugin
    // the GL context is GLX and there is no EGL display to import into at all.
    EGLDisplayHandle egl = api.eglGetCurrentDisplay();
    if (!egl) {
        logVaapiImportOnce("no current EGL display; Qt is probably on GLX rather than EGL");
        markVaapiFailed();
        return false;
    }
    const char *eglExts = api.eglQueryString(egl, kEglExtensions);
    if (!eglExts || !strstr(eglExts, "EGL_EXT_image_dma_buf_import")) {
        logVaapiImportOnce("EGL_EXT_image_dma_buf_import missing");
        markVaapiFailed();
        return false;
    }
    // Tiled surfaces need their modifier passed through or the import is refused or garbled,
    // but sending modifier attribs without this extension is EGL_BAD_ATTRIBUTE.
    const bool useModifiers = strstr(eglExts, "EGL_EXT_image_dma_buf_import_modifiers") != nullptr;

    const int codedW = frame->width & ~1;
    const int codedH = frame->height & ~1;
    if (codedW < 2 || codedH < 2)
        return false;

    VaDrmPrimeSurfaceDescriptor desc{};
    for (auto &object : desc.objects)
        object.fd = -1;
    const auto surface = VASurfaceID(uintptr_t(frame->data[3]));
    if (api.vaExportSurfaceHandle(display, surface, kVaMemTypeDrmPrime2,
                                  kVaExportReadOnly | kVaExportSeparateLayers, &desc)
        != kVaStatusSuccess) {
        logVaapiImportOnce("vaExportSurfaceHandle failed");
        markVaapiFailed();
        return false;
    }
    const ExportedSurfaceFds fds{&desc};
    // The decoder or VPP may still be writing. Failing to sync is not a reason to abandon the
    // path, so its result is deliberately not checked — this matches mpv.
    api.vaSyncSurface(display, surface);

    // Guards against a libva ABI change shifting every field past objects[]: without them a
    // mismatched struct imports plausible-looking garbage instead of failing. SEPARATE_LAYERS
    // is still the export request; the composed NV12 shape is accepted because some drivers
    // ignore that flag.
    struct ImportPlane
    {
        int fd = -1;
        uint32_t offset = 0;
        uint32_t pitch = 0;
        uint32_t fourcc = 0;
        uint64_t modifier = kDrmFormatModInvalid;
    };
    ImportPlane planes[2];
    bool shapeOk = false;
    if (desc.num_objects >= 1 && desc.num_objects <= 4) {
        if (desc.num_layers == 2 && desc.layers[0].drm_format == kDrmFormatR8
            && desc.layers[1].drm_format == kDrmFormatGr88) {
            shapeOk = true;
            for (uint32_t i = 0; i < 2; ++i) {
                const auto &layer = desc.layers[i];
                if (layer.num_planes != 1 || layer.object_index[0] >= desc.num_objects
                    || layer.pitch[0] == 0) {
                    shapeOk = false;
                    break;
                }
                const auto &object = desc.objects[layer.object_index[0]];
                planes[i] = {object.fd, layer.offset[0], layer.pitch[0], layer.drm_format,
                             object.drm_format_modifier};
            }
        } else if (desc.num_layers == 1 && desc.layers[0].drm_format == kDrmFormatNv12
                   && desc.layers[0].num_planes == 2) {
            const auto &layer = desc.layers[0];
            if (layer.object_index[0] < desc.num_objects && layer.object_index[1] < desc.num_objects
                && layer.pitch[0] != 0 && layer.pitch[1] != 0) {
                const auto &obj0 = desc.objects[layer.object_index[0]];
                const auto &obj1 = desc.objects[layer.object_index[1]];
                planes[0] = {obj0.fd, layer.offset[0], layer.pitch[0], kDrmFormatR8,
                             obj0.drm_format_modifier};
                planes[1] = {obj1.fd, layer.offset[1], layer.pitch[1], kDrmFormatGr88,
                             obj1.drm_format_modifier};
                shapeOk = true;
            }
        }
    }
    if (!shapeOk) {
        logVaapiImportOnce(
            QStringLiteral("unexpected drm-prime descriptor (%1)").arg(describePrime(desc)));
        markVaapiFailed();
        return false;
    }

    // Decide once per frame, before any EGLImage is created. Importing a tiled buffer as
    // implicit-linear (what radeonsi GFX6–8 exports as MOD_INVALID) samples garbage.
    const uint64_t mod0 = planes[0].modifier;
    const uint64_t mod1 = planes[1].modifier;
    const bool modifierUnknown = mod0 == kDrmFormatModInvalid || mod1 == kDrmFormatModInvalid;
    const bool tiled = (mod0 != kDrmFormatModInvalid && mod0 != kDrmFormatModLinear)
                       || (mod1 != kDrmFormatModInvalid && mod1 != kDrmFormatModLinear);
    if (modifierUnknown) {
        logVaapiImportOnce(
            QStringLiteral("driver reports no DRM modifier (tiling unknown); pre-GFX9 radeonsi "
                           "does this"));
        markVaapiFailed();
        return false;
    }
    if (tiled && !useModifiers) {
        logVaapiImportOnce(
            QStringLiteral("EGL_EXT_image_dma_buf_import_modifiers missing; cannot describe tiled "
                           "surface"));
        markVaapiFailed();
        return false;
    }
    const bool passModifierAttribs = tiled;

    if (!ensureImportTextureNames(gl))
        return false;

    const GLuint textures[2] = {m_importY, m_importUV};
    for (uint32_t i = 0; i < 2; ++i) {
        const ImportPlane &plane = planes[i];

        // The surface allocation is padded (1080 -> 1088); the picture is not. Sizing the image
        // from the descriptor would sample that padding, and because kQuad puts v=0 at the NDC
        // bottom it would land as a garbage strip along the visual bottom edge. The pitch
        // attribute already carries the stride, so the padded height is never needed.
        int32_t attribs[32];
        int n = 0;
        attribs[n++] = kEglWidth;
        attribs[n++] = i == 0 ? codedW : codedW / 2;
        attribs[n++] = kEglHeight;
        attribs[n++] = i == 0 ? codedH : codedH / 2;
        attribs[n++] = kEglLinuxDrmFourccExt;
        attribs[n++] = int32_t(plane.fourcc);
        attribs[n++] = kEglDmaBufPlane0FdExt;
        attribs[n++] = plane.fd;
        attribs[n++] = kEglDmaBufPlane0OffsetExt;
        attribs[n++] = int32_t(plane.offset);
        attribs[n++] = kEglDmaBufPlane0PitchExt;
        attribs[n++] = int32_t(plane.pitch);
        if (passModifierAttribs) {
            attribs[n++] = kEglDmaBufPlane0ModifierLoExt;
            attribs[n++] = int32_t(plane.modifier & 0xffffffffu);
            attribs[n++] = kEglDmaBufPlane0ModifierHiExt;
            attribs[n++] = int32_t(plane.modifier >> 32);
        }
        attribs[n++] = kEglNone;

        // EGL_NO_CONTEXT is required for EGL_LINUX_DMA_BUF_EXT.
        EGLImageHandle image =
            api.eglCreateImageKHR(egl, nullptr, kEglLinuxDmaBufExt, nullptr, attribs);
        if (!image) {
            logVaapiImportOnce("eglCreateImageKHR rejected the exported dma-buf");
            markVaapiFailed();
            return false;
        }
        gl->glBindTexture(GL_TEXTURE_2D, textures[i]);
        api.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
        // Destroying the image only orphans the handle; the texture sibling keeps the buffer.
        // Doing it here rather than after the convert draw means the early returns between the
        // two cannot leak one.
        api.eglDestroyImageKHR(egl, image);
    }

    // The same two texture names are re-targeted every frame. That is safe because commands in
    // one context are ordered and the driver holds a reference to what an already-recorded draw
    // sampled. The VA surface outlives the draw: render() finishes before returning and the
    // source keeps the AVFrame as its current frame.
    return true;
#endif
}

AVFrame *GpuVideoUploader::ensureSoftwareNv12(const AVFrame *src)
{
    if (!src)
        return nullptr;
    if (src->format == AV_PIX_FMT_NV12)
        return const_cast<AVFrame *>(src);

    if (isHwPixelFormat(static_cast<AVPixelFormat>(src->format))) {
        if (!m_hwImportStaging)
            m_hwImportStaging = av_frame_alloc();
        if (!m_hwImportStaging)
            return nullptr;
        av_frame_unref(m_hwImportStaging);
        if (av_hwframe_transfer_data(m_hwImportStaging, src, 0) < 0) {
            av_frame_unref(m_hwImportStaging);
            return nullptr;
        }
        src = m_hwImportStaging;
        if (src->format == AV_PIX_FMT_NV12)
            return m_hwImportStaging;
    }

    const int tw = src->width & ~1;
    const int th = src->height & ~1;
    if (tw < 2 || th < 2)
        return nullptr;

    m_importSws = sws_getCachedContext(m_importSws, src->width, src->height,
                                       static_cast<AVPixelFormat>(src->format), tw, th, AV_PIX_FMT_NV12,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_importSws)
        return nullptr;

    if (!m_importNv12)
        m_importNv12 = av_frame_alloc();
    if (!m_importNv12)
        return nullptr;
    if (m_importNv12->format != AV_PIX_FMT_NV12 || m_importNv12->width != tw
        || m_importNv12->height != th || !m_importNv12->data[0]) {
        av_frame_unref(m_importNv12);
        m_importNv12->format = AV_PIX_FMT_NV12;
        m_importNv12->width = tw;
        m_importNv12->height = th;
        if (av_frame_get_buffer(m_importNv12, 0) < 0) {
            av_frame_unref(m_importNv12);
            return nullptr;
        }
    }
    sws_scale(m_importSws, src->data, src->linesize, 0, src->height, m_importNv12->data,
              m_importNv12->linesize);
    m_importNv12->colorspace = src->colorspace;
    m_importNv12->color_range = src->color_range;
    return m_importNv12;
}

} // namespace prism
