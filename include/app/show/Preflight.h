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
    enum class Kind { MissingFile, MissingDevice };

    Kind kind = Kind::MissingFile;
    QString cueId;
    QString cueName;
    QString source;
    QString detail;

    QJsonObject toJson() const;
};

/// Walks every cue's input nodes and reports media files that cannot be found and capture
/// devices that are not present.
namespace Preflight {

using DeviceProbe = std::function<bool(const SourceDescriptor &)>;

/// True when the descriptor names a camera that is not attached (after venue mapping).
bool cameraMissing(const SourceDescriptor &desc);

QList<PreflightIssue> run(const ShowManager &show, const QDir &sessionDir, const DeviceProbe &deviceMissing);

QJsonArray toJson(const QList<PreflightIssue> &issues);

}
