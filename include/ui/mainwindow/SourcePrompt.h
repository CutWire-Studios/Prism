#pragma once

#include "core/sources/SourceDescriptor.h"
#include <QPixmap>
#include <functional>

class QMenu;
class QWidget;

namespace SourcePrompt {

// Runs kind-specific dialog(s) for a single-descriptor source.
// Returns false if the user cancelled. Handles: Slideshow, Camera, Screen,
// Window, Canvas, Shader, Html, Ndi, WebRtc.
bool prompt(SourceDescriptor::Kind kind, QWidget *parent,
            SourceDescriptor &outDesc, QPixmap &outThumb);

#ifdef PRISM_HAVE_WEBRTC
/// Reopens the phone pairing dialog for an existing WebRTC session (reconnect after drop).
bool reconnectWebRtc(QWidget *parent, const QString &sessionToken,
                     const QString &relayUrl = {});
#endif

// Populates menu with the canonical Add Element list (icon + label per entry).
// onFile fires for the media-file picker entry; onKind fires for each
// single-descriptor kind.
void buildMenu(QMenu *menu,
               std::function<void()> onFile,
               std::function<void()> onUrl,
               std::function<void(SourceDescriptor::Kind)> onKind,
               std::function<void()> onMicInput,
               bool ndiAvailable = true,
               bool webrtcAvailable = true);

} // namespace SourcePrompt
