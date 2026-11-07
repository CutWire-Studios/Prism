#include "app/show/Preflight.h"

#include "app/show/ShowManager.h"
#include "core/effects/EffectPassRunner.h"
#include "core/effects/ShaderEffects.h"
#include "core/graph/ClipGraph.h"
#include "core/project/AssetPathResolver.h"
#include "core/sources/SharedSourcePool.h"

#include <QFileInfo>
#include <algorithm>
#include <QJsonObject>
#include <QCameraDevice>
#include <QMediaDevices>

QJsonObject PreflightIssue::toJson() const
{
    QString kindName;
    switch (kind) {
    case Kind::MissingFile: kindName = QStringLiteral("missing_file"); break;
    case Kind::MissingDevice: kindName = QStringLiteral("missing_device"); break;
    case Kind::MissingScene: kindName = QStringLiteral("missing_scene"); break;
    case Kind::SceneCycle: kindName = QStringLiteral("scene_cycle"); break;
    case Kind::HistoryMemory: kindName = QStringLiteral("history_memory"); break;
    case Kind::ResidentMemory: kindName = QStringLiteral("resident_memory"); break;
    }
    return {{QStringLiteral("kind"), kindName},
            {QStringLiteral("sceneId"), sceneId},
            {QStringLiteral("scene"), sceneName},
            {QStringLiteral("source"), source},
            {QStringLiteral("detail"), detail}};
}

bool Preflight::cameraMissing(const SourceDescriptor &desc)
{
    if (desc.kind != SourceDescriptor::Kind::Camera || desc.path.isEmpty())
        return false;
    const QString path = SharedSourcePool::instance().resolve(desc.path);
    const auto devices = QMediaDevices::videoInputs();
    // An empty list is the IPU6/MIPI case, where devices cannot be enumerated.
    if (devices.isEmpty())
        return false;
    for (const auto &dev : devices) {
        if (QString::fromUtf8(dev.id()) == path)
            return false;
    }
    return true;
}

QList<PreflightIssue> Preflight::run(const ShowManager &show, const QDir &sessionDir, const DeviceProbe &deviceMissing)
{
    using Kind = SourceDescriptor::Kind;
    const AssetPathResolver::Options opts{sessionDir};
    QList<PreflightIssue> issues;
    const int residentCount = int(std::count_if(show.scenes().cbegin(), show.scenes().cend(),
                                                [](const Scene *s) { return s->keepResident(); }));
    for (const Scene *scene : show.scenes()) {
        const ClipGraph *g = scene->graph();
        if (scene->keepResident() && residentCount > kMaxResidentScenes) {
            PreflightIssue issue;
            issue.kind = PreflightIssue::Kind::ResidentMemory;
            issue.sceneId = scene->id();
            issue.sceneName = scene->name();
            issue.source = QObject::tr("Keep resident");
            issue.detail = QObject::tr("%1 scenes are kept resident (more than %2); expect higher memory use")
                               .arg(residentCount).arg(kMaxResidentScenes);
            issues.append(issue);
        }
        qint64 historyBytes = 0;
        bool historyReduced = false;
        QStringList historyEffects;
        for (const NodeId id : g->nodeIdsOfType(ClipGraph::NodeType::Process)) {
            const ClipGraph::Node *node = g->node(id);
            const ShaderEffectSpec *spec = node ? ShaderEffects::byId(node->params.value(QStringLiteral("effect")).toInt()) : nullptr;
            if (!spec || (!spec->needsPrevFrame && spec->historyFrames <= 0))
                continue;
            const auto plan = EffectPassRunner::planHistory(show.canvasSize(), spec->historyFrames,
                                                            spec->needsPrevFrame,
                                                            EffectPassRunner::kHistorySourceBytes);
            historyBytes += plan.bytes;
            historyReduced |= !plan.ok || (plan.frames > 0 && plan.tile != show.canvasSize());
            if (!historyEffects.contains(spec->name))
                historyEffects.append(spec->name);
        }
        if (historyReduced || historyBytes > EffectPassRunner::kHistoryCapBytes / 2) {
            PreflightIssue issue;
            issue.kind = PreflightIssue::Kind::HistoryMemory;
            issue.sceneId = scene->id();
            issue.sceneName = scene->name();
            issue.source = historyEffects.join(QStringLiteral(", "));
            issue.detail = QObject::tr("Frame history needs about %1 MB at %2x%3 (cap %4 MB across the app); %5")
                               .arg(historyBytes >> 20).arg(show.canvasSize().width()).arg(show.canvasSize().height())
                               .arg(EffectPassRunner::kHistoryCapBytes >> 20)
                               .arg(historyReduced ? QObject::tr("history frames will be reduced in resolution or skipped")
                                                   : QObject::tr("consider a smaller canvas or fewer time effects"));
            issues.append(issue);
        }
        for (const NodeId id : g->nodeIdsOfType(ClipGraph::NodeType::Input)) {
            const SourceDescriptor desc = g->sourceDescriptor(id);
            PreflightIssue issue;
            issue.sceneId = scene->id();
            issue.sceneName = scene->name();
            issue.source = g->sourceName(id);
            if (desc.kind == Kind::Scene) {
                if (desc.sceneId.isEmpty())
                    continue;
                if (!show.scene(desc.sceneId)) {
                    issue.kind = PreflightIssue::Kind::MissingScene;
                    issue.detail = desc.displayName.isEmpty() ? desc.sceneId : desc.displayName;
                    issues.append(issue);
                } else if (show.wouldCreateSceneCycle(scene->id(), desc.sceneId)) {
                    issue.kind = PreflightIssue::Kind::SceneCycle;
                    issue.detail = show.scene(desc.sceneId)->name();
                    issues.append(issue);
                }
                continue;
            }
            if (desc.isFileSource() && !desc.path.isEmpty()) {
                const QString resolved = AssetPathResolver::resolvePath(desc.path, opts, desc.kind == Kind::Slideshow);
                const QFileInfo fi(resolved);
                if (desc.kind == Kind::Slideshow ? fi.isDir() : fi.isFile())
                    continue;
                issue.kind = PreflightIssue::Kind::MissingFile;
                issue.detail = desc.path;
                issues.append(issue);
            } else if (deviceMissing && deviceMissing(desc)) {
                issue.kind = PreflightIssue::Kind::MissingDevice;
                issue.detail = desc.path.isEmpty() ? desc.displayName : desc.path;
                issues.append(issue);
            }
        }
    }
    return issues;
}

QJsonArray Preflight::toJson(const QList<PreflightIssue> &issues)
{
    QJsonArray arr;
    for (const PreflightIssue &i : issues)
        arr.append(i.toJson());
    return arr;
}
