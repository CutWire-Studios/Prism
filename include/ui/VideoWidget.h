#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QImage>
#include <QHash>
#include <QPixmap>
#include <memory>
#include <vector>
#include "core/MediaSource.h"
#include "core/OverlayItem.h"
#include "ui/Transition.h"

class VideoWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget();

    // ── Dual-source A/B API ──────────────────────────────────────────────────

    // Convenience loaders — detect video vs. static image automatically.
    void loadVideoA(const QString &filePath);
    void loadVideoB(const QString &filePath);

    // Direct source injection — used by future source types (slideshow, camera…).
    void setSourceA(std::unique_ptr<MediaSource> source);
    void setSourceB(std::unique_ptr<MediaSource> source);

    // HTML overlay composited on top of the A/B crossfade (RGBA, alpha-blended).
    void setHtmlOverlay(std::unique_ptr<MediaSource> source);
    void clearHtmlOverlay();

    void playA();
    void pauseA();
    void playB();
    void pauseB();

    void seekA(double seconds);
    void seekB(double seconds);

    void setRepeatA(bool repeat) { m_repeatA = repeat; }
    void setRepeatB(bool repeat) { m_repeatB = repeat; }
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

    // Overlays drawn on top of the output
    void setOverlaysA(const QList<OverlayItem> &overlays);
    void setOverlaysB(const QList<OverlayItem> &overlays);

    // ── Node chain compositing ────────────────────────────────────────────────
    // Sources from upstream nodes are rendered on top of the base deck source,
    // in order (index 0 = directly above base, last index = topmost).
    struct NodeChainSource {
        std::unique_ptr<MediaSource> source;
        float cropX = 0.f, cropY = 0.f, cropW = 1.f, cropH = 1.f;
        float baseX = 0.f, baseY = 0.f, baseW = 1.f, baseH = 1.f;
        bool  playing = false;   // whether to call nextFrame() each tick
        int   canvasWidth = 0, canvasHeight = 0;  // transform context canvas size (0 = no context)
    };
    void setNodeChainA(std::vector<NodeChainSource> chain);
    void setNodeChainB(std::vector<NodeChainSource> chain);

    bool   isPlayingA()      const { return m_playingA; }
    bool   isPlayingB()      const { return m_playingB; }
    double getCurrentTimeA() const;
    double getDurationA()    const;
    double getCurrentTimeB() const;
    double getDurationB()    const;

    // Crossfade mix: 0.0 = full A, 1.0 = full B
    void  setCrossfade(float mixB);
    float crossfade() const { return m_crossfadeB; }

    // ── Transition mode ───────────────────────────────────────────────────────
    // Transition strategies live in Transition.h; the enum is aliased here so
    // existing call sites can keep using VideoWidget::TransitionMode.
    using TransitionMode = ::TransitionMode;
    void           setTransitionMode(TransitionMode mode) { m_transitionMode = mode; update(); }
    TransitionMode transitionMode() const { return m_transitionMode; }

    // Current frame as QImage for deck preview labels
    QImage getFrameA() const;
    QImage getFrameB() const;

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

    QSize sizeHint() const override { return QSize(1280, 720); }

    // ── Program output (offscreen compositor) ─────────────────────────────────
    // The A/B mix is rendered to a fixed-size FBO; the widget displays a scaled
    // blit. Future outputs (NDI, recording, second monitor) read the same FBO.
    static constexpr int kProgramWidth  = 1280;
    static constexpr int kProgramHeight = 720;

    GLuint programColorTexture() const { return m_programColorTex; }
    QSize  programFrameSize()    const { return {kProgramWidth, kProgramHeight}; }

    /// Enable CPU readback of the program FBO for mirror outputs / recording.
    void addProgramFrameConsumer();
    void removeProgramFrameConsumer();
    void setProgramFrameConsumerCount(int count);
    QImage programFrame() const { return m_programFrameCache; }

signals:
    void programFrameReady();

protected:
    void initializeGL()           override;
    void resizeGL(int w, int h)   override;
    void paintGL()                override;
    void paintEvent(QPaintEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *)   override;

private slots:
    void updateFrame();

private:
    std::unique_ptr<MediaSource> m_sourceA;
    std::unique_ptr<MediaSource> m_sourceB;
    std::unique_ptr<MediaSource> m_htmlOverlay;
    GLuint m_textureA       = 0;
    GLuint m_textureB       = 0;
    GLuint m_textureOverlay = 0;
    bool   m_playingA       = false;
    bool   m_playingB       = false;
    bool   m_playingOverlay = false;
    float  m_crossfadeB = 0.f;
    // Which deck is the incoming one. Committed when the fader leaves an end
    // stop so directional transitions animate correctly in both directions
    // and mid-fader reversals simply rewind instead of swapping roles.
    bool   m_transitionTowardB = true;
    bool   m_repeatA = false;
    bool   m_repeatB = false;
    double m_trimStartA = 0.0,  m_trimEndA = -1.0;
    double m_trimStartB = 0.0,  m_trimEndB = -1.0;

    // Crop: normalised [0, 1]
    float m_cropXA = 0.f, m_cropYA = 0.f, m_cropWA = 1.f, m_cropHA = 1.f;
    float m_cropXB = 0.f, m_cropYB = 0.f, m_cropWB = 1.f, m_cropHB = 1.f;

    // Base placement: normalised [0, 1]
    float m_baseXA = 0.f, m_baseYA = 0.f, m_baseWA = 1.f, m_baseHA = 1.f;
    float m_baseXB = 0.f, m_baseYB = 0.f, m_baseWB = 1.f, m_baseHB = 1.f;

    // Canvas size (from transform context node)
    int m_canvasWidthA = 0, m_canvasHeightA = 0;
    int m_canvasWidthB = 0, m_canvasHeightB = 0;

    // Overlays
    QList<OverlayItem>       m_overlaysA;
    QList<OverlayItem>       m_overlaysB;
    QHash<QString, QPixmap>  m_overlayPixCache;

    // Node chain sources (upstream overlays)
    std::vector<NodeChainSource> m_chainA;
    std::vector<NodeChainSource> m_chainB;
    std::vector<GLuint>          m_chainTexA;
    std::vector<GLuint>          m_chainTexB;

    TransitionMode m_transitionMode = TransitionMode::Crossfade;

    QTimer *m_frameTimer = nullptr;
    QRectF  m_videoRectA;
    QRectF  m_videoRectB;

    // ── GL helpers ────────────────────────────────────────────────────────────
    void   setupTextureGL(GLuint &tex, QSize sz, bool alpha = false);
    // Takes tex by reference so it can recreate the texture if the frame size changes.
    void   uploadSourceFrameGL(GLuint &tex, MediaSource *source);
    QRectF computeContainedRect(QSize frameSize, float cw, float ch,
                                const QRectF &bounds) const;
    void   renderTexture(GLuint tex, float cx, float cy, float cw, float ch,
                         float dstX, float dstY, float dstW, float dstH);
    void   renderOverlays(QPainter &p, const QList<OverlayItem> &overlays,
                          const QRectF &videoRect, float globalAlpha);
    bool   advanceSource(MediaSource *source, bool &playing, bool repeat,
                         double trimStart, double trimEnd);
    void   loadSourceInternal(const QString &filePath,
                              std::unique_ptr<MediaSource> &target,
                              GLuint &tex, bool &playing);

    // Returns {alphaA, alphaB} for the current crossfader position and mode.
    // Used by both paintGL() and paintEvent() to keep overlay rendering consistent.
    std::pair<float,float> computeDeckAlphas() const;

    void clearChainTextures(std::vector<GLuint> &texList);
    void primeChainSources(std::vector<NodeChainSource> &chain,
                           std::vector<GLuint> &texList);
    void drawChainSources(std::vector<NodeChainSource> &chain,
                          std::vector<GLuint> &texList, float alpha,
                          int canvasW, int canvasH);
    void advanceChainSources(std::vector<NodeChainSource> &chain,
                             std::vector<GLuint> &texList, bool &anyDecoded);

    // Composition dimensions — set during FBO render, fallback to widget size.
    int renderW() const { return m_compW > 0 ? m_compW : width(); }
    int renderH() const { return m_compH > 0 ? m_compH : height(); }

    void ensureProgramFbo();
    void destroyProgramFbo();
    void renderCompositionGL();
    void blitProgramToScreen();
    void cacheProgramFrameFromFbo();
    QRectF scaleRectToWidget(const QRectF &programRect) const;

    GLuint m_programFbo      = 0;
    GLuint m_programColorTex = 0;
    int    m_compW           = 0;
    int    m_compH           = 0;
    int    m_programFrameConsumers = 0;
    QImage m_programFrameCache;
};
