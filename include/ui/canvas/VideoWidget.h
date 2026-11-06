#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QImage>
#include <memory>
#include <vector>
#include "core/render/Compositor.h"

/// Widgets-UI shim over Compositor: all compositing lives in the Compositor this widget owns;
/// the widget only draws its program texture, paints the QPainter overlays/panic text on top,
/// and handles frameless-window chrome, drag/drop and the context menu.
class VideoWidget : public QOpenGLWidget, public ProgramFrameSource, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget();

    using NodeChainSource  = Compositor::NodeChainSource;
    using PanicOverlay     = Compositor::PanicOverlay;
    using TransitionMode   = Compositor::TransitionMode;
    using OutputScaleMode  = Compositor::OutputScaleMode;

    Compositor *compositor() const { return m_comp; }

    void loadVideoA(const QString &filePath) { m_comp->loadVideoA(filePath); }
    void loadVideoB(const QString &filePath) { m_comp->loadVideoB(filePath); }
    void setSourceA(std::unique_ptr<MediaSource> source) { m_comp->setSourceA(std::move(source)); }
    void setSourceB(std::unique_ptr<MediaSource> source) { m_comp->setSourceB(std::move(source)); }
    void clearDeckA() { m_comp->clearDeckA(); }
    void clearDeckB() { m_comp->clearDeckB(); }
    void adoptSourceA(std::unique_ptr<MediaSource> source) { m_comp->adoptSourceA(std::move(source)); }
    void adoptSourceB(std::unique_ptr<MediaSource> source) { m_comp->adoptSourceB(std::move(source)); }

    void playA()  { m_comp->playA(); }
    void pauseA() { m_comp->pauseA(); }
    void playB()  { m_comp->playB(); }
    void pauseB() { m_comp->pauseB(); }
    void seekA(double seconds) { m_comp->seekA(seconds); }
    void seekB(double seconds) { m_comp->seekB(seconds); }
    void setRepeatA(bool repeat) { m_comp->setRepeatA(repeat); }
    void setRepeatB(bool repeat) { m_comp->setRepeatB(repeat); }
    void   setDeckSpeed(bool deckA, double speed) { m_comp->setDeckSpeed(deckA, speed); }
    double deckSpeed(bool deckA) const { return m_comp->deckSpeed(deckA); }
    void setTrimPointsA(double startSec, double endSec) { m_comp->setTrimPointsA(startSec, endSec); }
    void setTrimPointsB(double startSec, double endSec) { m_comp->setTrimPointsB(startSec, endSec); }

    void setCropA(float x, float y, float w, float h) { m_comp->setCropA(x, y, w, h); }
    void setCropB(float x, float y, float w, float h) { m_comp->setCropB(x, y, w, h); }
    void setBaseA(float x, float y, float w, float h) { m_comp->setBaseA(x, y, w, h); }
    void setBaseB(float x, float y, float w, float h) { m_comp->setBaseB(x, y, w, h); }
    void setCanvasSizeA(int width, int height) { m_comp->setCanvasSizeA(width, height); }
    void setCanvasSizeB(int width, int height) { m_comp->setCanvasSizeB(width, height); }
    void setOverlaysA(const QList<OverlayItem> &overlays) { m_comp->setOverlaysA(overlays); }
    void setOverlaysB(const QList<OverlayItem> &overlays) { m_comp->setOverlaysB(overlays); }

    void setNodeChainA(std::vector<NodeChainSource> chain) { m_comp->setNodeChainA(std::move(chain)); }
    void setNodeChainB(std::vector<NodeChainSource> chain) { m_comp->setNodeChainB(std::move(chain)); }
    void reorderChain(bool deckA, const std::vector<int> &perm) { m_comp->reorderChain(deckA, perm); }
    void setChainVisibility(bool deckA, int chainIndex, bool visible) {
        m_comp->setChainVisibility(deckA, chainIndex, visible);
    }
    void setChainPlacement(bool deckA, int chainIndex,
                           float cropX, float cropY, float cropW, float cropH,
                           bool flipH, bool flipV,
                           float baseX, float baseY, float baseW, float baseH,
                           bool visible) {
        m_comp->setChainPlacement(deckA, chainIndex, cropX, cropY, cropW, cropH, flipH, flipV,
                                  baseX, baseY, baseW, baseH, visible);
    }

    void setFlipA(bool flipH, bool flipV) { m_comp->setFlipA(flipH, flipV); }
    void setFlipB(bool flipH, bool flipV) { m_comp->setFlipB(flipH, flipV); }
    void setSingleStreamMode(bool enabled) { m_comp->setSingleStreamMode(enabled); }
    bool singleStreamMode() const { return m_comp->singleStreamMode(); }
    void swapDeckContents() { m_comp->swapDeckContents(); }
    double requestDeckOut(bool deckA) { return m_comp->requestDeckOut(deckA); }

    bool   isPlayingA()      const { return m_comp->isPlayingA(); }
    bool   isPlayingB()      const { return m_comp->isPlayingB(); }
    MediaSource *sourceA()   const { return m_comp->sourceA(); }
    MediaSource *sourceB()   const { return m_comp->sourceB(); }
    double getCurrentTimeA() const { return m_comp->getCurrentTimeA(); }
    double getDurationA()    const { return m_comp->getDurationA(); }
    double getCurrentTimeB() const { return m_comp->getCurrentTimeB(); }
    double getDurationB()    const { return m_comp->getDurationB(); }

    void  setCrossfade(float mixB) { m_comp->setCrossfade(mixB); }
    float crossfade() const { return m_comp->crossfade(); }

    void setPanicOverlay(PanicOverlay overlay) { m_comp->setPanicOverlay(overlay); update(); }
    PanicOverlay panicOverlay() const { return m_comp->panicOverlay(); }
    void setOutputFrozen(bool frozen) { m_comp->setOutputFrozen(frozen); }
    bool isOutputFrozen() const { return m_comp->isOutputFrozen(); }

    void           setTransitionMode(TransitionMode mode) { m_comp->setTransitionMode(mode); }
    TransitionMode transitionMode() const { return m_comp->transitionMode(); }

    QImage getFrameA() const { return m_comp->getFrameA(); }
    QImage getFrameB() const { return m_comp->getFrameB(); }

    void   loadVideo(const QString &path)    { loadVideoA(path); }
    void   play()                            { playA(); }
    void   pause()                           { pauseA(); }
    void   stop()                            { m_comp->stop(); }
    void   seek(double s)                    { seekA(s); }
    void   setRepeat(bool r)                 { setRepeatA(r); }
    void   setTrimPoints(double s, double e) { setTrimPointsA(s, e); }
    bool   isPlaying()    const              { return isPlayingA(); }
    double getCurrentTime() const            { return getCurrentTimeA(); }
    double getDuration()    const            { return getDurationA(); }

    QSize sizeHint() const override { return QSize(1280, 720); }

    static int programWidth()  { return Compositor::programWidth(); }
    static int programHeight() { return Compositor::programHeight(); }
    void setProgramResolution(int width, int height) { m_comp->setProgramResolution(width, height); update(); }
    static int deckPreviewWidth()  { return Compositor::deckPreviewWidth(); }
    static int deckPreviewHeight() { return Compositor::deckPreviewHeight(); }

    static OutputScaleMode outputScaleMode() { return Compositor::outputScaleMode(); }
    void setOutputScaleMode(OutputScaleMode mode) { m_comp->setOutputScaleMode(mode); update(); }
    static QRectF programRectOnSurface(const QSizeF &surface, const QSizeF &program, OutputScaleMode mode) {
        return Compositor::programRectOnSurface(surface, program, mode);
    }

    GLuint programColorTexture() const { return m_comp->programTexture(); }
    QSize  programFrameSize()    const { return m_comp->programFrameSize(); }

    void addProgramFrameConsumer()    { m_comp->addProgramFrameConsumer(); }
    void removeProgramFrameConsumer() { m_comp->removeProgramFrameConsumer(); }
    void setProgramFrameConsumerCount(int count) override { m_comp->setProgramFrameConsumerCount(count); }
    QImage programFrame() const override { return m_comp->programFrame(); }

    void addDeckPreviewConsumer()    { m_comp->addDeckPreviewConsumer(); }
    void removeDeckPreviewConsumer() { m_comp->removeDeckPreviewConsumer(); }
    void setDeckPreviewConsumerCount(int count) { m_comp->setDeckPreviewConsumerCount(count); }

    void setDeckFrameConsumerCount(int count) override { m_comp->setDeckFrameConsumerCount(count); }
    QImage deckProgramFrame(bool deckA) const override { return m_comp->deckProgramFrame(deckA); }

    QImage captureProgramFrame() { return m_comp->captureProgramFrame(); }
    void captureOutputFrameNow() override { m_comp->captureOutputFrameNow(); }

    /// Enable drag-to-move, double-click, and context menu for frameless windows.
    void setFramelessWindowChrome(bool enabled) {
        m_framelessWindowChrome = enabled;
        setMouseTracking(enabled);
        if (!enabled) unsetCursor();
    }
    bool framelessWindowChrome() const { return m_framelessWindowChrome; }

    void holdLayerAsStill(bool deckA, int chainIndex, const QImage &frame) {
        m_comp->holdLayerAsStill(deckA, chainIndex, frame);
    }

    const std::vector<NodeChainSource> &chainSourcesA() const { return m_comp->chainSourcesA(); }
    const std::vector<NodeChainSource> &chainSourcesB() const { return m_comp->chainSourcesB(); }

    void shutdownPlayback() { m_comp->shutdownPlayback(); }

signals:
    void programFrameReady();
    void framelessToggleFullscreenRequested();
    void framelessContextMenuRequested(const QPoint &globalPos);

protected:
    void initializeGL()           override;
    void resizeGL(int w, int h)   override;
    void paintGL()                override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *)   override;

private:
    void blitProgramToScreen(int surfaceW, int surfaceH);
    QRectF scaleRectToWidget(const QRectF &programRect) const;
    Qt::Edges resizeEdgesAt(const QPointF &pos) const;

    Compositor *m_comp = nullptr;
    int    m_glWidth  = 0;
    int    m_glHeight = 0;

    bool   m_framelessWindowChrome = false;
    bool   m_windowDragActive      = false;
    QPoint m_windowDragOffset;
};
