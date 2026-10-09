#include "core/render/SkiaFonts.h"

#include <mutex>

#if defined(SK_FONTMGR_FONTCONFIG_AVAILABLE)
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#elif defined(SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE)
#include "include/ports/SkFontMgr_empty.h"
#endif

namespace prism::skia {

sk_sp<SkFontMgr> systemFontMgr()
{
    static sk_sp<SkFontMgr> mgr;
    static std::once_flag once;
    std::call_once(once, [] {
#if defined(SK_FONTMGR_FONTCONFIG_AVAILABLE)
        mgr = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
#elif defined(SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE)
        mgr = SkFontMgr_New_Custom_Empty();
#endif
        if (!mgr)
            mgr = SkFontMgr::RefEmpty();
    });
    return mgr;
}

} // namespace prism::skia
