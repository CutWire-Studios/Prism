#include "app/editors/ShapeSourceBackend.h"

#include "app/AppController.h"
#include "app/common/ThumbHelper.h"
#include "app/editors/StyleLayers.h"
#include "core/graph/ClipGraphModels.h"
#include "core/render/SkiaRender.h"
#include "core/sources/ShapeSource.h"

#include <QTimer>

using namespace prism;

namespace {

QImage thumbFor(const ShapeCatalogEntry &entry)
{
    constexpr QSize canvas(88, 64);
    QSizeF box(canvas.width() - 12, canvas.height() - 12);
    QSizeF fit = box;
    if (entry.aspect > 0) {
        fit = QSizeF(entry.aspect, 1.0);
        fit.scale(box, Qt::KeepAspectRatio);
    }
    const QRectF bounds(QPointF((canvas.width() - fit.width()) / 2, (canvas.height() - fit.height()) / 2), fit);
    return renderShape(entry.style, canvas, bounds, 0.0);
}

bool isOneOf(ShapeKind kind, std::initializer_list<ShapeKind> kinds)
{
    for (ShapeKind k : kinds) {
        if (k == kind)
            return true;
    }
    return false;
}

} // namespace

ShapeSourceBackend::ShapeSourceBackend(QObject *parent)
    : QObject(parent)
{
    QList<ShapeKind> seen;
    for (const ShapeCatalogEntry &entry : shapeCatalog()) {
        if (seen.contains(entry.style.kind))
            continue;
        seen.append(entry.style.kind);
        m_kinds.append(QVariantMap{{QStringLiteral("id"), shapeKindToString(entry.style.kind)},
                                   {QStringLiteral("label"), entry.label},
                                   {QStringLiteral("image"), thumbFor(entry)}});
    }

    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(25);
    connect(m_renderTimer, &QTimer::timeout, this, &ShapeSourceBackend::updatePreview);

    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(40);
    connect(m_clockTimer, &QTimer::timeout, this, &ShapeSourceBackend::updatePreview);
    m_clock.start();
}

QString ShapeSourceBackend::kind() const
{
    return shapeKindToString(m_style.kind);
}

QVariantMap ShapeSourceBackend::geometry() const
{
    return QVariantMap{{QStringLiteral("cornerRadius"), m_style.cornerRadius},
                       {QStringLiteral("points"), m_style.points},
                       {QStringLiteral("innerRatio"), m_style.innerRatio},
                       {QStringLiteral("headSize"), m_style.headSize},
                       {QStringLiteral("thickness"), m_style.thickness},
                       {QStringLiteral("tailX"), m_style.tailX},
                       {QStringLiteral("tailSize"), m_style.tailSize}};
}

QVariantMap ShapeSourceBackend::sections() const
{
    const ShapeKind k = m_style.kind;
    return QVariantMap{
        {QStringLiteral("cornerRadius"), k != ShapeKind::Ellipse},
        {QStringLiteral("star"), isOneOf(k, {ShapeKind::Star, ShapeKind::Burst})},
        {QStringLiteral("arrow"), isOneOf(k, {ShapeKind::Arrow, ShapeKind::DoubleArrow, ShapeKind::BlockArrow,
                                             ShapeKind::Chevron, ShapeKind::Banner})},
        {QStringLiteral("shaft"), isOneOf(k, {ShapeKind::Arrow, ShapeKind::DoubleArrow, ShapeKind::Chevron,
                                             ShapeKind::Cross, ShapeKind::CurvedArrow})},
        {QStringLiteral("bubble"), isOneOf(k, {ShapeKind::SpeechBubble, ShapeKind::SpeechBubbleRect,
                                              ShapeKind::ThoughtBubble, ShapeKind::Callout})},
    };
}

QVariantList ShapeSourceBackend::layers() const
{
    return StyleLayers::normalizedLayers(m_style.layers).toVariantList();
}

void ShapeSourceBackend::open(ClipGraphController *graph, quint64 nodeId)
{
    if (!graph || !graph->graph())
        return;
    m_graph = graph;
    m_nodeId = nodeId;
    m_initial = graph->graph()->sourceDescriptor(nodeId);
    m_style = ShapeSource::styleFromDescriptor(m_initial);
    emit styleChanged();
    updateClock();
    updatePreview();
}

void ShapeSourceBackend::setKind(const QString &kind)
{
    m_style.kind = shapeKindFromString(kind);
    emit styleChanged();
    scheduleRender();
}

void ShapeSourceBackend::setGeometry(const QString &name, double value)
{
    if (name == QLatin1String("cornerRadius")) m_style.cornerRadius = value;
    else if (name == QLatin1String("points")) m_style.points = int(value);
    else if (name == QLatin1String("innerRatio")) m_style.innerRatio = value;
    else if (name == QLatin1String("headSize")) m_style.headSize = value;
    else if (name == QLatin1String("thickness")) m_style.thickness = value;
    else if (name == QLatin1String("tailX")) m_style.tailX = value;
    else if (name == QLatin1String("tailSize")) m_style.tailSize = value;
    else return;
    scheduleRender();
}

void ShapeSourceBackend::setLayers(const QVariantList &layers)
{
    m_style.layers = StyleLayers::parseLayers(QJsonArray::fromVariantList(layers));
    emit styleChanged();
    updateClock();
    scheduleRender();
}

bool ShapeSourceBackend::commit()
{
    if (!m_graph || !m_graph->graph())
        return false;
    const SourceDescriptor desc = resultDescriptor();
    if (m_nodeId == 0) {
        auto *app = AppController::instance();
        return app && app->addSourceFromDescriptor(desc, ThumbHelper::makeShapeThumb(desc)) != 0;
    }
    return m_graph->setSourceDescriptor(m_nodeId, desc);
}

SourceDescriptor ShapeSourceBackend::resultDescriptor() const
{
    SourceDescriptor desc = m_initial;
    desc.kind = SourceDescriptor::Kind::Shape;
    desc.shapeStyleJson = ShapeSource::styleToJson(m_style);
    if (desc.displayName.isEmpty())
        desc.displayName = tr("Shape");
    return desc;
}

void ShapeSourceBackend::updateClock()
{
    const bool driven = m_style.isAnimated() || shapeTimeDrivenPaint(m_style);
    if (driven && !m_clockTimer->isActive())
        m_clockTimer->start();
    else if (!driven)
        m_clockTimer->stop();
}

void ShapeSourceBackend::scheduleRender()
{
    if (!m_renderTimer->isActive())
        m_renderTimer->start();
}

void ShapeSourceBackend::updatePreview()
{
    const double t = m_clock.elapsed() / 1000.0;
    m_preview = ShapeSource::renderDescriptor(resultDescriptor(), t);
    emit previewChanged();
}
