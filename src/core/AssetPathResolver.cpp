#include "core/AssetPathResolver.h"

#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

namespace {

bool pathExists(const QString &path, bool isDirectory) {
    if (path.isEmpty()) return false;
    const QFileInfo fi(path);
    return isDirectory ? fi.isDir() : fi.isFile();
}

QString searchByBasename(const QDir &root, const QString &basename, bool isDirectory) {
    if (basename.isEmpty()) return {};

    QDirIterator it(root.absolutePath(), QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.fileName() != basename) continue;
        if (isDirectory ? fi.isDir() : fi.isFile())
            return fi.absoluteFilePath();
    }
    return {};
}

QString promptForPath(const QString &label, const AssetPathResolver::Options &opts,
                      bool isDirectory) {
    if (!opts.allowUserPrompt || !opts.dialogParent) return {};

    const QString title = QStringLiteral("Relink Missing Asset");
    const QString msg = QStringLiteral("Could not find:\n%1\n\nLocate the file or folder manually?")
                            .arg(label);
    if (QMessageBox::question(opts.dialogParent, title, msg,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::Yes) != QMessageBox::Yes)
        return {};

    if (isDirectory)
        return QFileDialog::getExistingDirectory(opts.dialogParent, title,
                                                 opts.sessionDir.absolutePath());
    return QFileDialog::getOpenFileName(opts.dialogParent, title,
                                        opts.sessionDir.absolutePath());
}

void noteRelink(AssetPathResolver::RelinkReport *report, const QString &label, bool found) {
    if (!report) return;
    if (found) {
        ++report->resolved;
    } else {
        ++report->stillMissing;
        report->notes.append(label);
    }
}

} // namespace

QString AssetPathResolver::storePath(const QString &absolutePath, const QDir &sessionDir) {
    if (absolutePath.isEmpty()) return absolutePath;

    const QFileInfo fi(absolutePath);
    if (!fi.isAbsolute()) return absolutePath;

    const QString rel = sessionDir.relativeFilePath(fi.absoluteFilePath());
    if (!rel.startsWith(QLatin1String("..")) && !QDir::isAbsolutePath(rel))
        return rel;
    return fi.absoluteFilePath();
}

QString AssetPathResolver::resolvePath(const QString &storedPath, const Options &opts,
                                       bool isDirectory) {
    if (storedPath.isEmpty()) return storedPath;

    if (pathExists(storedPath, isDirectory))
        return QFileInfo(storedPath).absoluteFilePath();

    if (!opts.sessionDir.isAbsolute())
        return storedPath;

    const QString relativeCandidate = opts.sessionDir.absoluteFilePath(storedPath);
    if (pathExists(relativeCandidate, isDirectory))
        return QFileInfo(relativeCandidate).absoluteFilePath();

    const QString basename = QFileInfo(storedPath).fileName();
    const QString found = searchByBasename(opts.sessionDir, basename, isDirectory);
    if (!found.isEmpty())
        return found;

    const QString prompted = promptForPath(storedPath, opts, isDirectory);
    if (!prompted.isEmpty() && pathExists(prompted, isDirectory))
        return QFileInfo(prompted).absoluteFilePath();

    return storedPath;
}

SourceDescriptor AssetPathResolver::relinkDescriptor(const SourceDescriptor &desc,
                                                     const Options &opts,
                                                     RelinkReport *report) {
    SourceDescriptor out = desc;
    using Kind = SourceDescriptor::Kind;

    switch (desc.kind) {
    case Kind::VideoFile:
    case Kind::Image: {
        const QString resolved = resolvePath(desc.path, opts, false);
        out.path = resolved;
        noteRelink(report, desc.displayName.isEmpty() ? desc.path : desc.displayName,
                   pathExists(resolved, false));
        break;
    }
    case Kind::Slideshow: {
        const QString resolved = resolvePath(desc.path, opts, true);
        out.path = resolved;
        noteRelink(report, desc.displayName.isEmpty() ? desc.path : desc.displayName,
                   pathExists(resolved, true));
        break;
    }
    case Kind::Html: {
        if (!desc.path.isEmpty()) {
            const QString resolved = resolvePath(desc.path, opts, false);
            out.path = resolved;
            noteRelink(report, desc.path, pathExists(resolved, false));
        }
        break;
    }
    default:
        break;
    }

    return out;
}

ClipSettings AssetPathResolver::relinkSettings(const ClipSettings &settings, const Options &opts,
                                               RelinkReport *report) {
    ClipSettings out = settings;
    for (OverlayItem &ov : out.overlays) {
        if (ov.type != OverlayItem::Type::Image || ov.content.isEmpty()) continue;
        const QString resolved = resolvePath(ov.content, opts, false);
        noteRelink(report, ov.content, pathExists(resolved, false));
        ov.content = resolved;
    }
    return out;
}
