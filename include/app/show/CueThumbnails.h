#pragma once

#include <QImage>
#include <QString>

class ShowManager;
class QQuickImageProvider;

/// Cue thumbnail files: ~160x90 PNGs named after the cue id.
namespace CueThumbnails {

constexpr int kWidth = 160;
constexpr int kHeight = 90;

/// Scales @p frame and writes it to `<dir>/<cueId>.png`; returns the path, or empty when the
/// frame is null or the write fails.
QString save(const QImage &frame, const QString &dir, const QString &cueId);

/// Serves `image://cuethumb/<cueId>` from the thumbnail path stored on each cue.
QQuickImageProvider *createProvider(const ShowManager *show);

}
