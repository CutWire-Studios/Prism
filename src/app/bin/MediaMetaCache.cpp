#include "app/bin/MediaMetaCache.h"

#include "core/media/MediaFormats.h"
#include "core/media/MediaProbe.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <QMutexLocker>
#include <QRunnable>

#include <algorithm>
#include <utility>

namespace {

QString kindOf(const QString &path)
{
    if (MediaFormats::isAudioPath(path))
        return QStringLiteral("audio");
    if (MediaFormats::isImagePath(path))
        return QStringLiteral("image");
    return QStringLiteral("video");
}

QString channelsText(int channels)
{
    switch (channels) {
    case 1: return QObject::tr("mono");
    case 2: return QObject::tr("stereo");
    default: return channels > 0 ? QObject::tr("%1 ch").arg(channels) : QString();
    }
}

class ProbeJob : public QRunnable {
public:
    explicit ProbeJob(QString path) : m_path(std::move(path)) {}

    void run() override
    {
        const MediaMeta result = MediaMetaCache::probeNow(m_path);
        MediaMetaCache *cache = MediaMetaCache::instance();
        QMetaObject::invokeMethod(cache, [cache, path = m_path, result] {
            cache->deliver(path, result);
        }, Qt::QueuedConnection);
    }

private:
    QString m_path;
};

} // namespace

QString MediaMeta::durationText() const
{
    if (kind == QLatin1String("image") || durationSeconds <= 0.0)
        return {};
    const qint64 total = qint64(durationSeconds + 0.5);
    const qint64 h = total / 3600;
    const qint64 m = (total / 60) % 60;
    const qint64 s = total % 60;
    return h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'))
                 : QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString MediaMeta::formatText() const
{
    QStringList parts;
    if (kind == QLatin1String("audio")) {
        if (sampleRate > 0)
            parts << QObject::tr("%1 kHz").arg(sampleRate / 1000.0, 0, 'g', 3);
        const QString ch = channelsText(channels);
        if (!ch.isEmpty())
            parts << ch;
        if (!audioCodec.isEmpty())
            parts << audioCodec.toUpper();
    } else {
        if (width > 0 && height > 0)
            parts << QStringLiteral("%1×%2").arg(width).arg(height);
        if (kind == QLatin1String("video")) {
            if (fps > 0.0)
                parts << QObject::tr("%1 fps").arg(fps, 0, 'g', 4);
            if (!videoCodec.isEmpty())
                parts << videoCodec.toUpper();
        }
    }
    return parts.join(QStringLiteral(" · "));
}

MediaMetaCache::MediaMetaCache()
{
    m_pool.setMaxThreadCount(2);
}

MediaMetaCache *MediaMetaCache::instance()
{
    static MediaMetaCache *cache = new MediaMetaCache;
    return cache;
}

MediaMeta MediaMetaCache::probeNow(const QString &path)
{
    MediaMeta m;
    m.kind = kindOf(path);
    const QFileInfo fi(path);
    m.exists = fi.isFile();
    m.size = fi.size();
    m.mtime = fi.lastModified().toMSecsSinceEpoch();
    m.probed = true;
    if (!m.exists)
        return m;

    if (m.kind == QLatin1String("image")) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize sz = reader.size();
        m.width = sz.width();
        m.height = sz.height();
        return m;
    }

    const MediaInfo info = MediaProbe::probe(path);
    if (!info.ok) {
        m.readable = false;
        return m;
    }
    m.durationSeconds = double(info.durationUs) / 1e6;
    for (const StreamInfo &s : info.streams) {
        if (s.type == StreamInfo::Type::Video && !s.attachedPicture && m.videoCodec.isEmpty()) {
            m.width = s.width;
            m.height = s.height;
            m.fps = s.fps;
            m.videoCodec = s.codecName;
            if (s.rotationDegrees == 90 || s.rotationDegrees == 270)
                std::swap(m.width, m.height);
        } else if (s.type == StreamInfo::Type::Audio && m.audioCodec.isEmpty()) {
            m.audioCodec = s.codecName;
            m.sampleRate = s.sampleRate;
            m.channels = s.channels;
        }
    }
    if (m.kind == QLatin1String("video") && m.videoCodec.isEmpty() && !m.audioCodec.isEmpty())
        m.kind = QStringLiteral("audio");
    return m;
}

MediaMeta MediaMetaCache::meta(const QString &path)
{
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_meta.constFind(path);
        if (it != m_meta.constEnd())
            return *it;
    }
    queue(path);
    MediaMeta placeholder;
    placeholder.kind = kindOf(path);
    return placeholder;
}

void MediaMetaCache::queue(const QString &path)
{
    {
        QMutexLocker lock(&m_mutex);
        if (m_pending.contains(path))
            return;
        m_pending.insert(path);
    }
    m_pool.start(new ProbeJob(path));
}

void MediaMetaCache::deliver(const QString &path, const MediaMeta &result)
{
    {
        QMutexLocker lock(&m_mutex);
        m_meta.insert(path, result);
        m_pending.remove(path);
    }
    emit metaChanged(path);
}

void MediaMetaCache::invalidate(const QString &path)
{
    queue(path);
}

void MediaMetaCache::rescan()
{
    QList<QPair<QString, MediaMeta>> known;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_meta.constBegin(); it != m_meta.constEnd(); ++it)
            known.append({it.key(), it.value()});
    }
    for (const auto &[path, meta] : std::as_const(known)) {
        const QFileInfo fi(path);
        const bool exists = fi.isFile();
        if (exists != meta.exists
            || (exists && (fi.size() != meta.size || fi.lastModified().toMSecsSinceEpoch() != meta.mtime)))
            invalidate(path);
    }
}

QString MediaMetaCache::keyFor(const QString &path)
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_pathToKey.constFind(path);
    if (it != m_pathToKey.constEnd())
        return *it;
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex());
    m_pathToKey.insert(path, key);
    m_keyToPath.insert(key, path);
    return key;
}

QString MediaMetaCache::pathForKey(const QString &key) const
{
    QMutexLocker lock(&m_mutex);
    return m_keyToPath.value(key);
}
