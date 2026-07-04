#include "core/platform/MacPermissions.h"

#include <QtGlobal>

#ifdef Q_OS_MACOS

#include <QCoreApplication>
#include <QPermissions>
#include <QDebug>

#include <CoreGraphics/CoreGraphics.h>

namespace MacPermissions {

void requestCameraAndMicrophone() {
    // Ask only when Undetermined; if already Granted/Denied the OS won't reprompt
    // anyway, and requesting a Denied permission is a no-op.
    const QCameraPermission camera;
    if (qApp->checkPermission(camera) == Qt::PermissionStatus::Undetermined) {
        qApp->requestPermission(camera, [](const QPermission &p) {
            qInfo() << "Camera permission:" << int(p.status());
        });
    }

    const QMicrophonePermission microphone;
    if (qApp->checkPermission(microphone) == Qt::PermissionStatus::Undetermined) {
        qApp->requestPermission(microphone, [](const QPermission &p) {
            qInfo() << "Microphone permission:" << int(p.status());
        });
    }
}

bool ensureScreenCaptureAccess() {
    if (CGPreflightScreenCaptureAccess())
        return true;

    // Triggers the one-time system prompt / opens System Settings > Privacy >
    // Screen Recording. The grant only applies after relaunch, so this returns
    // false now even if the user grants it.
    CGRequestScreenCaptureAccess();
    return false;
}

} // namespace MacPermissions

#else

namespace MacPermissions {

void requestCameraAndMicrophone() {}
bool ensureScreenCaptureAccess() { return true; }

} // namespace MacPermissions

#endif
