#pragma once

#include "core/MediaSource.h"
#include <QImage>

// MediaSource implementation for static images (PNG/JPG/BMP/WEBP/GIF).
// Loaded once via Qt — no FFmpeg required.
// nextFrame() always returns false; the image is uploaded once on load.
class ImageSource : public MediaSource {
public:
    ImageSource() = default;
    ~ImageSource() override = default;

    bool load(const QString &filePath);

    // Detects whether a file path points to a supported static image.
    static bool isStaticImageFile(const QString &path);

    Type    type()        const override { return Type::Image; }
    bool    isReady()     const override { return !m_image.isNull(); }
    QSize   frameSize()   const override { return m_image.size(); }
    const uint8_t *frameData() const override {
        return reinterpret_cast<const uint8_t *>(m_image.constBits());
    }
    bool    nextFrame()         override { return false; }  // static — never changes
    QString displayName() const override { return m_name; }
    bool    hasAlpha()    const override { return true; }

private:
    QImage  m_image;   // Always QImage::Format_RGBA8888 for proper compositing over other images
    QString m_name;
};
