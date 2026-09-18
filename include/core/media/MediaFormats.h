#pragma once

#include <QString>
#include <QStringList>

// The one place that decides which file suffixes count as media. File dialogs,
// folder import and source routing all read these lists.
namespace MediaFormats {

const QStringList &videoExtensions();
const QStringList &audioExtensions();
const QStringList &imageExtensions();

bool isVideoPath(const QString &path);
bool isAudioPath(const QString &path);
bool isImagePath(const QString &path);
bool isMediaPath(const QString &path);

// "*.mp4 *.mov ..." for the given lists.
QString globPattern(bool video, bool image, bool audio);
// ["*.mp4", "*.mov", ...] for QDir::setNameFilters.
QStringList nameFilters(bool video, bool image, bool audio);

} // namespace MediaFormats
