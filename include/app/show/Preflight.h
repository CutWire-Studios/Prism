#pragma once

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <functional>

#include "core/sources/SourceDescriptor.h"

class ShowManager;

struct PreflightIssue {
    enum class Kind { MissingFile, MissingDevice, MissingScene, SceneCycle };

    Kind kind = Kind::MissingFile;
    QString sceneId;
    QString sceneName;
    QString source;
    QString detail;

    QJsonObject toJson() const;
};

/// Walks every scene's input nodes and reports media files that cannot be found, capture
/// devices that are not present, and Scene sources whose scene was deleted or loops back.
namespace Preflight {

using DeviceProbe = std::function<bool(const SourceDescriptor &)>;

/// True when the descriptor names a camera that is not attached (after venue mapping).
bool cameraMissing(const SourceDescriptor &desc);

QList<PreflightIssue> run(const ShowManager &show, const QDir &sessionDir, const DeviceProbe &deviceMissing);

QJsonArray toJson(const QList<PreflightIssue> &issues);

}
