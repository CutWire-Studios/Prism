#pragma once

#include <QString>

// Remembers the OS screen-capture selection so that recreating a screen/window
// source (deck assignment, moving between decks, preview rebuilds) does not
// re-prompt the user.
//
// On Linux this stores the xdg-desktop-portal ScreenCast `restore_token` keyed
// by a stable per-source capture id. Tokens are machine-local (a portal token
// is only meaningful on the machine that issued it), so they live in QSettings
// rather than the portable session file.
namespace ScreenCapturePersist {

// Returns the stored restore token for a capture id, or an empty string if none
// is known (or the id is empty).
QString restoreToken(const QString &captureId);

// Stores/updates the restore token for a capture id. No-op for an empty id.
void setRestoreToken(const QString &captureId, const QString &token);

// Forgets the token for a capture id so the next capture re-prompts.
void clearRestoreToken(const QString &captureId);

} // namespace ScreenCapturePersist
