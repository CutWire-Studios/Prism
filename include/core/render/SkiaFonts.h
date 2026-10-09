#pragma once

#include "include/core/SkFontMgr.h"
#include "include/core/SkRefCnt.h"

// Skia font manager. SVG <text> resolves families through it.

namespace prism::skia {

// The platform's font manager: fontconfig on Linux, an empty
// manager where no port is compiled in. Created once, shared, thread-safe.
sk_sp<SkFontMgr> systemFontMgr();

} // namespace prism::skia
