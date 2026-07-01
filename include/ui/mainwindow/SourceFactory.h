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

    /// Build a NodeChainSource entry for one resolved layer.
    static VideoWidget::NodeChainSource makeLayerEntry(const ResolvedLayer &layer,
                                                       ClipNodeEditor *editor);

    /// Build the full deck stream (index 0 = bottom/deck-primary, 1..N = overlays).
    static std::vector<VideoWidget::NodeChainSource>
    buildStream(const ResolvedStream &stream, ClipNodeEditor *editor);
};
