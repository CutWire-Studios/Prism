#include "editors/ClipEditTools.h"

#include "app/AppController.h"
#include "app/common/ThumbHelper.h"
#include "app/graph/GraphRuntime.h"
#include "core/media/MediaProbe.h"
#include "core/sources/NdiSource.h"
#include "core/sources/ScreenCapturePersist.h"
#include "core/sources/ScreenSource.h"
#include "core/sources/SlideshowSource.h"

#include <QCameraDevice>
#include <QMediaDevices>
#include <QUuid>

#include <numeric>

namespace {

ClipGraph *graph()
{
    auto *app = AppController::instance();
    return app ? app->clipGraph() : nullptr;
}

} // namespace

ClipEditTools::ClipEditTools(QObject *parent)
    : QObject(parent)
{
}

ClipEditTools::~ClipEditTools()
{
    if (m_picker)
        delete m_picker.data();
}

int ClipEditTools::nodeType(quint64 id) const
{
    const ClipGraph *g = graph();
    const ClipGraph::Node *n = g ? g->node(id) : nullptr;
    return n ? int(n->type) : -1;
}

QVariantMap ClipEditTools::nodeParams(quint64 id) const
{
    const ClipGraph *g = graph();
    const ClipGraph::Node *n = g ? g->node(id) : nullptr;
    return n ? n->params : QVariantMap{};
}

quint64 ClipEditTools::referenceInput(quint64 processId) const
{
    const ClipGraph *g = graph();
    if (!g)
        return 0;
    const NodeId up = g->producerForInput(processId, 0);
    if (up == 0)
        return 0;
    const ClipGraph::ResolvedStream sub = g->evaluateVideoInput(up);
    return sub.layers.isEmpty() ? 0 : sub.layers.first().inputNodeId;
}

double ClipEditTools::mediaDuration(const QString &path) const
{
    const MediaInfo info = MediaProbe::probe(path);
    return info.ok ? double(info.durationUs) / 1e6 : 0.0;
}

QStringList ClipEditTools::slideshowEffects() const
{
    return SlideshowSource::effectNames();
}

QVariantList ClipEditTools::cameraDevices() const
{
    QVariantList out;
    out.append(QVariantMap{{"id", QString()}, {"label", tr("Default Camera")},
                           {"index", 0}, {"isDefault", true}});
    const auto devices = QMediaDevices::videoInputs();
    for (int i = 0; i < devices.size(); ++i) {
        out.append(QVariantMap{{"id", QString::fromUtf8(devices[i].id())},
                               {"label", devices[i].description()},
                               {"index", i}, {"isDefault", false}});
    }
    return out;
}

bool ClipEditTools::ndiAvailable() const
{
    return NdiSource::isAvailable();
}

QStringList ClipEditTools::ndiSources() const
{
    return NdiSource::discoverSources(2000);
}

bool ClipEditTools::captureSupported() const
{
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
}

void ClipEditTools::pickCapture(quint64 nodeId)
{
#ifdef Q_OS_LINUX
    ClipGraph *g = graph();
    auto *app = AppController::instance();
    if (!g || !app || !app->graphController() || m_picker) {
        emit captureFinished(nodeId, false);
        return;
    }

    auto *picker = new ScreenSource;
    picker->setPickOnly(true);
    m_picker = picker;
    connect(picker, &ScreenSource::captureConfigured, this,
            [this, picker, nodeId](bool ok, const QString &token) {
        picker->stop();
        picker->deleteLater();
        m_picker.clear();
        if (ok) {
            ClipGraph *g = graph();
            auto *app = AppController::instance();
            if (g && app && app->graphController()) {
                QVariantMap source = g->node(nodeId) ? g->node(nodeId)->params.value("source").toMap()
                                                     : QVariantMap{};
                // A fresh id makes any deck showing this source reload and restore the new pick.
                ScreenCapturePersist::clearRestoreToken(source.value("captureId").toString());
                const QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                ScreenCapturePersist::setRestoreToken(newId, token);
                source["captureId"] = newId;
                app->graphController()->setParams(nodeId, {{"source", source}});
            }
        }
        emit captureFinished(nodeId, ok);
    }, Qt::QueuedConnection);

    if (!picker->start(ScreenSource::CaptureType::Any)) {
        m_picker.clear();
        delete picker;
        emit captureFinished(nodeId, false);
    }
#else
    emit captureFinished(nodeId, false);
#endif
}

void ClipEditTools::refreshThumbnail(quint64 nodeId)
{
    auto *app = AppController::instance();
    ClipGraph *g = graph();
    if (!app || !g || !app->graphRuntime())
        return;
    const SourceDescriptor desc = g->sourceDescriptor(nodeId);
    if (desc.kind != SourceDescriptor::Kind::Canvas)
        return;
    const int gcd = std::gcd(qMax(1, desc.canvasWidth), qMax(1, desc.canvasHeight));
    const QString ratio = QStringLiteral("%1:%2").arg(desc.canvasWidth / gcd).arg(desc.canvasHeight / gcd);
    app->graphRuntime()->setThumbnail(nodeId, ThumbHelper::makeCanvasThumb(ratio, desc.canvasFill, desc.color));
}
