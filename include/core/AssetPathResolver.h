#pragma once

#include <QString>
#include <QStringList>
#include <QDir>
#include <QWidget>

#include "core/SourceDescriptor.h"
#include "core/OverlayItem.h"

/// Resolves media paths when loading session files and stores them portably on save.
class AssetPathResolver {
public:
    struct Options {
        QDir sessionDir;
        QWidget *dialogParent = nullptr;
        bool allowUserPrompt  = true;
    };

    struct RelinkReport {
        int resolved      = 0;
        int stillMissing    = 0;
        QStringList notes;
    };

    /// Store an absolute path relative to the session directory when possible.
    static QString storePath(const QString &absolutePath, const QDir &sessionDir);

    /// Resolve a stored path: absolute if present, relative to session dir, search by name, then prompt.
    static QString resolvePath(const QString &storedPath, const Options &opts,
                               bool isDirectory = false);

    static SourceDescriptor relinkDescriptor(const SourceDescriptor &desc, const Options &opts,
                                             RelinkReport *report = nullptr);
    static ClipSettings relinkSettings(const ClipSettings &settings, const Options &opts,
                                       RelinkReport *report = nullptr);
};
