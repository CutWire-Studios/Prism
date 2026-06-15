#pragma once

#include "core/MediaSource.h"
#include <QByteArray>
#include <QColor>

class CanvasSource : public MediaSource {
public:
    enum class Fill {
        Checkered,
        SolidColor,
    };

    explicit CanvasSource(Fill fill = Fill::Checkered,
                          QSize size = QSize(1280, 720),
                          QColor color = Qt::white);

    void setFill(Fill fill);
    void setColor(const QColor &color);
    void setSize(const QSize &size);

    Type type() const override { return Type::Canvas; }
    bool isReady() const override { return !m_buffer.isEmpty(); }
    QSize frameSize() const override { return m_size; }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_buffer.constData());
    }
    bool nextFrame() override { return false; }

private:
    void rebuildBuffer();

    Fill m_fill;
    QSize m_size;
    QColor m_color;
    QByteArray m_buffer;
};
