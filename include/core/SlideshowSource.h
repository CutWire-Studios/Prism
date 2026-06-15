#pragma once

#include "core/MediaSource.h"
#include <QImage>
#include <QList>
#include <QElapsedTimer>

// MediaSource that cycles through a folder (or list) of static images.
//
// All images are normalised to the first image's pixel size on load so the
// GL texture never needs to be reallocated between slides.
// nextFrame() advances the slide when enough time has passed (intervalMs);
// VideoWidget's 33 ms frame timer drives the polling — no extra QTimer needed.

class SlideshowSource : public MediaSource {
public:
    SlideshowSource() = default;
    ~SlideshowSource() override = default;

    // Load all supported images from a folder, sorted by filename.
    bool loadFolder(const QString &folderPath, int intervalMs = 3000);

    // Load an explicit list of image file paths.
    bool loadFiles(const QStringList &filePaths, int intervalMs = 3000);

    void setIntervalMs(int ms) { m_intervalMs = ms; }
    int  intervalMs()    const { return m_intervalMs; }
    int  count()         const { return m_images.size(); }
    int  currentIndex()  const { return m_current; }

    Type    type()        const override { return Type::Slideshow; }
    bool    isReady()     const override { return !m_images.isEmpty(); }
    QSize   frameSize()   const override { return m_frameSize; }
    const uint8_t *frameData() const override;

    // Returns true when the slide advances (triggers a GL texture re-upload).
    bool    nextFrame()         override;

    QString displayName() const override { return m_name; }

private:
    QList<QImage> m_images;      // all Format_RGB888, all m_frameSize
    QSize         m_frameSize;
    int           m_current    = 0;
    int           m_intervalMs = 3000;
    QElapsedTimer m_elapsed;
    QString       m_name;
};
