#pragma once

#include <QString>

/// Persisted recording preferences (output folder only).
struct RecordingOptions {
    QString outputDir;

    QString effectiveOutputDir() const;
};
