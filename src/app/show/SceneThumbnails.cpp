#include "app/show/SceneThumbnails.h"

#include "app/show/ShowManager.h"

#include <QDir>
#include <QQuickImageProvider>

namespace {

class Provider : public QQuickImageProvider {
public:
    explicit Provider(const ShowManager *show) : QQuickImageProvider(QQuickImageProvider::Image), m_show(show) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &) override
    {
        const Scene *scene = m_show->scene(id.section(QLatin1Char('?'), 0, 0));
        QImage img = scene ? QImage(scene->thumbnail()) : QImage();
        if (size)
            *size = img.size();
        return img;
    }

private:
    const ShowManager *m_show;
};

} // namespace

QString SceneThumbnails::save(const QImage &frame, const QString &dir, const QString &sceneId)
{
    if (frame.isNull() || !QDir().mkpath(dir))
        return {};
    const QString path = QDir(dir).filePath(sceneId + QStringLiteral(".png"));
    const QImage scaled = frame.scaled(kWidth, kHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return scaled.save(path) ? path : QString();
}

QQuickImageProvider *SceneThumbnails::createProvider(const ShowManager *show)
{
    return new Provider(show);
}
