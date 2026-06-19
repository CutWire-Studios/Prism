# Portable FFmpeg finder for Windows, macOS, and Linux
# Looks for: avcodec, avformat, avutil, swscale, swresample

include(FindPackageHandleStandardArgs)

# Try pkg-config first (Unix-like systems)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_FFMPEG QUIET libavcodec libavformat libavutil libswscale libswresample)
endif()

# Find include directory
find_path(FFMPEG_INCLUDE_DIR
    NAMES libavcodec/avcodec.h
    HINTS ${PC_FFMPEG_INCLUDE_DIRS}
)

# Find libraries
find_library(FFMPEG_AVCODEC_LIBRARY
    NAMES avcodec
    HINTS ${PC_FFMPEG_LIBDIR}
)

find_library(FFMPEG_AVFORMAT_LIBRARY
    NAMES avformat
    HINTS ${PC_FFMPEG_LIBDIR}
)

find_library(FFMPEG_AVUTIL_LIBRARY
    NAMES avutil
    HINTS ${PC_FFMPEG_LIBDIR}
)

find_library(FFMPEG_SWSCALE_LIBRARY
    NAMES swscale
    HINTS ${PC_FFMPEG_LIBDIR}
)

find_library(FFMPEG_SWRESAMPLE_LIBRARY
    NAMES swresample
    HINTS ${PC_FFMPEG_LIBDIR}
)

# Handle components
foreach(component ${FFmpeg_FIND_COMPONENTS})
    if(component STREQUAL "avcodec")
        if(FFMPEG_AVCODEC_LIBRARY)
            set(FFmpeg_avcodec_FOUND TRUE)
        endif()
    elseif(component STREQUAL "avformat")
        if(FFMPEG_AVFORMAT_LIBRARY)
            set(FFmpeg_avformat_FOUND TRUE)
        endif()
    elseif(component STREQUAL "avutil")
        if(FFMPEG_AVUTIL_LIBRARY)
            set(FFmpeg_avutil_FOUND TRUE)
        endif()
    elseif(component STREQUAL "swscale")
        if(FFMPEG_SWSCALE_LIBRARY)
            set(FFmpeg_swscale_FOUND TRUE)
        endif()
    elseif(component STREQUAL "swresample")
        if(FFMPEG_SWRESAMPLE_LIBRARY)
            set(FFmpeg_swresample_FOUND TRUE)
        endif()
    endif()
endforeach()

# Set combined library list
set(FFMPEG_LIBRARIES
    ${FFMPEG_AVCODEC_LIBRARY}
    ${FFMPEG_AVFORMAT_LIBRARY}
    ${FFMPEG_AVUTIL_LIBRARY}
    ${FFMPEG_SWSCALE_LIBRARY}
    ${FFMPEG_SWRESAMPLE_LIBRARY}
)

find_package_handle_standard_args(FFmpeg
    FOUND_VAR FFmpeg_FOUND
    REQUIRED_VARS FFMPEG_INCLUDE_DIR FFMPEG_AVCODEC_LIBRARY FFMPEG_AVFORMAT_LIBRARY FFMPEG_AVUTIL_LIBRARY FFMPEG_SWSCALE_LIBRARY
    HANDLE_COMPONENTS
)

if(FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    set_target_properties(FFmpeg::FFmpeg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${FFMPEG_LIBRARIES}"
    )
endif()

mark_as_advanced(FFMPEG_INCLUDE_DIR FFMPEG_AVCODEC_LIBRARY FFMPEG_AVFORMAT_LIBRARY FFMPEG_AVUTIL_LIBRARY FFMPEG_SWSCALE_LIBRARY)
