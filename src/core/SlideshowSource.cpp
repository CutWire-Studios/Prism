#include "core/SlideshowSource.h"
#include "core/ImageSource.h"    // reuse isStaticImageFile()
#include <QDir>
#include <QFileInfo>

// ── Load helpers ──────────────────────────────────────────────────────────────

bool SlideshowSource::loadFolder(const QString &folderPath, int intervalMs) {
    m_name = QFileInfo(folderPath).fileName();

    QDir dir(folderPath);
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::Name | QDir::IgnoreCase);

    QStringList files;
    for (const QString &f : dir.entryList()) {
        if (ImageSource::isStaticImageFile(f))
            files << dir.absoluteFilePath(f);
    }

    return loadFiles(files, intervalMs);
}

bool SlideshowSource::loadFiles(const QStringList &filePaths, int intervalMs) {
    m_intervalMs = intervalMs;
    m_images.clear();
    m_current  = 0;
    m_frameSize = {};
    m_elapsed.invalidate();

    for (const QString &path : filePaths) {
        QImage img = QImage(path).convertToFormat(QImage::Format_RGB888);
        if (img.isNull()) continue;

        if (m_frameSize.isEmpty()) {
            m_frameSize = img.size();
        } else if (img.size() != m_frameSize) {
            // Scale to the reference size so the GL texture stays the same dimensions.
            img = img.scaled(m_frameSize, Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
        }
        m_images.append(std::move(img));
    }

    return !m_images.isEmpty();
}

// ── MediaSource interface ─────────────────────────────────────────────────────

const uint8_t *SlideshowSource::frameData() const {
    if (m_images.isEmpty()) return nullptr;
    return reinterpret_cast<const uint8_t *>(m_images[m_current].constBits());
}

bool SlideshowSource::nextFrame() {
    if (m_images.isEmpty()) return false;

    // On first call: start the timer, stay on slide 0.
    if (!m_elapsed.isValid()) {
        m_elapsed.start();
        return false;
    }

    if (m_images.size() < 2) return false;

    if (m_elapsed.elapsed() >= m_intervalMs) {
        m_current = (m_current + 1) % m_images.size();
        m_elapsed.restart();
        return true;
    }

    return false;
}
