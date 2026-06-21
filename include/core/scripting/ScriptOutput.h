#pragma once

#include <QMutex>
#include <QString>
#include <atomic>
#include <memory>

// Thread-safe JSON payload produced by ScriptRuntime, consumed by TextSource
// (and future data-driven nodes).
struct ScriptOutput {
    QMutex mutex;
    QString json;
    std::atomic<uint> version{0};
};
