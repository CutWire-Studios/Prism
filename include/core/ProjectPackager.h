#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

class ClipNodeModel;

/// Bundles a SwitchX session and its media into a portable `.switch` ZIP archive.
///
/// Archive layout:
///   manifest.json
///   session.sxs
///   assets/...
class ProjectPackager {
public:
    struct Report {
        bool        success = false;
        QString     error;
        QStringList warnings;
        int         assetCount = 0;
    };

    struct ImportResult : Report {
        QString sessionPath;
    };

    static constexpr const char *kExtension      = ".switch";
    static constexpr int         kFormatVersion  = 1;
    static constexpr const char *kManifestName   = "manifest.json";
    static constexpr const char *kSessionName    = "session.sxs";
    static constexpr const char *kAssetsDirName  = "assets";

    static Report exportPackage(const QJsonObject &sessionJson,
                                const QVector<ClipNodeModel *> &nodes,
                                const QString &outputPath);

    static ImportResult importPackage(const QString &packagePath,
                                      const QString &extractDir,
                                      bool verifyIntegrity = true);
};
