#include "core/sources/SegmentationSource.h"

#include <QDebug>
#include <utility>

SegmentationSource::SegmentationSource(std::unique_ptr<MediaSource> inner)
    : m_inner(std::move(inner)) {
    m_segmenter = std::make_shared<SelfieSegmenter>();
    if (m_segmenter->isReady()) {
        m_worker = std::thread(&SegmentationSource::workerLoop, this);
    } else {
        qWarning() << "SegmentationSource: segmenter unavailable — passing frames "
                      "through without background removal";
    }
}

SegmentationSource::~SegmentationSource() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_worker.joinable())
        m_worker.join();
}

bool SegmentationSource::isReady() const {
    return m_inner && m_inner->isReady() && !m_output.isNull();
}

bool SegmentationSource::nextFrame() {
    if (!m_inner)
        return false;

    const bool innerNew = m_inner->nextFrame();

    if (innerNew && m_inner->isReady()) {
        const QSize sz = m_inner->frameSize();
        if (!sz.isEmpty()) {
            const QImage::Format fmt = m_inner->hasAlpha()
                ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
            const QImage wrapped(m_inner->frameData(), sz.width(), sz.height(), fmt);
            // Deep copy into an owned RGB frame (detaches from the inner buffer).
            m_currentFrame = wrapped.convertToFormat(QImage::Format_RGB888);

            if (m_worker.joinable()) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_pendingInput = m_currentFrame;   // cheap COW handoff to worker
                m_hasPending = true;
                m_cv.notify_one();
            }
        }
    }

    if (m_currentFrame.isNull())
        return false;

    // Pick up a fresh mask if the worker produced one since last compose.
    QImage mask;
    bool newMask = false;
    if (m_worker.joinable()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_maskGen != m_consumedMaskGen) {
            mask = m_latestMask;
            m_consumedMaskGen = m_maskGen;
            newMask = true;
        }
    }

    if (!innerNew && !newMask)
        return false;

    // Reuse the last mask when only the frame changed so the matte still tracks.
    if (mask.isNull())
        mask = m_lastMaskUsed;
    else
        m_lastMaskUsed = mask;

    compose(m_currentFrame, mask);
    return true;
}

void SegmentationSource::compose(const QImage &frameRgb, const QImage &mask) {
    QImage out = frameRgb.convertToFormat(QImage::Format_RGBA8888);
    const int w = out.width();
    const int h = out.height();

    if (mask.isNull()) {
        // No mask yet (or segmenter disabled): emit fully opaque passthrough.
        m_output = std::move(out);
        return;
    }

    const QImage scaled = mask.width() == w && mask.height() == h
        ? mask
        : mask.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    for (int y = 0; y < h; ++y) {
        uchar *dst = out.scanLine(y);
        const uchar *m = scaled.constScanLine(y);
        for (int x = 0; x < w; ++x)
            dst[x * 4 + 3] = m[x];   // alpha = foreground probability
    }
    m_output = std::move(out);
}

void SegmentationSource::workerLoop() {
    for (;;) {
        QImage input;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop || m_hasPending; });
            if (m_stop)
                return;
            input = std::move(m_pendingInput);
            m_hasPending = false;
        }

        QImage mask = m_segmenter->segment(input);
        if (mask.isNull())
            continue;

        std::lock_guard<std::mutex> lock(m_mutex);
        m_latestMask = std::move(mask);
        ++m_maskGen;
    }
}
