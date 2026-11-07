#include "app/show/Preflight.h"

#include "app/show/ShowManager.h"
#include "core/graph/ClipGraph.h"
#include "core/project/AssetPathResolver.h"
#include "core/sources/SharedSourcePool.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QCameraDevice>
#include <QMediaDevices>

QJsonObject PreflightIssue::toJson() const
{
    return {{QStringLiteral("kind"), kind == Kind::MissingFile ? QStringLiteral("missing_file")
                                                                : QStringLiteral("missing_device")},
            {QStringLiteral("cueId"), cueId},
            {QStringLiteral("cue"), cueName},
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
    for (const Cue *cue : show.cues()) {
        const ClipGraph *g = cue->graph();
        for (const NodeId id : g->nodeIdsOfType(ClipGraph::NodeType::Input)) {
            const SourceDescriptor desc = g->sourceDescriptor(id);
            PreflightIssue issue;
            issue.cueId = cue->id();
            issue.cueName = cue->name();
            issue.source = g->sourceName(id);
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
