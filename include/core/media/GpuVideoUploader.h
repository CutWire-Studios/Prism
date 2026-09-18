#pragma once

#include <QString>
#include <QtGui/qopengl.h>

#include <cstdint>
#include <memory>

struct AVBufferRef;
struct AVFrame;
struct SwsContext;
class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLExtraFunctions;
class QOpenGLFramebufferObject;
class QOpenGLShaderProgram;
class QSurface;

namespace prism {

namespace gl { class D3d11GlInterop; }

enum class ZeroCopyMode {
    Auto, // VAAPI only on the verified driver; CUDA and D3D11 interop on
    On,   // every import path, and xcb_egl on X11 so VAAPI can import at all
    Off,  // always read hardware frames back to system memory
};

// "playback/zeroCopy" in QSettings, overridden by PRISM_ZEROCOPY=0/1. Cached after the first call.
ZeroCopyMode zeroCopyMode();

// Must run before QApplication: on an explicit On, switches Qt's xcb plugin to EGL so VAAPI
// surfaces can be imported as dma-bufs. Auto never rewrites how the whole window renders.
void applyZeroCopyXcbEgl();

// Turns decoded video frames (software NV12 or hardware surfaces) into RGBA textures that
// VideoWidget and the other GL consumers sample through MediaSource::glTexture().
//
// One offscreen compatibility context, shared with Qt's global share context, does all the
// work: hardware surfaces are imported GPU to GPU where the platform allows (CUDA-GL interop,
// VAAPI dma-buf into EGLImages, D3D11 via WGL_NV_DX_interop2), everything else is uploaded
// through a PBO, and one NV12 shader applies colour matrix, range and display rotation.
//
// GUI thread only. Leaves no context current on return, matching the other offscreen sources.
class GpuVideoUploader {
public:
    enum class UploadPath { None, CudaInterop, VaapiDmaBuf, D3d11Interop, CpuRoundTrip };

    static GpuVideoUploader &instance();

    // Whether the context, R8/RG8 textures and the convert shader are usable. When false, callers
    // convert on the CPU instead.
    bool available();

    // Draws `frame` into `target` (created or resized as needed) at its display size. Returns the
    // target's texture, or 0 on failure.
    GLuint render(const AVFrame *frame, int rotation,
                  std::unique_ptr<QOpenGLFramebufferObject> &target);

    // Packed RGB24 rows, top first, of what render() last drew into `target`.
    bool readback(QOpenGLFramebufferObject *target, uint8_t *rgb);

    // Deletes `target` with the uploader's context current.
    void release(std::unique_ptr<QOpenGLFramebufferObject> &target);

    // Whether a hardware frame of this kind is worth handing over as-is. Decoder threads ask this
    // to decide between queueing the surface and reading it back themselves. Thread-safe.
    static bool mayImport(const AVFrame *frame);

    static UploadPath lastUploadPath();
    static QString lastZeroCopyDeclineReason();

private:
    GpuVideoUploader();
    ~GpuVideoUploader();

    bool ensureContext();
    bool makeCurrent();
    void doneCurrent();

    bool ensureVideoUploadTextures(QOpenGLExtraFunctions *gl, int width, int height);
    bool uploadPlanePbo(QOpenGLExtraFunctions *gl, GLuint texture, int texW, int texH,
                        GLenum internalFormat, GLenum format, const uint8_t *src, int srcPitch,
                        int packedWidth);
    void unregisterCudaResources();
    bool importCudaNv12(QOpenGLExtraFunctions *gl, const AVFrame *frame);
    bool importD3d11Nv12(QOpenGLExtraFunctions *gl, const AVFrame *frame, GLuint *texY,
                         GLuint *texUV);
    void unlockD3d11Import();
    bool ensureImportTextureNames(QOpenGLExtraFunctions *gl);
    bool importVaapiNv12(QOpenGLExtraFunctions *gl, const AVFrame *frame);
    AVFrame *ensureSoftwareNv12(const AVFrame *src);
    void markCudaFailed();
    void markVaapiFailed();
    void markD3d11Failed();

    QOffscreenSurface *m_surface = nullptr;
    QOpenGLContext *m_context = nullptr;
    QOpenGLContext *m_prevContext = nullptr;
    QSurface *m_prevSurface = nullptr;
    QOpenGLShaderProgram *m_program = nullptr;
    int m_state = 0; // 0 untried, 1 ready, -1 unusable

    GLuint m_videoY = 0;
    GLuint m_videoUV = 0;
    int m_videoTexW = 0;
    int m_videoTexH = 0;
    GLuint m_videoPbo[2] = {0, 0};
    int m_videoPboIndex = 0;

    GLuint m_importY = 0;
    GLuint m_importUV = 0;

    void *m_cudaYResource = nullptr;
    void *m_cudaUvResource = nullptr;
    AVBufferRef *m_cudaResourceDevice = nullptr;
    int m_cudaTexW = 0;
    int m_cudaTexH = 0;
    int m_cudaGlVendorOk = -1;
    bool m_cudaImportFailed = false;

    int m_vaapiAutoVerified = -1;
    bool m_vaapiImportFailed = false;

    std::unique_ptr<gl::D3d11GlInterop> m_d3d11;
    bool m_d3d11ImportFailed = false;

    AVFrame *m_hwImportStaging = nullptr;
    AVFrame *m_importNv12 = nullptr;
    SwsContext *m_importSws = nullptr;
};

} // namespace prism
