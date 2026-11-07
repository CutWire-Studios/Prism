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
#include "core/Deck.h"
#include "core/sources/MediaSource.h"
#include "app/output/ProgramFrameSource.h"
#include "app/transitions/Transition.h"

class QOffscreenSurface;
class QOpenGLContext;
class QPainter;
class QSurface;
class QTimer;

/// Widget-free deck compositor: composites the Live and Staging MediaSources through a Transition plus an
/// overlay chain into program/deck FBOs, and exposes the result as GL textures (for display
/// surfaces sharing its context group) and as a ProgramFrameSource (CPU readback for
/// preview/record/NDI). Renders on the GUI thread from its own frame timer, in a private
/// QOpenGLContext shared with QOpenGLContext::globalShareContext().
class Compositor : public QObject, public ProgramFrameSource, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit Compositor(QObject *parent = nullptr);
    ~Compositor() override;

    // ── Live/Staging deck API ──────────────────────────────────────────────────

    // Convenience loaders — detect video vs. static image automatically.
    void loadVideoLive(const QString &filePath);
    void loadVideoStaging(const QString &filePath);

    // Direct source injection — used by future source types (slideshow, camera…).
    void setSourceLive(std::unique_ptr<MediaSource> source);
    void setSourceStaging(std::unique_ptr<MediaSource> source);

    /// Drop the primary source and overlay chain for a deck (e.g. when its node is deleted).
    void clearDeckLive();
    void clearDeckStaging();

    /// Assign a deck source without GL texture upload (safe before `initializeGL`).
    void adoptSourceLive(std::unique_ptr<MediaSource> source);
    void adoptSourceStaging(std::unique_ptr<MediaSource> source);

    void playLive();
    void pauseLive();
    void playStaging();
    void pauseStaging();

    void seekLive(double seconds);
    void seekStaging(double seconds);

    void setRepeatLive(bool repeat) { m_repeatLive = repeat; }
    void setRepeatStaging(bool repeat) { m_repeatStaging = repeat; }

    // Playback rate for the deck's wall-clock pacing (1.0 = normal).
    void   setDeckSpeed(Deck deck, double speed);
    double deckSpeed(Deck deck) const { return isLive(deck) ? m_speedLive : m_speedStaging; }
    void setTrimPointsLive(double startSec, double endSec);
    void setTrimPointsStaging(double startSec, double endSec);

    // Spatial crop (normalised 0..1; default 0,0,1,1 = full frame)
    void setCropLive(float x, float y, float w, float h);
    void setCropStaging(float x, float y, float w, float h);

    // Base placement inside the canvas (normalised 0..1; default 0,0,1,1).
    void setBaseLive(float x, float y, float w, float h);
    void setBaseStaging(float x, float y, float w, float h);

    // Canvas size for transform context (0 = no context, use full window)
    void setCanvasSizeLive(int width, int height);
    void setCanvasSizeStaging(int width, int height);

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
    void setNodeChainLive(std::vector<NodeChainSource> chain);
    void setNodeChainStaging(std::vector<NodeChainSource> chain);

    // In-place layer edits that avoid rebuilding/re-decoding MediaSources.
    void reorderChain(Deck deck, const std::vector<int> &perm);

    // Re-renders a deck's primary source after its effect params changed in place, even if the
    // deck is paused (a paused deck is otherwise not advanced).
    void refreshDeckFrame(Deck deck);
    void setChainVisibility(Deck deck, int chainIndex, bool visible);
    void setChainPlacement(Deck deck, int chainIndex,
                           float cropX, float cropY, float cropW, float cropH,
                           bool flipH, bool flipV,
                           float baseX, float baseY, float baseW, float baseH,
                           bool visible);

    // Deck-primary flip (folded from process nodes on the base layer).
    void setFlipLive(bool flipH, bool flipV);
    void setFlipStaging(bool flipH, bool flipV);

    // Exchange all Live/Staging deck content (sources, textures, chains, placement, playback
    // clocks) so a clip can move between decks without re-decoding from disk.
    void swapDeckContents();

    // Starts the Out animation of every source on the deck (primary and overlay chain) and returns
    // the longest one in seconds; 0 when nothing animates out. The caller keeps the deck on air
    // for that long.
    double requestDeckOut(Deck deck);

    bool   isPlayingLive()      const { return m_playingLive; }
    bool   isPlayingStaging()      const { return m_playingStaging; }
    MediaSource *sourceLive()   const { return m_sourceLive.get(); }
    MediaSource *sourceStaging()   const { return m_sourceStaging.get(); }
    /// Pulls the first frame of a source built ahead of a deck (compiles shaders, decodes stills) in
    /// the compositor's GL context, as setSourceLive/Staging would on assignment.
    void primeSource(MediaSource *source);
    double getCurrentTimeLive() const;
    double getDurationLive()    const;
    double getCurrentTimeStaging() const;
    double getDurationStaging()    const;

    // Crossfade mix: 0.0 = Live fully on air, 1.0 = Staging fully on air
    void  setCrossfade(float mix);
    float crossfade() const { return m_crossfade; }

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

    // ── Program output (offscreen compositor) ─────────────────────────────────
    // The Live/Staging mix is rendered to a configurable-size FBO; the widget displays a
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
    GLuint deckTexture(Deck deck) const;
    QSize  programFrameSize()    const { return {s_programWidth, s_programHeight}; }
    QSize  programTextureSize()  const;
    QSize  deckTextureSize()     const { return programTextureSize(); }

    /// True when the compositor's context shares textures with the GUI/Qt Quick context group,
    /// so display surfaces may sample programTexture()/deckTexture() directly.
    bool texturesShareable() { return isValid() && m_sharesWithGui; }

    /// True when a GL context could be created (false on headless platforms without GL).
    bool glAvailable() { return isValid(); }

    /// Display surfaces register while visible. Without texture sharing the compositor reads
    /// back the program (source 0) or deck frames (1 = Live, 2 = Staging) for them; with it, nothing.
    void addViewer(int source);
    void removeViewer(int source);

    std::pair<float,float> computeDeckAlphas() const;
    /// Audio gains {Live, Staging} for a crossfade position: Cut switches at the midpoint, the Dip modes
    /// fall to silence there and rise again, everything else is equal-power (cos/sin).
    static std::pair<float,float> audioGains(TransitionMode mode, float mix);
    std::pair<float,float> computeAudioGains() const { return audioGains(m_transitionMode, m_crossfade); }
    QRectF videoRectProgram(Deck deck) const { return isLive(deck) ? m_videoRectProgramLive : m_videoRectProgramStaging; }

    /// Enable CPU readback of the program FBO for mirror outputs / recording.
    void addProgramFrameConsumer();
    void removeProgramFrameConsumer();
    void setProgramFrameConsumerCount(int count) override;
    QImage programFrame() const override { return m_programFrameCache; }

    /// Full-resolution deck iso frames (program resolution) for recording.
    void setDeckFrameConsumerCount(int count) override;
    QImage deckProgramFrame(Deck deck) const override;

    /// Read back the current program compositor frame (1280×720 RGBA).
    QImage captureProgramFrame();

    /// Compose and emit programFrameReady when mirror/NDI/recording consumers are active.
    void captureOutputFrameNow() override;

    /// Replace a deck base source or overlay chain entry with a still image.
    void holdLayerAsStill(Deck deck, int chainIndex, const QImage &frame);

    const std::vector<NodeChainSource> &chainSourcesLive() const { return m_chainLive; }
    const std::vector<NodeChainSource> &chainSourcesStaging() const { return m_chainStaging; }

    /// Render rate of the compositor tick (default 30).
    void setProgramFps(int fps);
    int  programFps() const { return m_programFps; }

    /// True when the deck's FBO feeds the program output at the current crossfade, mode and
    /// panic/single-stream state.
    bool deckContributesToProgram(Deck deck) const;
    /// Contributing, or a visible preview / iso recording wants its texture.
    bool deckNeedsRender(Deck deck) const;

    /// Stop timers and release live sources (safe to call before window teardown).
    void shutdownPlayback();

signals:
    /// A new program/deck frame was rendered into the textures.
    void frameReady();
    /// A frame was rendered for external readback consumers (mirror, NDI, recording).
    void programFrameReady();
    /// A deck's source presented its first frame (async loads), so the deck can go to air.
    void deckReady(Deck deck);
    /// Start of every render tick, before sources advance; drives time-based transitions.
    void frameTick();

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

    std::unique_ptr<MediaSource> m_sourceLive;
    std::unique_ptr<MediaSource> m_sourceStaging;
    GLuint m_textureLive       = 0;
    GLuint m_textureStaging       = 0;
    GLuint m_stayTunedTex = 0;
    QSize  m_stayTunedSize;
    bool   m_playingLive       = false;
    bool   m_playingStaging       = false;
    // Async video source whose first (or post-seek) frame has not arrived yet.
    bool   m_pendingFrameLive  = false;
    bool   m_pendingFrameStaging  = false;
    bool   m_readyLive         = false;
    bool   m_readyStaging         = false;
    float  m_crossfade = 0.f;
    bool   m_repeatLive = false;
    bool   m_repeatStaging = false;
    double m_trimStartLive = 0.0,  m_trimEndLive = -1.0;
    double m_trimStartStaging = 0.0,  m_trimEndStaging = -1.0;

    // Wall-clock playback pacing for the Live/Staging decks. The video timeline is driven
    // by elapsed real time (not one-frame-per-tick) so it tracks the real-time
    // audio regardless of the source's native frame rate. Re-anchored on
    // play/seek (m_clockDirty*).
    QElapsedTimer m_playClockLive, m_playClockStaging;
    double m_clockAnchorLive = 0.0, m_clockAnchorStaging = 0.0;
    bool   m_clockDirtyLive = true, m_clockDirtyStaging = true;
    double m_speedLive = 1.0, m_speedStaging = 1.0;

    // Crop: normalised [0, 1]
    float m_cropXLive = 0.f, m_cropYLive = 0.f, m_cropWLive = 1.f, m_cropHLive = 1.f;
    float m_cropXStaging = 0.f, m_cropYStaging = 0.f, m_cropWStaging = 1.f, m_cropHStaging = 1.f;

    // Deck-primary flip (folded from process nodes)
    bool m_flipHLive = false, m_flipVLive = false;
    bool m_flipHStaging = false, m_flipVStaging = false;

    // Base placement: normalised [0, 1]
    float m_baseXLive = 0.f, m_baseYLive = 0.f, m_baseWLive = 1.f, m_baseHLive = 1.f;
    float m_baseXStaging = 0.f, m_baseYStaging = 0.f, m_baseWStaging = 1.f, m_baseHStaging = 1.f;

    // Canvas size (from transform context node)
    int m_canvasWidthLive = 0, m_canvasHeightLive = 0;
    int m_canvasWidthStaging = 0, m_canvasHeightStaging = 0;


    // Tracks each source texture's allocated size so uploadSourceFrameGL can
    // detect resize without a per-frame glGet (which forces a GPU sync stall).
    QHash<GLuint, QSize>     m_texSizes;

    // Node chain sources (upstream overlays)
    std::vector<NodeChainSource> m_chainLive;
    std::vector<NodeChainSource> m_chainStaging;
    std::vector<GLuint>          m_chainTexLive;
    std::vector<GLuint>          m_chainTexStaging;

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
    // Wall-clock paced advance for the Live/Staging decks; falls back to advanceSource() for
    // live/static sources that have no real timeline.
    bool   advanceDeckPaced(Deck deck);
    void   loadSourceInternal(const QString &filePath,
                              std::unique_ptr<MediaSource> &target,
                              GLuint &tex, bool &playing, bool &pending);

    // Returns {alphaLive, alphaStaging} for the current crossfader position and mode.
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
    void renderDeckToFbo(Deck deck);
    void renderCompositionGL();
    void drawStayTunedSlate(int w, int h);
    void cacheProgramFrameFromFbo();
    void cacheDeckFrameFromFbo(Deck deck);

    // PBO ring behind the CPU frame caches (one frame of latency).
    struct Readback {
        GLuint pbo[2] = {0, 0};
        int    next = 0;
        bool   pending[2] = {false, false};
        QSize  size;
    };
    static int frameIntervalMs(int fps);
    void readbackToCache(Readback &rb, QImage &cache, bool flipRows);
    static void resetReadback(Readback &rb);
    Readback m_readbackProgram, m_readbackDeckLive, m_readbackDeckStaging;
    bool   m_syncReadback  = false;
    bool   m_holdContext   = false;
    bool   m_contextHeld   = false;
    int    m_programFps    = 30;

    static int s_programWidth;
    static OutputScaleMode s_outputScaleMode;
    static int s_programHeight;

    GLuint m_programFbo      = 0;
    GLuint m_programColorTex = 0;
    GLuint m_deckFboLive        = 0;
    GLuint m_deckColorTexLive   = 0;
    GLuint m_deckFboStaging        = 0;
    GLuint m_deckColorTexStaging   = 0;
    int    m_compW           = 0;
    int    m_compH           = 0;
    int    m_programFrameConsumers = 0;
    int    m_deckFrameConsumers    = 0;
    QImage m_programFrameCache;
    QImage m_deckFrameCacheLive;
    QImage m_deckFrameCacheStaging;
    QRectF m_videoRectProgramLive;
    QRectF m_videoRectProgramStaging;

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
