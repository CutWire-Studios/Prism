#include "core/project/ProjectPackager.h"

#include "core/project/OverlayItem.h"
#include "core/sources/SourceDescriptor.h"
#include "ui/nodes/ClipNodeModel.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTemporaryDir>
#include <QSet>
#include <zip.h>

namespace {

using Kind = SourceDescriptor::Kind;

QString sha256File(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

QString sanitizeName(const QString &name) {
    QString out = name;
    for (QChar &ch : out) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('-') && ch != QLatin1Char('_'))
            ch = QLatin1Char('_');
    }
    return out.isEmpty() ? QStringLiteral("asset") : out;
}

bool copyFile(const QString &src, const QString &dst, QString *error) {
    const QFileInfo dstInfo(dst);
    if (!QDir().mkpath(dstInfo.absolutePath())) {
        if (error) *error = QStringLiteral("Cannot create directory: %1").arg(dstInfo.absolutePath());
        return false;
    }
    if (QFile::exists(dst) && !QFile::remove(dst)) {
        if (error) *error = QStringLiteral("Cannot replace file: %1").arg(dst);
        return false;
    }
    if (!QFile::copy(src, dst)) {
        if (error) *error = QStringLiteral("Cannot copy %1 to %2").arg(src, dst);
        return false;
    }
    return true;
}

bool copyDirectoryRecursively(const QString &src, const QString &dst, QString *error) {
    const QDir srcDir(src);
    if (!srcDir.exists()) {
        if (error) *error = QStringLiteral("Missing folder: %1").arg(src);
        return false;
    }

    QDir dstDir(dst);
    if (!dstDir.exists() && !QDir().mkpath(dst)) {
        if (error) *error = QStringLiteral("Cannot create directory: %1").arg(dst);
        return false;
    }

    const QFileInfoList entries =
        srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString target = dstDir.filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectoryRecursively(entry.absoluteFilePath(), target, error))
                return false;
        } else if (!copyFile(entry.absoluteFilePath(), target, error)) {
            return false;
        }
    }
    return true;
}

void collectFilePath(const QString &path, QSet<QString> &files, QStringList *warnings) {
    if (path.isEmpty())
        return;
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        if (warnings)
            warnings->append(QStringLiteral("Missing file: %1").arg(path));
        return;
    }
    files.insert(fi.absoluteFilePath());
}

void collectDirectoryPath(const QString &path, QSet<QString> &directories, QStringList *warnings) {
    if (path.isEmpty())
        return;
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isDir()) {
        if (warnings)
            warnings->append(QStringLiteral("Missing folder: %1").arg(path));
        return;
    }
    directories.insert(fi.absoluteFilePath());
}

void collectFromDescriptor(const SourceDescriptor &desc,
                           QSet<QString> &files,
                           QSet<QString> &directories,
                           QStringList *warnings) {
    switch (desc.kind) {
    case Kind::VideoFile:
    case Kind::Image:
        collectFilePath(desc.path, files, warnings);
        break;
    case Kind::Slideshow:
        collectDirectoryPath(desc.path, directories, warnings);
        break;
    case Kind::Html:
        if (!desc.path.isEmpty())
            collectFilePath(desc.path, files, warnings);
        break;
    default:
        break;
    }
}

void collectFromSettings(const ClipSettings &settings,
                         QSet<QString> &files,
                         QSet<QString> &directories,
                         QStringList *warnings) {
    Q_UNUSED(directories);
    for (const OverlayItem &overlay : settings.overlays) {
        if (overlay.type == OverlayItem::Type::Image)
            collectFilePath(overlay.content, files, warnings);
    }
}

QString assignUniqueFilePath(const QString &absolutePath,
                             QMap<QString, QString> &assigned,
                             QSet<QString> &usedRelPaths) {
    if (assigned.contains(absolutePath))
        return assigned.value(absolutePath);

    const QFileInfo fi(absolutePath);
    const QString stem = sanitizeName(fi.completeBaseName());
    const QString suffix = fi.suffix().isEmpty() ? QString() : QStringLiteral(".") + fi.suffix();

    QString rel = QStringLiteral("%1/media/%2%3")
                      .arg(ProjectPackager::kAssetsDirName, stem, suffix);
    int counter = 2;
    while (usedRelPaths.contains(rel)) {
        rel = QStringLiteral("%1/media/%2_%3%4")
                  .arg(ProjectPackager::kAssetsDirName, stem)
                  .arg(counter++)
                  .arg(suffix);
    }

    usedRelPaths.insert(rel);
    assigned.insert(absolutePath, rel);
    return rel;
}

QString assignUniqueDirectoryPath(const QString &absolutePath,
                                  QMap<QString, QString> &assigned,
                                  QSet<QString> &usedRelPaths) {
    if (assigned.contains(absolutePath))
        return assigned.value(absolutePath);

    const QFileInfo fi(absolutePath);
    QString rel = QStringLiteral("%1/slideshows/%2")
                        .arg(ProjectPackager::kAssetsDirName, sanitizeName(fi.fileName()));
    int counter = 2;
    while (usedRelPaths.contains(rel)) {
        rel = QStringLiteral("%1/slideshows/%2_%3")
                  .arg(ProjectPackager::kAssetsDirName, sanitizeName(fi.fileName()))
                  .arg(counter++);
    }

    usedRelPaths.insert(rel);
    assigned.insert(absolutePath, rel);
    return rel;
}

void rewriteSourcePath(QJsonObject &source, const QMap<QString, QString> &pathMap) {
    const QString stored = source.value(QStringLiteral("path")).toString();
    if (stored.isEmpty())
        return;

    const QFileInfo fi(stored);
    const QString key = fi.isAbsolute() ? fi.absoluteFilePath() : stored;
    const auto it = pathMap.constFind(key);
    if (it != pathMap.constEnd())
        source.insert(QStringLiteral("path"), it.value());
}

void rewriteSettingsPaths(QJsonObject &settings, const QMap<QString, QString> &pathMap) {
    QJsonArray overlays = settings.value(QStringLiteral("overlays")).toArray();
    for (int i = 0; i < overlays.size(); ++i) {
        QJsonObject overlay = overlays.at(i).toObject();
        if (overlay.value(QStringLiteral("type")).toString() != QLatin1String("image"))
            continue;

        const QString stored = overlay.value(QStringLiteral("content")).toString();
        if (stored.isEmpty())
            continue;

        const QFileInfo fi(stored);
        const QString key = fi.isAbsolute() ? fi.absoluteFilePath() : stored;
        const auto it = pathMap.constFind(key);
        if (it != pathMap.constEnd())
            overlay.insert(QStringLiteral("content"), it.value());
        overlays[i] = overlay;
    }
    settings.insert(QStringLiteral("overlays"), overlays);
}

QJsonObject rewriteSessionPaths(const QJsonObject &session,
                                const QMap<QString, QString> &pathMap) {
    QJsonObject out = session;
    out.remove(QStringLiteral("sessionDir"));

    QJsonObject graph = out.value(QStringLiteral("graph")).toObject();
    QJsonArray clipNodes = graph.value(QStringLiteral("clipNodes")).toArray();
    for (int i = 0; i < clipNodes.size(); ++i) {
        QJsonObject node = clipNodes.at(i).toObject();
        QJsonObject source = node.value(QStringLiteral("source")).toObject();
        rewriteSourcePath(source, pathMap);
        node.insert(QStringLiteral("source"), source);

        QJsonObject settings = node.value(QStringLiteral("settings")).toObject();
        rewriteSettingsPaths(settings, pathMap);
        node.insert(QStringLiteral("settings"), settings);

        clipNodes[i] = node;
    }
    graph.insert(QStringLiteral("clipNodes"), clipNodes);
    out.insert(QStringLiteral("graph"), graph);
    return out;
}

QJsonArray buildManifestFiles(const QDir &root) {
    QJsonArray files;
    QDirIterator it(root.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString absPath = it.filePath();
        const QString relPath = root.relativeFilePath(absPath);

        QJsonObject entry;
        entry.insert(QStringLiteral("path"), relPath);
        entry.insert(QStringLiteral("size"), static_cast<qint64>(QFileInfo(absPath).size()));
        entry.insert(QStringLiteral("sha256"), sha256File(absPath));
        files.append(entry);
    }
    return files;
}

bool writeJsonFile(const QJsonObject &json, const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("Cannot write file: %1").arg(path);
        return false;
    }
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    return true;
}

bool createZipArchive(const QDir &root, const QString &zipPath, QString *error) {
    int zipError = 0;
    zip_t *archive = zip_open(zipPath.toUtf8().constData(), ZIP_CREATE | ZIP_TRUNCATE, &zipError);
    if (!archive) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, zipError);
        if (error) *error = QStringLiteral("Cannot create archive: %1").arg(zip_error_strerror(&ze));
        zip_error_fini(&ze);
        return false;
    }

    QDirIterator it(root.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString absPath = it.filePath();
        const QByteArray relPath = root.relativeFilePath(absPath).toUtf8();

        zip_source_t *source = zip_source_file(archive, absPath.toUtf8().constData(), 0, -1);
        if (!source) {
            if (error) *error = QStringLiteral("Cannot read file for archive: %1").arg(absPath);
            zip_close(archive);
            return false;
        }

        if (zip_file_add(archive, relPath.constData(), source,
                         ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0) {
            if (error)
                *error = QStringLiteral("Cannot add %1 to archive: %2")
                             .arg(QString::fromUtf8(relPath), zip_strerror(archive));
            zip_source_free(source);
            zip_close(archive);
            return false;
        }
    }

    if (zip_close(archive) != 0) {
        if (error) *error = QStringLiteral("Cannot finalize archive: %1").arg(zipPath);
        return false;
    }
    return true;
}

bool extractZipArchive(const QString &zipPath, const QDir &destDir, QString *error) {
    int zipError = 0;
    zip_t *archive = zip_open(zipPath.toUtf8().constData(), ZIP_RDONLY, &zipError);
    if (!archive) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, zipError);
        if (error) *error = QStringLiteral("Cannot open archive: %1").arg(zip_error_strerror(&ze));
        zip_error_fini(&ze);
        return false;
    }

    const zip_int64_t entryCount = zip_get_num_entries(archive, 0);
    for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(entryCount); ++i) {
        const char *name = zip_get_name(archive, i, 0);
        if (!name) {
            if (error) *error = QStringLiteral("Invalid archive entry at index %1").arg(i);
            zip_close(archive);
            return false;
        }

        const QString outPath = destDir.filePath(QString::fromUtf8(name));
        if (!QDir().mkpath(QFileInfo(outPath).absolutePath())) {
            if (error) *error = QStringLiteral("Cannot create directory for: %1").arg(outPath);
            zip_close(archive);
            return false;
        }

        zip_file_t *entry = zip_fopen_index(archive, i, 0);
        if (!entry) {
            if (error) *error = QStringLiteral("Cannot open archive entry: %1").arg(name);
            zip_close(archive);
            return false;
        }

        QFile outFile(outPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) *error = QStringLiteral("Cannot write extracted file: %1").arg(outPath);
            zip_fclose(entry);
            zip_close(archive);
            return false;
        }

        char buffer[64 * 1024];
        zip_int64_t bytesRead = 0;
        while ((bytesRead = zip_fread(entry, buffer, sizeof(buffer))) > 0) {
            if (outFile.write(buffer, bytesRead) != bytesRead) {
                if (error) *error = QStringLiteral("Write failed while extracting: %1").arg(outPath);
                zip_fclose(entry);
                zip_close(archive);
                return false;
            }
        }

        zip_fclose(entry);
        if (bytesRead < 0) {
            if (error) *error = QStringLiteral("Read failed while extracting: %1").arg(outPath);
            zip_close(archive);
            return false;
        }
    }

    if (zip_close(archive) != 0) {
        if (error) *error = QStringLiteral("Cannot close archive: %1").arg(zipPath);
        return false;
    }
    return true;
}

bool verifyManifest(const QDir &root, const QJsonObject &manifest, QString *error) {
    const QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    for (const QJsonValue &value : files) {
        const QJsonObject entry = value.toObject();
        const QString relPath = entry.value(QStringLiteral("path")).toString();
        const qint64 expectedSize = entry.value(QStringLiteral("size")).toInteger(-1);
        const QString expectedHash = entry.value(QStringLiteral("sha256")).toString();

        const QString absPath = root.filePath(relPath);
        const QFileInfo fi(absPath);
        if (!fi.isFile()) {
            if (error) *error = QStringLiteral("Missing packaged file: %1").arg(relPath);
            return false;
        }
        if (expectedSize >= 0 && fi.size() != expectedSize) {
            if (error)
                *error = QStringLiteral("Size mismatch for %1").arg(relPath);
            return false;
        }
        if (!expectedHash.isEmpty()) {
            const QString actualHash = sha256File(absPath);
            if (actualHash != expectedHash) {
                if (error) *error = QStringLiteral("Checksum mismatch for %1").arg(relPath);
                return false;
            }
        }
    }
    return true;
}

} // namespace

ProjectPackager::Report ProjectPackager::exportPackage(const QJsonObject &sessionJson,
                                                       const QVector<ClipNodeModel *> &nodes,
                                                       const QString &outputPath) {
    Report report;

    QSet<QString> files;
    QSet<QString> directories;
    for (ClipNodeModel *model : nodes) {
        if (!model)
            continue;
        collectFromDescriptor(model->sourceDescriptor(), files, directories, &report.warnings);
        collectFromSettings(model->settings(), files, directories, &report.warnings);
    }

    QTemporaryDir staging;
    if (!staging.isValid()) {
        report.error = QStringLiteral("Cannot create temporary export directory.");
        return report;
    }

    const QDir stagingDir(staging.path());
    QMap<QString, QString> pathMap;
    QSet<QString> usedRelPaths;

    for (const QString &absPath : files) {
        const QString relPath = assignUniqueFilePath(absPath, pathMap, usedRelPaths);
        if (!copyFile(absPath, stagingDir.filePath(relPath), &report.error))
            return report;
    }

    for (const QString &absPath : directories) {
        const QString relPath = assignUniqueDirectoryPath(absPath, pathMap, usedRelPaths);
        if (!copyDirectoryRecursively(absPath, stagingDir.filePath(relPath), &report.error))
            return report;
    }

    report.assetCount = pathMap.size();

    const QJsonObject packagedSession = rewriteSessionPaths(sessionJson, pathMap);
    const QString sessionPath = stagingDir.filePath(QString::fromUtf8(kSessionName));
    if (!writeJsonFile(packagedSession, sessionPath, &report.error))
        return report;

    QJsonObject manifest;
    manifest.insert(QStringLiteral("format"), QStringLiteral("prism-project"));
    manifest.insert(QStringLiteral("formatVersion"), kFormatVersion);
    manifest.insert(QStringLiteral("createdAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    manifest.insert(QStringLiteral("sessionFile"), QString::fromUtf8(kSessionName));
    manifest.insert(QStringLiteral("assetsDir"), QString::fromUtf8(kAssetsDirName));
    manifest.insert(QStringLiteral("files"), buildManifestFiles(stagingDir));

    if (!writeJsonFile(manifest, stagingDir.filePath(QString::fromUtf8(kManifestName)), &report.error))
        return report;

    if (!createZipArchive(stagingDir, outputPath, &report.error))
        return report;

    report.success = true;
    return report;
}

ProjectPackager::ImportResult ProjectPackager::importPackage(const QString &packagePath,
                                                               const QString &extractDir,
                                                               bool verifyIntegrity) {
    ImportResult result;

    const QFileInfo packageInfo(packagePath);
    if (!packageInfo.isFile()) {
        result.error = QStringLiteral("Package not found: %1").arg(packagePath);
        return result;
    }

    QDir destDir(extractDir);
    if (!destDir.exists() && !QDir().mkpath(extractDir)) {
        result.error = QStringLiteral("Cannot create extraction directory: %1").arg(extractDir);
        return result;
    }

    if (!extractZipArchive(packagePath, destDir, &result.error))
        return result;

    const QString manifestPath = destDir.filePath(QString::fromUtf8(kManifestName));
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Missing manifest in package.");
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument manifestDoc =
        QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !manifestDoc.isObject()) {
        result.error = QStringLiteral("Invalid manifest: %1").arg(parseError.errorString());
        return result;
    }

    const QJsonObject manifest = manifestDoc.object();
    if (manifest.value(QStringLiteral("format")).toString() != QLatin1String("prism-project")) {
        result.error = QStringLiteral("Unsupported package format.");
        return result;
    }
    if (manifest.value(QStringLiteral("formatVersion")).toInt() != kFormatVersion) {
        result.error = QStringLiteral("Unsupported package version.");
        return result;
    }

    if (verifyIntegrity && !verifyManifest(destDir, manifest, &result.error))
        return result;

    const QString sessionName = manifest.value(QStringLiteral("sessionFile")).toString(
        QString::fromUtf8(kSessionName));
    const QString sessionPath = destDir.filePath(sessionName);
    if (!QFileInfo(sessionPath).isFile()) {
        result.error = QStringLiteral("Missing session file in package.");
        return result;
    }

    result.sessionPath = sessionPath;
    result.assetCount = manifest.value(QStringLiteral("files")).toArray().size();
    result.success = true;
    return result;
}
