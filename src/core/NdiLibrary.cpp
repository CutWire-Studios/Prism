#include "core/NdiLibrary.h"

#ifdef SWITCHX_HAVE_NDI
#include <cstddef>
#include <Processing.NDI.Lib.h>
#endif

NdiLibrary &NdiLibrary::instance() {
    static NdiLibrary lib;
    return lib;
}

bool NdiLibrary::isAvailable() const {
#ifdef SWITCHX_HAVE_NDI
    return true;
#else
    return false;
#endif
}

bool NdiLibrary::acquire() {
#ifndef SWITCHX_HAVE_NDI
    return false;
#else
    if (m_refs == 0) {
        if (!NDIlib_initialize())
            return false;
        m_initialized = true;
    }
    ++m_refs;
    return true;
#endif
}

void NdiLibrary::release() {
#ifndef SWITCHX_HAVE_NDI
    return;
#else
    if (m_refs <= 0) return;
    --m_refs;
    if (m_refs == 0 && m_initialized) {
        NDIlib_destroy();
        m_initialized = false;
    }
#endif
}
