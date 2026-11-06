#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>

/// What the media bin shows about one library file. Filled by a background probe.
struct MediaMeta {
    bool probed = false;
    bool exists = true;
    bool readable = true;
    QString kind;
    double durationSeconds = 0.0;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    QString videoCodec;
    QString audioCodec;
    int sampleRate = 0;
    int channels = 0;
    qint64 size = 0;
    qint64 mtime = 0;

    QString durationText() const;
    QString formatText() const;
};

/// Process-wide cache of probed media metadata, shared by every media bin view.
/// Lookups never block: an unknown file is queued for a worker and metaChanged
/// fires when it lands.
class MediaMetaCache : public QObject {
    Q_OBJECT

public:
    static MediaMetaCache *instance();

    MediaMeta meta(const QString &path);
    void invalidate(const QString &path);
    // Re-stats every cached file on a worker; changed or newly missing/found files are re-probed.
    void rescan();

    // Opaque id for image://mediabin/<key>; the provider resolves it back to a path.
    QString keyFor(const QString &path);
    QString pathForKey(const QString &key) const;

    static MediaMeta probeNow(const QString &path);
    void deliver(const QString &path, const MediaMeta &result);

signals:
    void metaChanged(const QString &path);

private:
    MediaMetaCache();
    void queue(const QString &path);

    mutable QMutex m_mutex;
    QHash<QString, MediaMeta> m_meta;
    QSet<QString> m_pending;
    QHash<QString, QString> m_keyToPath;
    QHash<QString, QString> m_pathToKey;
    QThreadPool m_pool;
};
