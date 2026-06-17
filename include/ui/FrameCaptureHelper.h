#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include "ui/ClipNodeModel.h"

class ClipNodeEditor;
class DeckController;
class MediaSource;
class VideoWidget;

/// Captures still frames from program output or individual compositor layers.
class FrameCaptureHelper {
public:
    enum class LayerKind { Program, DeckBase, DeckChain };

    struct LayerRef {
        LayerKind kind   = LayerKind::Program;
        bool      deckA  = true;
        int       chainIndex = -1;
        NodeId    nodeId = 0;
        QString   label;
    };

    static QImage frameFromSource(const MediaSource *source);
    static QString capturesDirectory();
    static QString savePng(const QImage &image, const QString &baseName);

    static QList<LayerRef> enumerateLayers(VideoWidget     *output,
                                           ClipNodeEditor  *editor,
                                           DeckController  *decks);

    static QImage captureLayer(VideoWidget *output, const LayerRef &layer);
};
