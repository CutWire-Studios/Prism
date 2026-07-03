# Builds tshino/softcam (MIT) as a DLL for CutWire Prism virtual camera output on Windows.
# SOFTCAM_SOURCE_DIR must point at a populated FetchContent tree (see root CMakeLists.txt).

if(NOT WIN32)
    return()
endif()
if(NOT SOFTCAM_SOURCE_DIR)
    message(FATAL_ERROR "SOFTCAM_SOURCE_DIR is not set")
endif()

set(_softcam_root "${SOFTCAM_SOURCE_DIR}/src")

add_library(softcam SHARED
    ${_softcam_root}/softcam/softcam.cpp
    ${_softcam_root}/softcamcore/DShowSoftcam.cpp
    ${_softcam_root}/softcamcore/FrameBuffer.cpp
    ${_softcam_root}/softcamcore/Misc.cpp
    ${_softcam_root}/softcamcore/SenderAPI.cpp
    ${_softcam_root}/softcamcore/Watchdog.cpp
    ${_softcam_root}/baseclasses/amfilter.cpp
    ${_softcam_root}/baseclasses/amvideo.cpp
    ${_softcam_root}/baseclasses/combase.cpp
    ${_softcam_root}/baseclasses/dllentry.cpp
    ${_softcam_root}/baseclasses/dllsetup.cpp
    ${_softcam_root}/baseclasses/mtype.cpp
    ${_softcam_root}/baseclasses/source.cpp
    ${_softcam_root}/baseclasses/winutil.cpp
    ${_softcam_root}/baseclasses/wxdebug.cpp
    ${_softcam_root}/baseclasses/wxlist.cpp
    ${_softcam_root}/baseclasses/wxutil.cpp
)

target_include_directories(softcam PUBLIC
    ${_softcam_root}/softcam
)
target_include_directories(softcam PRIVATE
    ${_softcam_root}
    ${_softcam_root}/baseclasses
)

target_compile_definitions(softcam PRIVATE
    WIN32
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(softcam PRIVATE /Zc:strictStrings-)
    target_link_options(softcam PRIVATE
        "/DEF:${_softcam_root}/softcam/softcam.def")
endif()

target_link_libraries(softcam PRIVATE
    winmm
    ole32
    oleaut32
    uuid
    strmiids
    vfw32
)

set_target_properties(softcam PROPERTIES
    OUTPUT_NAME softcam
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)
