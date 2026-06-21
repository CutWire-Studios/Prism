#include "ui/mainwindow/MainWindowUtils.h"

namespace MainWindowUtils {

QString formatRecordingElapsed(qint64 ms)
{
    const qint64 totalSecs = ms / 1000;
    const int hours   = static_cast<int>(totalSecs / 3600);
    const int minutes = static_cast<int>((totalSecs % 3600) / 60);
    const int seconds = static_cast<int>(totalSecs % 60);
    return QStringLiteral("%1:%2:%3")
        .arg(hours,   2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

QString ensureExtension(const QString &path, const QString &ext)
{
    if (path.endsWith(ext, Qt::CaseInsensitive))
        return path;
    return path + ext;
}

QStringList diffNewItems(const QStringList &before, const QStringList &after)
{
    QStringList added;
    for (const QString &item : after) {
        if (!before.contains(item))
            added << item;
    }
    return added;
}

PanicState panicStateForButtons(bool blackout, bool stayTuned, bool pause)
{
    if (blackout)
        return PanicState::Blackout;
    if (stayTuned)
        return PanicState::StayTuned;
    if (pause)
        return PanicState::Freeze;
    return PanicState::None;
}

bool isBackwardJump(double current, double last, double threshold)
{
    return current < last - threshold;
}

} // namespace MainWindowUtils
