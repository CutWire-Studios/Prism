#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include "core/Deck.h"
#include "core/Deck.h"
#include "core/graph/ClipGraph.h"

class Compositor;
class DeckController;
class MediaSource;

/// Captures still frames from program output or individual compositor layers.
class FrameCaptureHelper {
public:
    enum class LayerKind { Program, DeckBase, DeckChain };

    struct LayerRef {
        LayerKind kind   = LayerKind::Program;
        Deck      deck   = Deck::Live;
        int       chainIndex = -1;
        NodeId    nodeId = 0;
        QString   label;
    };

    static QImage frameFromSource(const MediaSource *source);
    /// PNG captures are written to a "Captures" subfolder of \a baseDir (the
    /// user-chosen recording output folder).
    static QString capturesDirectory(const QString &baseDir);
    static QString savePng(const QImage &image, const QString &baseName, const QString &baseDir);

    static QList<LayerRef> enumerateLayers(Compositor      *output,
                                           ClipGraph       *graph,
                                           DeckController  *decks);

    static QImage captureLayer(Compositor *output, const LayerRef &layer);
};
