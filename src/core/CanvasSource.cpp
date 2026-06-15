#include "core/CanvasSource.h"

CanvasSource::CanvasSource(Fill fill, QSize size, QColor color)
    : m_fill(fill), m_size(size), m_color(color) {
    rebuildBuffer();
}

void CanvasSource::setFill(Fill fill) {
    m_fill = fill;
    rebuildBuffer();
}

void CanvasSource::setColor(const QColor &color) {
    m_color = color;
    rebuildBuffer();
}

void CanvasSource::setSize(const QSize &size) {
    m_size = size;
    rebuildBuffer();
}

void CanvasSource::rebuildBuffer() {
    if (m_size.isEmpty()) {
        m_buffer.clear();
        return;
    }

    const int w = m_size.width();
    const int h = m_size.height();
    m_buffer.resize(w * h * 3);
    auto *buf = reinterpret_cast<uint8_t *>(m_buffer.data());

    if (m_fill == Fill::SolidColor) {
        const uint8_t r = static_cast<uint8_t>(m_color.red());
        const uint8_t g = static_cast<uint8_t>(m_color.green());
        const uint8_t b = static_cast<uint8_t>(m_color.blue());
        for (int i = 0; i < w * h; ++i) {
            buf[i * 3] = r;
            buf[i * 3 + 1] = g;
            buf[i * 3 + 2] = b;
        }
        return;
    }

    constexpr int tile = 32;
    const QColor light(190, 190, 190);
    const QColor dark(120, 120, 120);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool isLight = ((x / tile) + (y / tile)) % 2 == 0;
            const QColor c = isLight ? light : dark;
            const int idx = (y * w + x) * 3;
            buf[idx] = static_cast<uint8_t>(c.red());
            buf[idx + 1] = static_cast<uint8_t>(c.green());
            buf[idx + 2] = static_cast<uint8_t>(c.blue());
        }
    }
}
