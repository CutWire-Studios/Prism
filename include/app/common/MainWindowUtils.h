#pragma once

#include <QString>
#include <QStringList>

namespace MainWindowUtils {

QString formatRecordingElapsed(qint64 ms);
QString ensureExtension(const QString &path, const QString &ext);
QStringList diffNewItems(const QStringList &before, const QStringList &after);

enum class PanicState { None, Blackout, StayTuned, Freeze };

PanicState panicStateForButtons(bool blackout, bool stayTuned, bool pause);
bool       isBackwardJump(double current, double last, double threshold = 0.2);

} // namespace MainWindowUtils
