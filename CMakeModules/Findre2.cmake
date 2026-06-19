# Find re2 via CMake config (Homebrew, Arch) or pkg-config (Debian/Ubuntu).
include(FindPackageHandleStandardArgs)

if(NOT re2_NO_re2_CMAKE)
    find_package(re2 QUIET CONFIG)
    if(re2_FOUND)
        find_package_handle_standard_args(re2 CONFIG_MODE)
        return()
    endif()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(re2 QUIET re2 IMPORTED_TARGET GLOBAL)
if(re2_FOUND AND NOT TARGET re2::re2)
    add_library(re2::re2 ALIAS PkgConfig::re2)
endif()

find_package_handle_standard_args(re2 DEFAULT_MSG)
