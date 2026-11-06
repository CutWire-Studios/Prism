#pragma once

#include <QImage>
#include <QQuickImageProvider>

/// image://mediabin/<key>[?v=...] — poster frames for library files. <key> comes from
/// MediaMetaCache::keyFor. Frames are decoded on a small worker pool and kept in a disk cache.
class MediaThumbnailProvider : public QQuickAsyncImageProvider {
public:
    MediaThumbnailProvider();
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

    static QImage render(const QString &path);
};
