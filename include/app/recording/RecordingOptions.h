#pragma once

#include <QString>

/// Persisted recording preferences.
struct RecordingOptions {
    QString outputDir;
    /// "h264" (default), "hevc" or "av1". ProgramRecorder reads the same setting when it starts.
    QString videoCodec = QStringLiteral("h264");
    /// "mkv" (default) or "mp4".
    QString container = QStringLiteral("mkv");

    QString effectiveOutputDir() const;

    static RecordingOptions loadSaved();
    static void saveOutputDir(const QString &dir);
    /// True once the user has explicitly picked an output folder.
    static bool hasChosenOutputDir();
};
