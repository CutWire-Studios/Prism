#include "app/output/FrameCaptureHelper.h"
#include "core/render/Compositor.h"
#include "app/mainwindow/DeckController.h"
#include "core/sources/MediaSource.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

QImage FrameCaptureHelper::frameFromSource(const MediaSource *source) {
    if (!source || !source->isReady())
        return {};

    const QSize sz = source->frameSize();
    if (sz.isEmpty())
        return {};

    if (source->hasAlpha()) {
        return QImage(source->frameData(), sz.width(), sz.height(),
                      source->frameBytesPerLine(), QImage::Format_RGBA8888).copy();
    }

    return QImage(source->frameData(), sz.width(), sz.height(),
                  source->frameBytesPerLine(), QImage::Format_RGB888).copy();
}

QString FrameCaptureHelper::capturesDirectory(const QString &baseDir) {
    const QString dir = QDir(baseDir).filePath(QStringLiteral("Captures"));
    QDir().mkpath(dir);
    return dir;
}

QString FrameCaptureHelper::savePng(const QImage &image, const QString &baseName, const QString &baseDir) {
    if (image.isNull())
        return {};

    QString safe = baseName;
    safe.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9._-]+")), QStringLiteral("_"));
    if (safe.isEmpty())
        safe = QStringLiteral("capture");

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    const QString path  = QDir(capturesDirectory(baseDir)).filePath(stamp + QStringLiteral("_") + safe + QStringLiteral(".png"));

    QImage rgba = image;
    if (rgba.format() != QImage::Format_RGBA8888)
        rgba = rgba.convertToFormat(QImage::Format_RGBA8888);
    if (!rgba.save(path, "PNG"))
        return {};

    return path;
}

QList<FrameCaptureHelper::LayerRef> FrameCaptureHelper::enumerateLayers(
    Compositor *output, ClipGraph *graph, DeckController *decks)
{
    QList<LayerRef> layers;
    if (!output || !graph || !decks)
        return layers;

    layers.append({LayerKind::Program, false, -1, 0,
                   QStringLiteral("Program Output (full mix)")});

    const auto addDeck = [&](bool deckA) {
        const NodeId deckNodeId = deckA ? decks->activeNodeA() : decks->activeNodeB();
        if (!deckNodeId)
            return;

        if (!graph->hasNode(deckNodeId))
            return;

        const QString deckLabel = deckA ? QStringLiteral("A") : QStringLiteral("B");
        layers.append({LayerKind::DeckBase, deckA, -1, deckNodeId,
                       QStringLiteral("Deck %1 — %2").arg(deckLabel, graph->sourceName(deckNodeId))});

        const auto &chainSources = deckA ? output->chainSourcesA() : output->chainSourcesB();
        for (int i = 0; i < static_cast<int>(chainSources.size()); ++i) {
            layers.append({LayerKind::DeckChain, deckA, i, deckNodeId,
                           QStringLiteral("Deck %1 overlay %2").arg(deckLabel).arg(i + 1)});
        }
    };

    addDeck(true);
    addDeck(false);
    return layers;
}

QImage FrameCaptureHelper::captureLayer(Compositor *output, const LayerRef &layer) {
    if (!output)
        return {};

    switch (layer.kind) {
    case LayerKind::Program:
        return output->captureProgramFrame();
    case LayerKind::DeckBase:
        return frameFromSource(layer.deckA ? output->sourceA() : output->sourceB());
    case LayerKind::DeckChain: {
        const auto &chain = layer.deckA ? output->chainSourcesA() : output->chainSourcesB();
        if (layer.chainIndex < 0 || layer.chainIndex >= static_cast<int>(chain.size()))
            return {};
        const MediaSource *src = chain[static_cast<size_t>(layer.chainIndex)].source.get();
        return frameFromSource(src);
    }
    }
    return {};
}
