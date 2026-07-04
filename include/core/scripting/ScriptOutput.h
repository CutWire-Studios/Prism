#pragma once

#include <QMutex>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

// Thread-safe JSON payload produced by ScriptRuntime, consumed by TextSource
// (and future data-driven nodes).
struct ScriptOutput {
    QMutex mutex;
    QString json;
    std::atomic<uint> version{0};
};

// Snapshot handed to editors of data-driven sources when their DataIn port is
// wired to a Script node: the script source (for variable discovery), the live
// output, and a way to trigger a run.
struct ScriptBinding {
    QString code;
    std::shared_ptr<ScriptOutput> output;
    std::function<void()> requestRun;

    bool connected() const { return output != nullptr; }
};
