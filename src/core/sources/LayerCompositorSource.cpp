#include "core/sources/LayerCompositorSource.h"

namespace {
/// Wrap the inner source's current frame as a non-owning QImage (matching the
/// convention used by the CPU effect decorators).
QImage wrapFrame(MediaSource *src) {
    const QSize sz = src->frameSize();
    if (sz.isEmpty()) return {};
    const uint8_t *data = src->frameData();
    if (!data) return {};
    const QImage::Format fmt = src->hasAlpha() ? QImage::Format_RGBA8888
                                               : QImage::Format_RGB888;
    return QImage(data, sz.width(), sz.height(), fmt);
}
} // namespace

bool LayerCompositorSource::advanceTop(Layer &l) {
    if (!l.source) return false;
    const double dur = l.source->duration();
    if (dur <= 0.0)                      // live/image/canvas: one frame per tick
        return l.source->nextFrame();

    if (!l.clock.isValid()) {
        l.anchor = l.source->currentTime();
        l.clock.start();
    }
    double desired = l.anchor + l.clock.elapsed() / 1000.0;   // native 1× rate
    if (desired >= dur) {                                     // loop
        l.source->seek(0.0);
        l.anchor = 0.0;
        l.clock.restart();
        desired = 0.0;
    }
    return l.source->presentAt(desired);
}

bool LayerCompositorSource::nextFrame() {
    if (m_canvas.isEmpty()) return false;

    // The bottom layer (index 0) is advanced once per call — the deck's pacing
    // controls how often that happens, so its speed/scrub apply to the bottom.
    // Upper layers self-pace to their native rate so deck speed doesn't scale them.
    bool anyNew = (m_outTex == 0);
    bool anyReady = false;
    for (size_t i = 0; i < m_layers.size(); ++i) {
        Layer &l = m_layers[i];
        if (!l.source) continue;
        bool n = false;
        if (i != 0)
            n = advanceTop(l);
        else
            n = m_presentAt >= 0.0 ? l.source->presentAt(m_presentAt) : l.source->nextFrame();
        if (n) anyNew = true;
        if (l.source->isReady()) anyReady = true;
    }
    if (!anyNew) return false;
    if (!anyReady) return false;

    if (!m_runner.beginComposite(m_canvas)) return false;

    for (Layer &l : m_layers) {
        if (!l.visible || !l.source || !l.source->isReady()) continue;
        unsigned int tex = l.source->glTexture();
        if (tex == 0) {
            const QImage frame = wrapFrame(l.source.get());
            if (frame.isNull()) continue;
            tex = m_runner.uploadScratch(frame.convertToFormat(QImage::Format_RGBA8888));
        }
        m_runner.drawCompositeLayer(tex, l.bx, l.by, l.bw, l.bh, l.flipH, l.flipV);
    }

    m_outTex = m_runner.lastOutput();
    m_runner.endComposite();
    m_cpuValid = false;
    return true;
}

const uint8_t *LayerCompositorSource::frameData() const {
    if (m_outTex == 0) return nullptr;
    if (!m_cpuValid) {
        m_cpu = m_runner.readback();
        m_cpuValid = true;
    }
    return m_cpu.isNull() ? nullptr
                          : reinterpret_cast<const uint8_t *>(m_cpu.constBits());
}
