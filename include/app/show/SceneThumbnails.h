#pragma once

#include <QImage>
#include <QString>

class ShowManager;
class QQuickImageProvider;

/// Scene thumbnail files: ~160x90 PNGs named after the scene id.
namespace SceneThumbnails {

constexpr int kWidth = 160;
constexpr int kHeight = 90;

/// Scales @p frame and writes it to `<dir>/<sceneId>.png`; returns the path, or empty when the
/// frame is null or the write fails.
QString save(const QImage &frame, const QString &dir, const QString &sceneId);

/// Serves `image://scenethumb/<sceneId>` from the thumbnail path stored on each scene.
QQuickImageProvider *createProvider(const ShowManager *show);

}
