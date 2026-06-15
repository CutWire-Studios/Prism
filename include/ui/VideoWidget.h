#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QImage>
#include <QHash>
#include <QPixmap>
#include <memory>
#include "core/MediaSource.h"
#include "core/OverlayItem.h"

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

    // Overlays drawn on top of the output
    void setOverlaysA(const QList<OverlayItem> &overlays);
    void setOverlaysB(const QList<OverlayItem> &overlays);

    bool   isPlayingA()      const { return m_playingA; }
    bool   isPlayingB()      const { return m_playingB; }
    double getCurrentTimeA() const;
    double getDurationA()    const;
    double getCurrentTimeB() const;
    double getDurationB()    const;

    // Crossfade mix: 0.0 = full A, 1.0 = full B
    void  setCrossfade(float mixB);
    float crossfade() const { return m_crossfadeB; }

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
    GLuint m_textureA = 0;
    GLuint m_textureB = 0;
    bool   m_playingA = false;
    bool   m_playingB = false;
    float  m_crossfadeB = 0.f;
    bool   m_repeatA = false;
    bool   m_repeatB = false;
    double m_trimStartA = 0.0,  m_trimEndA = -1.0;
    double m_trimStartB = 0.0,  m_trimEndB = -1.0;

    // Crop: normalised [0, 1]
    float m_cropXA = 0.f, m_cropYA = 0.f, m_cropWA = 1.f, m_cropHA = 1.f;
    float m_cropXB = 0.f, m_cropYB = 0.f, m_cropWB = 1.f, m_cropHB = 1.f;

    // Overlays
    QList<OverlayItem>       m_overlaysA;
    QList<OverlayItem>       m_overlaysB;
    QHash<QString, QPixmap>  m_overlayPixCache;

    QTimer *m_frameTimer = nullptr;
    QRectF  m_videoRectA;
    QRectF  m_videoRectB;

    // ── GL helpers ────────────────────────────────────────────────────────────
    void   setupTextureGL(GLuint &tex, QSize sz);
    // Takes tex by reference so it can recreate the texture if the frame size changes.
    void   uploadSourceFrameGL(GLuint &tex, MediaSource *source);
    QRectF computeVideoRect(QSize frameSize, float cx, float cy,
                            float cw, float ch) const;
    void   renderTexture(GLuint tex, float cx, float cy, float cw, float ch,
                         float dstX, float dstY, float dstW, float dstH);
    void   renderOverlays(QPainter &p, const QList<OverlayItem> &overlays,
                          const QRectF &videoRect, float globalAlpha);
    bool   advanceSource(MediaSource *source, bool &playing, bool repeat,
                         double trimStart, double trimEnd);
    void   loadSourceInternal(const QString &filePath,
                              std::unique_ptr<MediaSource> &target,
                              GLuint &tex, bool &playing);
};
