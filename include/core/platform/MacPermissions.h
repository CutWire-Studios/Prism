#pragma once

/// macOS runtime-permission helpers. On macOS 10.14+ the OS only shows the
/// camera/microphone prompt when the app explicitly asks via Qt's permission
/// API (the Info.plist usage strings alone are not enough), and screen capture
/// needs the separate Screen Recording grant. These are no-ops on other
/// platforms so callers need no #ifdefs.
namespace MacPermissions {

/// Request camera and microphone access if not yet determined. Fire-and-forget:
/// the OS prompt appears on first launch; the result is applied to later
/// QCamera / QAudioSource use. Safe to call once at startup.
void requestCameraAndMicrophone();

/// Ensure Screen Recording access before capturing a display. Returns true if
/// already granted. If not, triggers the system prompt (which opens System
/// Settings) and returns false — the grant only takes effect after the app is
/// relaunched, so callers should tell the user to restart.
bool ensureScreenCaptureAccess();

} // namespace MacPermissions
