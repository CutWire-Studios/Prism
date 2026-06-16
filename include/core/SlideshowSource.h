#pragma once

#include "core/MediaSource.h"
#include <QImage>
#include <QElapsedTimer>
#include <QByteArray>
#include <QStringList>

class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;
class QOpenGLShaderProgram;

// MediaSource that cycles through a folder (or list) of static images.
//
// Slide file paths are stored on load; only the current slide (and from/to pair
// during a transition) are decoded at once. Images are normalised to the first
// slide's pixel size so GL textures stay the same dimensions.
// nextFrame() advances the slide when enough time has passed (intervalMs);
// VideoWidget's 33 ms frame timer drives the polling — no extra QTimer needed.

class SlideshowSource : public MediaSource {
public:
    enum class Effect {
        None = 0,
        Cube3D,
        Flip3D,
        PageCurl,
        Doorway,
        CrossZoom,
        Ripple,
        Glitch,
        MosaicFlip,
        Swirl,
    };

    SlideshowSource() = default;
    ~SlideshowSource() override;

    // Load all supported images from a folder, sorted by filename.
    bool loadFolder(const QString &folderPath, int intervalMs = 3000);

    // Load an explicit list of image file paths.
    bool loadFiles(const QStringList &filePaths, int intervalMs = 3000);

    void setIntervalMs(int ms) { m_intervalMs = ms; }
    int  intervalMs()    const { return m_intervalMs; }
    int  count()         const { return m_paths.size(); }
    int  currentIndex()  const { return m_current; }

    void   setEffect(Effect e);
    Effect effect()           const { return m_effect; }
    void   setTransitionMs(int ms) { m_transitionMs = ms; }
    int    transitionMs()     const { return m_transitionMs; }

    static QStringList effectNames();
    static int         effectCount() { return static_cast<int>(Effect::Swirl) + 1; }

    Type    type()        const override { return Type::Slideshow; }
    bool    isReady()     const override {
        if (m_paths.isEmpty()) return false;
        if (m_transitioning && m_effect != Effect::None && !m_buffer.isEmpty())
            return true;
        return !m_currentSlide.isNull();
    }
    QSize   frameSize()   const override { return m_frameSize; }
    const uint8_t *frameData() const override;

    // Returns true when the slide advances (triggers a GL texture re-upload).
    bool    nextFrame()         override;

    QString displayName() const override { return m_name; }

private:
    bool initGL();
    void destroyGL();
    bool compileEffect(Effect effect);
    void uploadSlideTexture(unsigned int tex, const QImage &img);
    void renderTransition(float progress);
    QImage loadSlideImage(const QString &path) const;
    static QString effectShaderPath(Effect effect);

    QStringList m_paths;         // sorted image file paths
    QImage      m_currentSlide;  // Format_RGB888, decoded current frame
    QImage      m_fromSlide;     // outgoing slide during transition
    QImage      m_toSlide;       // incoming slide during transition
    QSize         m_frameSize;
    int           m_current    = 0;
    int           m_intervalMs = 3000;
    QElapsedTimer m_elapsed;
    QString       m_name;

    Effect m_effect         = Effect::None;
    int    m_transitionMs   = 800;
    int    m_prevIndex        = 0;
    QElapsedTimer m_transElapsed;
    bool   m_transitioning    = false;

    QByteArray m_buffer;
    QByteArray m_rgbaScratch;

    QOffscreenSurface        *m_surface = nullptr;
    QOpenGLContext           *m_context = nullptr;
    QOpenGLFramebufferObject *m_fbo     = nullptr;
    QOpenGLShaderProgram     *m_program = nullptr;
    unsigned int m_vbo      = 0;
    unsigned int m_texFrom  = 0;
    unsigned int m_texTo    = 0;
    bool m_glInitialized    = false;
    bool m_compiled         = false;
    Effect m_compiledEffect = Effect::None;
};
