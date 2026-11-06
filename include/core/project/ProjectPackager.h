#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

class ClipGraph;

/// Bundles a CutWire Prism session and its media into a portable `.prism` ZIP archive.
///
/// Archive layout:
///   manifest.json
///   session.psm
///   venue.json   (optional)
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

    static constexpr const char *kExtension      = ".prism";
    static constexpr int         kFormatVersion  = 3;
    static constexpr const char *kManifestName   = "manifest.json";
    static constexpr const char *kSessionName    = "session.psm";
    static constexpr const char *kAssetsDirName  = "assets";
    static constexpr const char *kVenueName      = "venue.json";

    static Report exportPackage(const QJsonObject &sessionJson,
                                const QVector<const ClipGraph *> &graphs,
                                const QString &outputPath,
                                const QJsonObject &venue = {});

    static ImportResult importPackage(const QString &packagePath,
                                      const QString &extractDir,
                                      bool verifyIntegrity = true);
};
