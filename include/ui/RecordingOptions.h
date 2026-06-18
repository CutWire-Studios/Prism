#pragma once

#include <QString>
#include <QVector>
#include <QMap>
#include "ui/ClipNodeModel.h"

struct RecordingSourceTarget {
    NodeId  nodeId = 0;
    QString label;
};

/// User-selected recording targets (program mix, deck isos, live sources).
struct RecordingOptions {
    bool recordProgram = true;
    bool recordDeckA   = false;
    bool recordDeckB   = false;
    QVector<RecordingSourceTarget> recordSources;
    QString outputDir;

    bool hasAnyTarget() const {
        return recordProgram || recordDeckA || recordDeckB || !recordSources.isEmpty();
    }
};
