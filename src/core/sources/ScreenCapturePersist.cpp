#include "core/sources/ScreenCapturePersist.h"

#include <QSettings>

namespace {
constexpr const char *kOrg   = "Prism";
constexpr const char *kApp   = "ScreenCapture";
constexpr const char *kGroup = "restoreTokens";

QString settingsKey(const QString &captureId) {
    return QStringLiteral("%1/%2").arg(kGroup, captureId);
}
} // namespace

namespace ScreenCapturePersist {

QString restoreToken(const QString &captureId) {
    if (captureId.isEmpty())
        return {};
    QSettings settings(kOrg, kApp);
    return settings.value(settingsKey(captureId)).toString();
}

void setRestoreToken(const QString &captureId, const QString &token) {
    if (captureId.isEmpty())
        return;
    QSettings settings(kOrg, kApp);
    if (token.isEmpty())
        settings.remove(settingsKey(captureId));
    else
        settings.setValue(settingsKey(captureId), token);
}

void clearRestoreToken(const QString &captureId) {
    if (captureId.isEmpty())
        return;
    QSettings settings(kOrg, kApp);
    settings.remove(settingsKey(captureId));
}

} // namespace ScreenCapturePersist
