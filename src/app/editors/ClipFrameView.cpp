#include "editors/ClipFrameView.h"

#include "app/AppController.h"
#include "app/graph/GraphRuntime.h"
#include "core/media/StillImage.h"

#include <QPainter>
#include <QPointer>
#include <QThreadPool>

ClipFrameView::ClipFrameView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(50);
    connect(&m_debounce, &QTimer::timeout, this, &ClipFrameView::request);
    connect(this, &ClipFrameView::sourceChanged, this, &ClipFrameView::reload);
}

void ClipFrameView::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    emit sourceChanged();
}

void ClipFrameView::setTime(double time)
{
    if (qFuzzyCompare(m_time + 1.0, time + 1.0))
        return;
    m_time = time;
    emit sourceChanged();
}

void ClipFrameView::setNodeId(quint64 id)
{
    if (m_nodeId == id)
        return;
    m_nodeId = id;
    emit sourceChanged();
}

void ClipFrameView::setMaxSize(int size)
{
    if (m_maxSize == size)
        return;
    m_maxSize = size;
    emit sourceChanged();
}

qreal ClipFrameView::imageAspect() const
{
    return m_image.isNull() ? 0.0 : qreal(m_image.width()) / qMax(1, m_image.height());
}

void ClipFrameView::paint(QPainter *painter)
{
    if (m_image.isNull())
        return;
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(boundingRect(), m_image);
}

void ClipFrameView::reload()
{
    ++m_generation;
    m_debounce.start();
}

void ClipFrameView::request()
{
    QString path = m_path;
    double time = m_time;
    QImage fallback;

    if (path.isEmpty() && m_nodeId != 0) {
        if (auto *app = AppController::instance(); app && app->clipGraph()) {
            const SourceDescriptor desc = app->clipGraph()->sourceDescriptor(m_nodeId);
            if (desc.kind == SourceDescriptor::Kind::VideoFile || desc.kind == SourceDescriptor::Kind::Image) {
                path = desc.path;
                time = app->clipGraph()->clipSettings(m_nodeId).startTime;
            }
            if (path.isEmpty() && app->graphRuntime())
                fallback = app->graphRuntime()->thumbnail(m_nodeId);
        }
    }

    if (path.isEmpty()) {
        apply(fallback, m_generation);
        return;
    }

    const int generation = m_generation;
    const int maxSize = m_maxSize;
    QPointer<ClipFrameView> self(this);
    QThreadPool::globalInstance()->start([self, path, time, maxSize, generation] {
        QImage image = StillImage::decodeVideoFrame(path, time, maxSize, maxSize);
        if (image.isNull())
            image = StillImage::decode(path, maxSize, maxSize);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, image, generation] {
            if (self)
                self->apply(image, generation);
        }, Qt::QueuedConnection);
    });
}

void ClipFrameView::apply(const QImage &image, int generation)
{
    if (generation != m_generation)
        return;
    m_image = image;
    emit imageChanged();
    update();
}
