#include "app/bin/MediaThumbnailProvider.h"

#include "app/bin/MediaMetaCache.h"
#include "core/media/MediaFormats.h"
#include "core/media/StillImage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QQuickTextureFactory>
#include <QAtomicInt>
#include <QDateTime>
#include <QRunnable>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QThreadPool>

namespace {

constexpr int kThumbWidth = 480;
constexpr int kThumbHeight = 270;
constexpr int kCacheVersion = 1;

QThreadPool *thumbPool()
{
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(3);
        return p;
    }();
    return pool;
}

QString cacheBase(const QString &path)
{
    const QFileInfo fi(path);
    const QByteArray id = path.toUtf8() + '|' + QByteArray::number(fi.size()) + '|'
        + QByteArray::number(fi.lastModified().toMSecsSinceEpoch()) + '|' + QByteArray::number(kCacheVersion);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/mediabin");
    return dir + QLatin1Char('/')
        + QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex());
}

class ThumbResponse : public QQuickImageResponse {
public:
    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }
    QString errorString() const override { return m_error; }
    void cancel() override { m_cancelled->storeRelaxed(1); }
    QSharedPointer<QAtomicInt> cancelFlag() const { return m_cancelled; }

    void deliver(const QImage &image, const QString &error)
    {
        m_image = image;
        m_error = error;
        emit finished();
    }

private:
    QImage m_image;
    QString m_error;
    QSharedPointer<QAtomicInt> m_cancelled = QSharedPointer<QAtomicInt>::create(0);
};

class ThumbJob : public QRunnable {
public:
    ThumbJob(QPointer<ThumbResponse> response, QSharedPointer<QAtomicInt> cancelled, QString path)
        : m_response(std::move(response)), m_cancelled(std::move(cancelled)), m_path(std::move(path)) {}

    void run() override
    {
        QImage image;
        QString error;
        if (m_cancelled->loadRelaxed() == 0) {
            image = MediaThumbnailProvider::render(m_path);
            if (image.isNull())
                error = QStringLiteral("no thumbnail");
        } else {
            error = QStringLiteral("cancelled");
        }
        QPointer<ThumbResponse> response = m_response;
        if (!response)
            return;
        QMetaObject::invokeMethod(response.data(), [response, image, error] {
            if (response)
                response->deliver(image, error);
        }, Qt::QueuedConnection);
    }

private:
    QPointer<ThumbResponse> m_response;
    QSharedPointer<QAtomicInt> m_cancelled;
    QString m_path;
};

} // namespace

MediaThumbnailProvider::MediaThumbnailProvider() = default;

QImage MediaThumbnailProvider::render(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.isFile() || MediaFormats::isAudioPath(path))
        return {};

    const QString base = cacheBase(path);
    for (const QString &ext : {QStringLiteral(".jpg"), QStringLiteral(".png")}) {
        QImage cached;
        if (cached.load(base + ext))
            return cached;
    }

    QImage image;
    if (MediaFormats::isImagePath(path)) {
        image = StillImage::decode(path, kThumbWidth, kThumbHeight);
    } else {
        const MediaMeta probe = MediaMetaCache::probeNow(path);
        const double at = probe.durationSeconds > 0.0 ? probe.durationSeconds * 0.1 : 0.0;
        image = StillImage::decodeVideoFrame(path, at, kThumbWidth, kThumbHeight);
        if (!image.isNull())
            image = image.convertToFormat(QImage::Format_RGB888);
    }
    if (image.isNull())
        return {};
    if (image.width() > kThumbWidth || image.height() > kThumbHeight)
        image = image.scaled(kThumbWidth, kThumbHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QDir().mkpath(QFileInfo(base).absolutePath());
    if (image.hasAlphaChannel())
        image.save(base + QStringLiteral(".png"), "PNG");
    else
        image.convertToFormat(QImage::Format_RGB888).save(base + QStringLiteral(".jpg"), "JPG", 85);
    return image;
}

QQuickImageResponse *MediaThumbnailProvider::requestImageResponse(const QString &id, const QSize &)
{
    auto *response = new ThumbResponse;
    const QString key = id.left(id.indexOf(QLatin1Char('?')));
    const QString path = MediaMetaCache::instance()->pathForKey(key);
    if (path.isEmpty()) {
        QMetaObject::invokeMethod(response, [response] { response->deliver({}, QStringLiteral("unknown")); },
                                  Qt::QueuedConnection);
        return response;
    }
    thumbPool()->start(new ThumbJob(QPointer<ThumbResponse>(response), response->cancelFlag(), path));
    return response;
}
