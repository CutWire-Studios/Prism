#pragma once

#include <QHash>
#include <QString>
#include <QImage>
#include <QByteArray>
#include <QSize>
#include <functional>
#include <vector>

class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;
class QOpenGLShaderProgram;

/// Offscreen, GPU-resident fragment-shader pass pipeline for process-node effects.
///
/// Owns a QOpenGLContext that shares with QOpenGLContext::globalShareContext(), so
/// the FBO texture it produces can be sampled directly by VideoWidget's compositor
/// (which honours MediaSource::glTexture()) with no GPU→CPU→GPU round trip. Modeled
/// on the offscreen GL setup in SlideshowSource.
///
/// Usage per frame:
///   if (!runner.begin(size)) return;              // makes context current
///   GLuint in = runner.uploadInput(rgbaImage);    // or an external shared tex id
///   auto *prog = runner.program(key, fragSrc);
///   GLuint t = runner.runPass(prog, in, setUniforms);   // ping-pong, chainable
///   ... more passes, feeding t back in ...
///   runner.end();                                 // glFlush + doneCurrent
///   // runner.lastOutput() is now valid in any context sharing with the global one.
class EffectPassRunner {
public:
    /// An extra sampler bound alongside u_input for one pass (texture units 1..N).
    struct TexBinding {
        QByteArray   uniform;   // sampler2D uniform name, e.g. "u_input1" or "p_lut"
        unsigned int tex = 0;
    };

    /// Layout of the per-source history buffers: a full-size copy of the previous output (u_prev)
    /// and an atlas of the last `frames` input frames (u_history), one tile per frame.
    struct HistoryPlan {
        bool   ok = false;
        int    frames = 0;
        QSize  tile;          // per-frame size inside the atlas; shrinks below the frame size under the cap
        int    cols = 0, rows = 0;
        QSize  atlas;
        qint64 bytes = 0;     // atlas plus the previous-output copy
    };

    static constexpr qint64 kHistoryCapBytes    = 256ll * 1024 * 1024;   // all sources together
    static constexpr qint64 kHistorySourceBytes = 128ll * 1024 * 1024;   // one source

    /// Pure sizing: shrinks the tiles (x0.7 steps) until the atlas fits @p maxTex and @p budget.
    /// ok is false when even the smallest tile (16 px) or the previous-output copy does not fit.
    static HistoryPlan planHistory(QSize frame, int frames, bool prev, qint64 budget, int maxTex = 4096);
    static qint64 historyBytesInUse();

    ~EffectPassRunner();

    /// Between begin() and end(): (re)allocate history for @p frame. A size or request change
    /// reallocates and clears. False when the cap leaves no room; the caller should pass through.
    bool configureHistory(QSize frame, int frames, bool prev);
    const HistoryPlan &historyPlan() const { return m_hist.plan; }
    unsigned int prevTexture() const { return m_hist.prevTex; }
    unsigned int historyAtlas() const { return m_hist.atlas; }
    /// Next atlas slot to be written; age 1 is the slot before it.
    int historyHead() const { return m_hist.head; }
    /// After the pass, before end(): stores the output as u_prev and pushes @p inputTex into the ring.
    void commitHistory(unsigned int inputTex);
    /// Clears both buffers (makes the context current itself; not for use between begin() and end()).
    void resetHistory();
    /// Copies @p inTex to the output FBO unchanged.
    unsigned int passThrough(unsigned int inTex);

    /// Make the offscreen context current and ensure both ping-pong FBOs match
    /// @p size. Returns false if GL could not be initialised (e.g. no global share
    /// context yet). Must be paired with end().
    bool begin(QSize size);
    void end();

    /// Upload a CPU frame as pass input; returns the input texture id. The image is
    /// converted to RGBA8888 if needed. Uses the same top-row-first convention as
    /// the built-in vertex shader.
    unsigned int uploadInput(const QImage &frame);

    /// Upload @p frame into the persistent texture owned by @p slot (created on first use) and
    /// return its id. Slots are independent of uploadInput()'s scratch texture.
    unsigned int uploadSlot(int slot, const QImage &frame);

    /// A 1x1 fully transparent texture, for extra inputs that have nothing connected.
    unsigned int transparentTexture();

    /// Compile + cache a full-screen-quad program for @p fragmentSrc, keyed by
    /// @p key (stable per shader). Returns null on compile/link failure.
    QOpenGLShaderProgram *program(int key, const char *fragmentSrc);
    /// Compile log of the last failed program(); cleared by a successful one.
    const QString &lastError() const { return m_lastError; }

    /// Render one full-screen pass sampling @p inputTex through @p prog into the
    /// next ping-pong FBO. u_input (sampler, unit 0) and u_texel (1/size) are set
    /// automatically; @p setUniforms sets the rest. Returns the output texture id,
    /// which can be fed as the input of a subsequent pass.
    unsigned int runPass(QOpenGLShaderProgram *prog, unsigned int inputTex,
                         const std::function<void(QOpenGLShaderProgram *)> &setUniforms,
                         const std::vector<TexBinding> &extras = {});

    // ── Compositing (Layer node flatten) ────────────────────────────────────
    // Draws several layers into one shared-context FBO at their normalised canvas
    // placement, bottom→top with source-over alpha, then exposes it via
    // lastOutput()/glTexture(). Placement math is derived to match VideoWidget's
    // deck compositing exactly (a full-canvas layer reduces to the plain blit).
    //
    //   if (!runner.beginComposite(canvas)) return;
    //   for (layer bottom→top)
    //     runner.drawCompositeLayer(tex, bx, by, bw, bh, flipH, flipV);
    //   runner.endComposite();

    bool beginComposite(QSize canvas);
    /// Upload a CPU frame into the shared scratch input texture (for a layer with
    /// no GPU-resident texture); returns its id to hand to drawCompositeLayer.
    unsigned int uploadScratch(const QImage &frame) { return uploadInput(frame); }
    /// @p bx,by,bw,bh are normalised canvas placement (y measured from the top).
    void drawCompositeLayer(unsigned int tex, float bx, float by, float bw, float bh,
                            bool flipH, bool flipV);
    void endComposite();

    unsigned int lastOutput() const { return m_lastTex; }
    QSize        size()       const { return m_size; }

    /// GPU→CPU read of the last output as RGBA8888 (for CPU effects chained after a
    /// GPU effect, or non-GL consumers). Self-contained: makes the context current
    /// for the read. Valid after a begin()/runPass…/end() cycle.
    QImage readback();

private:
    bool initGL();
    void destroyGL();
    bool ensureFbos(QSize size);
    void releaseHistory();
    void clearTexture(unsigned int tex, QSize size);

    QOffscreenSurface        *m_surface = nullptr;
    QOpenGLContext           *m_context = nullptr;
    QOpenGLFramebufferObject *m_fbo[2]  = {nullptr, nullptr};
    QOpenGLFramebufferObject *m_lastFbo = nullptr;
    unsigned int              m_inputTex = 0;
    unsigned int              m_vbo      = 0;
    QSize                     m_size;
    int                       m_ping     = 0;
    unsigned int              m_lastTex  = 0;
    bool                      m_glInit   = false;
    QString                   m_lastError;
    QHash<int, unsigned int> m_slotTex;
    unsigned int             m_transparentTex = 0;
    QHash<int, QOpenGLShaderProgram *> m_programs;
    QOpenGLShaderProgram     *m_compositeProg = nullptr;

    struct History {
        QSize frame;
        int   reqFrames = 0;
        bool  reqPrev = false;
        bool  warned = false;
        HistoryPlan  plan;
        unsigned int atlas = 0, prevTex = 0, fbo = 0;
        int   head = 0;
        qint64 charged = 0;
    } m_hist;
};
