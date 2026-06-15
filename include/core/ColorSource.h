#pragma once

#include "core/MediaSource.h"
#include <QColor>
#include <QByteArray>

// MediaSource that outputs a solid RGB colour at a given resolution.
// Useful for:
//   • fade-to-black / fade-to-white
//   • test card / colour bars
//   • default empty deck state
//
// nextFrame() always returns false — the frame never changes until setColor()
// is called, which rebuilds the buffer and re-uploads via setSourceA/B.

class ColorSource : public MediaSource {
public:
    explicit ColorSource(QColor color = Qt::black,
                         QSize  size  = QSize(1280, 720));

    void setColor(const QColor &color);
    void setSize(const QSize &size);

    QColor color() const { return m_color; }

    Type    type()        const override { return Type::Color; }
    bool    isReady()     const override { return !m_buffer.isEmpty(); }
    QSize   frameSize()   const override { return m_size; }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_buffer.constData());
    }
    bool    nextFrame()         override { return false; }
    QString displayName() const override { return m_color.name(); }

private:
    void rebuildBuffer();

    QColor     m_color;
    QSize      m_size;
    QByteArray m_buffer;  // RGB24, m_size.width() * m_size.height() * 3 bytes
};
