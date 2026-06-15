#include "core/ColorSource.h"

ColorSource::ColorSource(QColor color, QSize size)
    : m_color(color), m_size(size) {
    rebuildBuffer();
}

void ColorSource::setColor(const QColor &color) {
    m_color = color;
    rebuildBuffer();
}

void ColorSource::setSize(const QSize &size) {
    m_size = size;
    rebuildBuffer();
}

void ColorSource::rebuildBuffer() {
    const int pixels = m_size.width() * m_size.height();
    m_buffer.resize(pixels * 3);

    auto *buf = reinterpret_cast<uint8_t *>(m_buffer.data());
    const uint8_t r = static_cast<uint8_t>(m_color.red());
    const uint8_t g = static_cast<uint8_t>(m_color.green());
    const uint8_t b = static_cast<uint8_t>(m_color.blue());

    for (int i = 0; i < pixels; ++i) {
        buf[i * 3    ] = r;
        buf[i * 3 + 1] = g;
        buf[i * 3 + 2] = b;
    }
}
