#pragma once

#include <QObject>
#include <QOpenGLFunctions>
#include <QImage>
#include <QHash>
#include <QPixmap>
#include <QElapsedTimer>
#include <QSize>
#include <memory>
#include <vector>
#include "core/sources/MediaSource.h"
#include "app/output/ProgramFrameSource.h"
#include "app/transitions/Transition.h"

class QOffscreenSurface;
class QOpenGLContext;
class QPainter;
class QSurface;
class QTimer;

/// Widget-free deck compositor: composites two A/B MediaSources through a Transition plus an
/// overlay chain into program/deck FBOs, and exposes the result as GL textures (for display
/// surfaces sharing its context group) and as a ProgramFrameSource (CPU readback for
/// preview/record/NDI). Renders on the GUI thread from its own frame timer, in a private
/// QOpenGLContext shared with QOpenGLContext::globalShareContext().
class Compositor : public QObject, public ProgramFrameSource, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit Compositor(QObject *parent = nullptr);
    ~Compositor() override;

    // ── Dual-source A/B API ──────────────────────────────────────────────────

    // Convenience loaders — detect video vs. static image automatically.
    void loadVideoA(const QString &filePath);
    void loadVideoB(const QString &filePath);

    // Direct source injection — used by future source types (slideshow, camera…).
    void setSourceA(std::unique_ptr<MediaSource> source);
    void setSourceB(std::unique_ptr<MediaSource> source);

    /// Drop the primary source and overlay chain for a deck (e.g. when its node is deleted).
    void clearDeckA();
    void clearDeckB();

    /// Assign a deck source without GL texture upload (safe before `initializeGL`).
    void adoptSourceA(std::unique_ptr<MediaSource> source);
    void adoptSourceB(std::unique_ptr<MediaSource> source);

    void playA();
    void pauseA();
    void playB();
    void pauseB();

    void seekA(double seconds);
    void seekB(double seconds);

    void setRepeatA(bool repeat) { m_repeatA = repeat; }
    void setRepeatB(bool repeat) { m_repeatB = repeat; }

    // Playback rate for the deck's wall-clock pacing (1.0 = normal).
    void   setDeckSpeed(bool deckA, double speed);
    double deckSpeed(bool deckA) const { return deckA ? m_speedA : m_speedB; }
    void setTrimPointsA(double startSec, double endSec);
    void setTrimPointsB(double startSec, double endSec);

    // Spatial crop (normalised 0..1; default 0,0,1,1 = full frame)
    void setCropA(float x, float y, float w, float h);
    void setCropB(float x, float y, float w, float h);

    // Base placement inside the canvas (normalised 0..1; default 0,0,1,1).
    void setBaseA(float x, float y, float w, float h);
    void setBaseB(float x, float y, float w, float h);

    // Canvas size for transform context (0 = no context, use full window)
    void setCanvasSizeA(int width, int height);
    void setCanvasSizeB(int width, int height);

    // ── Node chain compositing ────────────────────────────────────────────────
    // Sources from upstream nodes are rendered on top of the base deck source,
    // in order (index 0 = directly above base, last index = topmost).
    struct NodeChainSource {
        std::unique_ptr<MediaSource> source;
        float cropX = 0.f, cropY = 0.f, cropW = 1.f, cropH = 1.f;
        bool  flipH = false, flipV = false;
        float baseX = 0.f, baseY = 0.f, baseW = 1.f, baseH = 1.f;
        bool  playing = false;   // whether to advance the source each tick
        QElapsedTimer clock;     // play clock for sources with a timeline
        double anchor = 0.0;     // source time at clock start
        bool  visible = true;    // Layer-node eye toggle (hidden layers are skipped, still advanced)
        int   canvasWidth = 0, canvasHeight = 0;  // canvas size from Layer node (0 = no canvas)
    };
    void setNodeChainA(std::vector<NodeChainSource> chain);
    void setNodeChainB(std::vector<NodeChainSource> chain);

    // In-place layer edits that avoid rebuilding/re-decoding MediaSources.
    void reorderChain(bool deckA, const std::vector<int> &perm);
    void setChainVisibility(bool deckA, int chainIndex, bool visible);
    void setChainPlacement(bool deckA, int chainIndex,
                           float cropX, float cropY, float cropW, float cropH,
                           bool flipH, bool flipV,
                           float baseX, float baseY, float baseW, float baseH,
                           bool visible);

    // Deck-primary flip (folded from process nodes on the base layer).
    void setFlipA(bool flipH, bool flipV);
    void setFlipB(bool flipH, bool flipV);

    // Single-stream output: show deck A directly with no crossfade/transition.
    void setSingleStreamMode(bool enabled);
    bool singleStreamMode() const { return m_singleStream; }

    // Exchange all A/B deck content (sources, textures, chains, placement, playback
    // clocks) so a clip can move between decks without re-decoding from disk.
    void swapDeckContents();

    // Starts the Out animation of every source on the deck (primary and overlay chain) and returns
    // the longest one in seconds; 0 when nothing animates out. The caller keeps the deck on air
    // for that long.
    double requestDeckOut(bool deckA);

    bool   isPlayingA()      const { return m_playingA; }
    bool   isPlayingB()      const { return m_playingB; }
    MediaSource *sourceA()   const { return m_sourceA.get(); }
    MediaSource *sourceB()   const { return m_sourceB.get(); }
    double getCurrentTimeA() const;
    double getDurationA()    const;
    double getCurrentTimeB() const;
    double getDurationB()    const;

    // Crossfade mix: 0.0 = full A, 1.0 = full B
    void  setCrossfade(float mixB);
    float crossfade() const { return m_crossfadeB; }

    // ── Panic / emergency output controls ─────────────────────────────────────
    enum class PanicOverlay { None, Blackout, StayTuned };

    void setPanicOverlay(PanicOverlay overlay);
    PanicOverlay panicOverlay() const { return m_panicOverlay; }

    /// Freeze program output on the current frame (does not change deck play state).
    void setOutputFrozen(bool frozen);
    bool isOutputFrozen() const { return m_outputFrozen; }

    // ── Transition mode ───────────────────────────────────────────────────────
    // Transition strategies live in Transition.h; the enum is aliased here so
    // call sites can keep using Compositor::TransitionMode.
    using TransitionMode = ::TransitionMode;
    void           setTransitionMode(TransitionMode mode) { m_transitionMode = mode; markDirty(); }
    TransitionMode transitionMode() const { return m_transitionMode; }

    // ── Backward-compat single-source API (delegates to source A) ────────────
    void   loadVideo(const QString &path)    { loadVideoA(path); }
    void   play()                            { playA(); }
    void   pause()                           { pauseA(); }
    void   stop();
    void   seek(double s)                    { seekA(s); }
    void   setRepeat(bool r)                 { setRepeatA(r); }
    void   setTrimPoints(double s, double e) { setTrimPointsA(s, e); }
    bool   isPlaying()    const              { return m_playingA; }
    double getCurrentTime() const            { return getCurrentTimeA(); }
    double getDuration()    const            { return getDurationA(); }

    // ── Program output (offscreen compositor) ─────────────────────────────────
    // The A/B mix is rendered to a configurable-size FBO; the widget displays a
    // scaled blit. Future outputs (NDI, recording, second monitor) read the same FBO.
    static int programWidth()  { return s_programWidth; }
    static int programHeight() { return s_programHeight; }
    /// Change the compositor's render resolution, reallocating the program/deck FBOs.
    void setProgramResolution(int width, int height);

    enum class OutputScaleMode { Stretch, Fit, Crop };
    static OutputScaleMode outputScaleMode() { return s_outputScaleMode; }
    void setOutputScaleMode(OutputScaleMode mode);
    /// Where the whole program frame lands on a surface of the given size.
    static QRectF programRectOnSurface(const QSizeF &surface, const QSizeF &program, OutputScaleMode mode);

    GLuint programColorTexture() const { return m_programColorTex; }
    GLuint programTexture()      const { return m_programColorTex; }
    /// Deck preview FBO texture (GL-upright, row 0 is the image bottom).
    GLuint deckTexture(bool deckA) const;
    QSize  programFrameSize()    const { return {s_programWidth, s_programHeight}; }
    QSize  programTextureSize()  const;
    QSize  deckTextureSize()     const { return programTextureSize(); }

    /// True when the compositor's context shares textures with the GUI/Qt Quick context group,
    /// so display surfaces may sample programTexture()/deckTexture() directly.
    bool texturesShareable() { return isValid() && m_sharesWithGui; }

    /// True when a GL context could be created (false on headless platforms without GL).
    bool glAvailable() { return isValid(); }

    /// Display surfaces register while visible. Without texture sharing the compositor reads
    /// back the program (source 0) or deck frames (1 = A, 2 = B) for them; with it, nothing.
    void addViewer(int source);
    void removeViewer(int source);

    std::pair<float,float> computeDeckAlphas() const;
    QRectF videoRectProgram(bool deckA) const { return deckA ? m_videoRectProgramA : m_videoRectProgramB; }

    /// Enable CPU readback of the program FBO for mirror outputs / recording.
    void addProgramFrameConsumer();
    void removeProgramFrameConsumer();
    void setProgramFrameConsumerCount(int count) override;
    QImage programFrame() const override { return m_programFrameCache; }

    /// Full-resolution deck iso frames (program resolution) for recording.
    void setDeckFrameConsumerCount(int count) override;
    QImage deckProgramFrame(bool deckA) const override;

    /// Read back the current program compositor frame (1280×720 RGBA).
    QImage captureProgramFrame();

    /// Compose and emit programFrameReady when mirror/NDI/recording consumers are active.
    void captureOutputFrameNow() override;

    /// Replace a deck base source or overlay chain entry with a still image.
    void holdLayerAsStill(bool deckA, int chainIndex, const QImage &frame);

    const std::vector<NodeChainSource> &chainSourcesA() const { return m_chainA; }
    const std::vector<NodeChainSource> &chainSourcesB() const { return m_chainB; }

    /// Stop timers and release live sources (safe to call before window teardown).
    void shutdownPlayback();

signals:
    /// A new program/deck frame was rendered into the textures.
    void frameReady();
    /// A frame was rendered for external readback consumers (mirror, NDI, recording).
    void programFrameReady();

private slots:
    void updateFrame();

private:
    /// Stop live sources before GL context teardown (~Compositor ordering).
    void releaseMediaSources();

    void markDirty() { m_dirty = true; }
    /// Lazily creates the shared context and offscreen surface; false when none is available.
    bool isValid();
    /// Nesting-aware; restores whatever context was current before the outermost call.
    bool makeCurrent();
    void doneCurrent();
    bool renderNow();
    bool wantsProgramFrame() const;
    bool wantsDeckFrame() const;

    /// Render program/deck FBOs and optional CPU readback (mirror, NDI, recording).
    void composeProgramFrame();

    std::unique_ptr<MediaSource> m_sourceA;
    std::unique_ptr<MediaSource> m_sourceB;
    GLuint m_textureA       = 0;
    GLuint m_textureB       = 0;
    bool   m_playingA       = false;
    bool   m_playingB       = false;
    float  m_crossfadeB = 0.f;
    // Which deck is the incoming one. Committed when the fader leaves an end
    // stop so directional transitions animate correctly in both directions
    // and mid-fader reversals simply rewind instead of swapping roles.
    bool   m_transitionTowardB = true;
    bool   m_repeatA = false;
    bool   m_repeatB = false;
    double m_trimStartA = 0.0,  m_trimEndA = -1.0;
    double m_trimStartB = 0.0,  m_trimEndB = -1.0;

    // Wall-clock playback pacing for the A/B decks. The video timeline is driven
    // by elapsed real time (not one-frame-per-tick) so it tracks the real-time
    // audio regardless of the source's native frame rate. Re-anchored on
    // play/seek (m_clockDirty*).
    QElapsedTimer m_playClockA, m_playClockB;
    double m_clockAnchorA = 0.0, m_clockAnchorB = 0.0;
    bool   m_clockDirtyA = true, m_clockDirtyB = true;
    double m_speedA = 1.0, m_speedB = 1.0;

    // Crop: normalised [0, 1]
    float m_cropXA = 0.f, m_cropYA = 0.f, m_cropWA = 1.f, m_cropHA = 1.f;
    float m_cropXB = 0.f, m_cropYB = 0.f, m_cropWB = 1.f, m_cropHB = 1.f;

    // Deck-primary flip (folded from process nodes)
    bool m_flipHA = false, m_flipVA = false;
    bool m_flipHB = false, m_flipVB = false;

    // Single-stream output mode (deck A only, no crossfade)
    bool m_singleStream = false;

    // Base placement: normalised [0, 1]
    float m_baseXA = 0.f, m_baseYA = 0.f, m_baseWA = 1.f, m_baseHA = 1.f;
    float m_baseXB = 0.f, m_baseYB = 0.f, m_baseWB = 1.f, m_baseHB = 1.f;

    // Canvas size (from transform context node)
    int m_canvasWidthA = 0, m_canvasHeightA = 0;
    int m_canvasWidthB = 0, m_canvasHeightB = 0;


    // Tracks each source texture's allocated size so uploadSourceFrameGL can
    // detect resize without a per-frame glGet (which forces a GPU sync stall).
    QHash<GLuint, QSize>     m_texSizes;

    // Node chain sources (upstream overlays)
    std::vector<NodeChainSource> m_chainA;
    std::vector<NodeChainSource> m_chainB;
    std::vector<GLuint>          m_chainTexA;
    std::vector<GLuint>          m_chainTexB;

    TransitionMode m_transitionMode = TransitionMode::Crossfade;

    PanicOverlay m_panicOverlay = PanicOverlay::None;
    bool         m_outputFrozen = false;

    QTimer *m_frameTimer = nullptr;

    // ── GL helpers ────────────────────────────────────────────────────────────
    void   setupTextureGL(GLuint &tex, QSize sz, bool alpha = false);
    // Takes tex by reference so it can recreate the texture if the frame size changes.
    void   uploadSourceFrameGL(GLuint &tex, MediaSource *source);
    void   renderTexture(GLuint tex, float cx, float cy, float cw, float ch,
                         float dstX, float dstY, float dstW, float dstH,
                         bool flipH = false, bool flipV = false);
    void   renderFboTexture(GLuint tex, float dstX, float dstY, float dstW, float dstH,
                            float alpha);
    bool   advanceSource(MediaSource *source, bool &playing, bool repeat,
                         double trimStart, double trimEnd);
    // Wall-clock paced advance for deck A/B; falls back to advanceSource() for
    // live/static sources that have no real timeline.
    bool   advanceDeckPaced(bool deckA);
    void   loadSourceInternal(const QString &filePath,
                              std::unique_ptr<MediaSource> &target,
                              GLuint &tex, bool &playing);

    // Returns {alphaA, alphaB} for the current crossfader position and mode.
    // Used by both paintGL() and paintEvent() to keep overlay rendering consistent.
    void syncOnAir();

    void clearChainTextures(std::vector<GLuint> &texList);
    void primeChainSources(std::vector<NodeChainSource> &chain,
                           std::vector<GLuint> &texList);
    void drawChainSources(std::vector<NodeChainSource> &chain,
                          std::vector<GLuint> &texList, float alpha,
                          int canvasW, int canvasH);
    void advanceChainSources(std::vector<NodeChainSource> &chain,
                             std::vector<GLuint> &texList, bool &anyDecoded);

    // Composition dimensions — set during FBO render, fallback to the program size.
    int renderW() const { return m_compW > 0 ? m_compW : s_programWidth; }
    int renderH() const { return m_compH > 0 ? m_compH : s_programHeight; }

    void ensureProgramFbo();
    void ensureDeckFbos();
    void destroyProgramFbo();
    void renderDeckToFbo(bool deckA);
    void renderCompositionGL();
    void cacheProgramFrameFromFbo();
    void cacheDeckFrameFromFbo(bool deckA);

    static int s_programWidth;
    static OutputScaleMode s_outputScaleMode;
    static int s_programHeight;

    GLuint m_programFbo      = 0;
    GLuint m_programColorTex = 0;
    GLuint m_deckFboA        = 0;
    GLuint m_deckColorTexA   = 0;
    GLuint m_deckFboB        = 0;
    GLuint m_deckColorTexB   = 0;
    int    m_compW           = 0;
    int    m_compH           = 0;
    int    m_programFrameConsumers = 0;
    int    m_deckFrameConsumers    = 0;
    QImage m_programFrameCache;
    QImage m_deckFrameCacheA;
    QImage m_deckFrameCacheB;
    QRectF m_videoRectProgramA;
    QRectF m_videoRectProgramB;

    std::unique_ptr<QOffscreenSurface> m_surface;
    std::unique_ptr<QOpenGLContext>    m_ctx;
    QOpenGLContext *m_prevContext = nullptr;
    QSurface       *m_prevSurface = nullptr;
    int    m_ctxDepth      = 0;
    bool   m_glReady       = false;
    bool   m_contextFailed = false;
    bool   m_sharesWithGui = false;
    bool   m_dirty         = true;
    int    m_viewers[3]    = {0, 0, 0};
};
