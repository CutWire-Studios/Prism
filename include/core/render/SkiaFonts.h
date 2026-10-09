#pragma once

#include "include/core/SkFontMgr.h"
#include "include/core/SkRefCnt.h"

// Skia font manager. SVG <text> resolves families through it.

namespace prism::skia {

// The platform's font manager (fontconfig on Linux, DirectWrite on Windows, CoreText on macOS; an
// empty manager where no port is compiled in) with the fonts bundled in :/fonts in front.
// Created once, shared, thread-safe.
sk_sp<SkFontMgr> systemFontMgr();

} // namespace prism::skia
