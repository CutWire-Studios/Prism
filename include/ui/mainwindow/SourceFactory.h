#pragma once

#include <memory>
#include "core/sources/MediaSource.h"
#include "core/sources/SourceDescriptor.h"
#include "ui/canvas/VideoWidget.h"
#include "ui/nodes/ClipNodeEditor.h"

/// Factory that creates MediaSource instances from a SourceDescriptor.
/// Centralises the duplicated switch-statements that existed both in
/// MainWindow::assignNodeToDeck() and the file-scope makeNodeChainSource().
class SourceFactory {
public:
    SourceFactory() = delete;

    /// Create a ready-to-play MediaSource from a descriptor.
    /// Returns nullptr if the descriptor cannot produce a source
    /// (e.g. transparent canvas, bad file path).
    static std::unique_ptr<MediaSource> create(const SourceDescriptor &desc);

    /// Build a NodeChainSource entry (used by VideoWidget overlay chain).
    static VideoWidget::NodeChainSource makeChainEntry(ClipNodeModel *node,
                                                       ClipNodeEditor *editor);

    /// Build the full overlay chain starting upstream from fromClip.
    static std::vector<VideoWidget::NodeChainSource>
    buildChain(const QVector<ClipNodeModel *> &chain,
               ClipNodeEditor *editor,
               int canvasWidth = 0, int canvasHeight = 0);
};
