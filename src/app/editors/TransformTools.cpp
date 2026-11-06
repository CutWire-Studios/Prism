#include "editors/TransformTools.h"

#include "app/AppController.h"

#include <QPolygonF>
#include <QTransform>

namespace {

ClipGraph *graph()
{
    auto *app = AppController::instance();
    return app ? app->clipGraph() : nullptr;
}

} // namespace

TransformTools::TransformTools(QObject *parent)
    : QObject(parent)
{
}

QVariantList TransformTools::layerSlots(quint64 layerId) const
{
    QVariantList out;
    const ClipGraph *g = graph();
    if (!g)
        return out;
    for (const ClipGraph::LayerSlotView &v : g->layerSlotViews(layerId)) {
        out.append(QVariantMap{{"slot", v.index},
                               {"name", v.name},
                               {"visible", v.visible},
                               {"sourceNode", quint64(v.sourceNode)},
                               {"x", v.rect.x()},
                               {"y", v.rect.y()},
                               {"w", v.rect.width()},
                               {"h", v.rect.height()}});
    }
    return out;
}

bool TransformTools::setSlotRect(quint64 layerId, int slot, qreal x, qreal y, qreal w, qreal h)
{
    ClipGraph *g = graph();
    return g && g->setLayerSlotRect(layerId, slot, float(x), float(y), float(w), float(h));
}

bool TransformTools::setSlotVisible(quint64 layerId, int slot, bool visible)
{
    ClipGraph *g = graph();
    return g && g->setLayerSlotVisible(layerId, slot, visible);
}

QSize TransformTools::canvasSize(quint64 layerId) const
{
    int w = 0, h = 0;
    const ClipGraph *g = graph();
    if (!g || !g->layerCanvasSize(layerId, w, h))
        return {};
    return {w, h};
}

bool TransformTools::setCanvasSize(quint64 layerId, int w, int h)
{
    ClipGraph *g = graph();
    return g && g->setLayerCanvasSize(layerId, w, h);
}

QMatrix4x4 TransformTools::quadMatrix(qreal w, qreal h, const QVariantList &corners) const
{
    if (corners.size() != 4)
        return {};
    QPolygonF dst;
    for (const QVariant &c : corners) {
        const QVariantList p = c.toList();
        if (p.size() != 2)
            return {};
        dst << QPointF(p.at(0).toDouble() * w, p.at(1).toDouble() * h);
    }
    const QPolygonF src{{0, 0}, {w, 0}, {w, h}, {0, h}};
    QTransform t;
    if (!QTransform::quadToQuad(src, dst, t))
        return {};
    return QMatrix4x4(t);
}
