#pragma once

#include "core/SourceDescriptor.h"
#include <QPixmap>
#include <functional>

class QMenu;
class QWidget;

namespace SourcePrompt {

// Runs kind-specific dialog(s) for a single-descriptor source.
// Returns false if the user cancelled. Handles: Slideshow, Camera, Screen,
// Window, Canvas, Shader, Html, Ndi.
bool prompt(SourceDescriptor::Kind kind, QWidget *parent,
            SourceDescriptor &outDesc, QPixmap &outThumb);

// Populates menu with the canonical Add Element list (icon + label per entry).
// onFile fires for the media-file picker entry; onKind fires for each
// single-descriptor kind.
void buildMenu(QMenu *menu,
               std::function<void()> onFile,
               std::function<void(SourceDescriptor::Kind)> onKind,
               bool ndiAvailable = true);

} // namespace SourcePrompt
