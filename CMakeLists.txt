cmake_minimum_required(VERSION 3.25)

# Must be set before project() to apply to the whole build. Qt 6 / Homebrew Qt
# require macOS 11+; matches Qt's own minimum so QtMultimedia capture APIs exist.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(Prism VERSION 0.1.2 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")
include(PrismBuildSpeed)

# --- Release channel --------------------------------------------------------
# Nightly builds are the same source with a different identity: their own app id, their own
# launcher name, and a version string carrying the build stamp. That lets one install sit beside a
# stable one (a separate bundle id, Windows AppId and Flatpak id). The nightly workflow passes both
# of these; nothing else sets them.
#
# project(Prism VERSION ...) stays strictly X.Y.Z, so the string the app displays is built here.
set(PRISM_CHANNEL "stable" CACHE STRING "Release channel: stable or nightly")
set(PRISM_BUILD_ID "" CACHE STRING "Nightly build stamp, YYYYMMDD.<short sha>")

if(NOT PRISM_CHANNEL STREQUAL "stable" AND NOT PRISM_CHANNEL STREQUAL "nightly")
    message(FATAL_ERROR "PRISM_CHANNEL must be 'stable' or 'nightly', not '${PRISM_CHANNEL}'")
endif()

set(PRISM_APP_ID "org.cutwire.Prism")
set(PRISM_APP_NAME "Prism")
set(PRISM_VERSION_STRING "${PROJECT_VERSION}")
if(PRISM_CHANNEL STREQUAL "nightly")
    set(PRISM_APP_ID "org.cutwire.Prism.Nightly")
    set(PRISM_APP_NAME "Prism Nightly")
    if(PRISM_BUILD_ID STREQUAL "")
        set(PRISM_BUILD_ID "dev")
    endif()
    set(PRISM_VERSION_STRING "${PROJECT_VERSION}-nightly.${PRISM_BUILD_ID}")
endif()
message(STATUS "Prism channel: ${PRISM_CHANNEL} (${PRISM_VERSION_STRING}, ${PRISM_APP_ID})")

# Which package this build ships as. The updater only self-installs where it owns the install
# (Windows installer, macOS DMG, AppImage); package-manager builds just get the notification.
set(PRISM_DISTRIBUTIONS source windows macos appimage flatpak arch)
set(PRISM_DISTRIBUTION "source" CACHE STRING "Package type: ${PRISM_DISTRIBUTIONS}")
set_property(CACHE PRISM_DISTRIBUTION PROPERTY STRINGS ${PRISM_DISTRIBUTIONS})
if(NOT PRISM_DISTRIBUTION IN_LIST PRISM_DISTRIBUTIONS)
    message(FATAL_ERROR "PRISM_DISTRIBUTION must be one of ${PRISM_DISTRIBUTIONS}, not '${PRISM_DISTRIBUTION}'")
endif()
message(STATUS "Prism distribution: ${PRISM_DISTRIBUTION}")

# Skia draws text and shapes on the CPU (see third_party/build-skia.sh for the prebuilt)
find_package(Skia REQUIRED)

# Qt
set(_prism_qt_components Core Svg OpenGL Gui Multimedia Network Qml Quick QuickControls2 QuickEffects)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND _prism_qt_components DBus)
endif()
find_package(Qt6 REQUIRED COMPONENTS ${_prism_qt_components})
find_package(Qt6 QUIET COMPONENTS WebSockets)
if(TARGET Qt6::WebSockets)
    set(PRISM_HAVE_OBS_WS TRUE)
    message(STATUS "Qt WebSockets found — OBS integration enabled")
else()
    message(STATUS "Qt6 WebSockets not found — OBS integration disabled (install qt6-websockets)")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(re2 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
    pkg_check_modules(GSTREAMER_APP REQUIRED gstreamer-app-1.0)
    pkg_check_modules(GSTREAMER_VIDEO REQUIRED gstreamer-video-1.0)
    pkg_check_modules(LIBZIP REQUIRED libzip)
elseif(WIN32)
    find_package(re2 CONFIG REQUIRED)
    find_package(libzip CONFIG REQUIRED)
else()
    find_package(re2 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBZIP REQUIRED libzip)
endif()

# FFmpeg
find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swscale swresample avfilter)

include(FetchContent)

# SoundTouch does pitch shifting (and tempo) for the audio effects. pkg-config first: SoundTouch's
# installed CMake config can name include directories that do not exist, which is a hard
# generate-time error on an imported target. Where there is no pkg-config (MSVC), the CMake config
# is the only path, so it gets repaired instead.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SOUNDTOUCH QUIET IMPORTED_TARGET soundtouch)
endif()
if(TARGET PkgConfig::SOUNDTOUCH)
    set(PRISM_SOUNDTOUCH_TARGET PkgConfig::SOUNDTOUCH)
    # soundtouch.pc often exports -I.../include/soundtouch (for <SoundTouch.h>), but our sources
    # use <soundtouch/SoundTouch.h> and need .../include.
    get_target_property(_st_pc_includes PkgConfig::SOUNDTOUCH INTERFACE_INCLUDE_DIRECTORIES)
    if(_st_pc_includes)
        set(_st_usable "")
        foreach(_dir IN LISTS _st_pc_includes)
            if(EXISTS "${_dir}/soundtouch/SoundTouch.h")
                list(APPEND _st_usable "${_dir}")
            elseif(EXISTS "${_dir}/SoundTouch.h")
                get_filename_component(_parent "${_dir}" DIRECTORY)
                if(EXISTS "${_parent}/soundtouch/SoundTouch.h")
                    list(APPEND _st_usable "${_parent}")
                else()
                    list(APPEND _st_usable "${_dir}")
                endif()
            elseif(IS_DIRECTORY "${_dir}")
                list(APPEND _st_usable "${_dir}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES _st_usable)
        set_target_properties(PkgConfig::SOUNDTOUCH PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_st_usable}")
    endif()
else()
    find_package(SoundTouch CONFIG REQUIRED)
    set(PRISM_SOUNDTOUCH_TARGET SoundTouch::SoundTouch)
    # Upstream 2.4.x exports include dirs that do not exist (install() keywords leaked into
    # INCLUDES DESTINATION); only <prefix>/include is real.
    get_target_property(_st_includes SoundTouch::SoundTouch INTERFACE_INCLUDE_DIRECTORIES)
    if(_st_includes)
        set(_st_usable "")
        foreach(_dir IN LISTS _st_includes)
            if(IS_DIRECTORY "${_dir}")
                list(APPEND _st_usable "${_dir}")
            else()
                message(STATUS "SoundTouch: ignoring exported include dir that does not exist: ${_dir}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES _st_usable)
        set_target_properties(SoundTouch::SoundTouch PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_st_usable}")
    endif()
endif()

# JUCE: only juce_dsp and the modules it pulls in. Not juce_audio_processors, which would drag in
# the GUI modules. JUCE never opens the sound card; it only processes float blocks.
# The project helpers are unused, so skip them (they add install() rules and build juceaide).
set(JUCE_MODULES_ONLY ON CACHE BOOL "Only configure the JUCE modules" FORCE)

option(PRISM_FETCH_JUCE "Download JUCE when PRISM_JUCE_DIR is not set" ON)
# STRING, not PATH: a relative PATH cache entry is rewritten against the build directory.
set(PRISM_JUCE_DIR "" CACHE STRING
    "JUCE source tree, absolute or relative to this file; overrides PRISM_FETCH_JUCE")
set(PRISM_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag to fetch when PRISM_JUCE_DIR is unset")

if(PRISM_JUCE_DIR)
    if(IS_ABSOLUTE "${PRISM_JUCE_DIR}")
        set(_prism_juce_dir "${PRISM_JUCE_DIR}")
    else()
        set(_prism_juce_dir "${CMAKE_CURRENT_SOURCE_DIR}/${PRISM_JUCE_DIR}")
    endif()
    if(NOT EXISTS "${_prism_juce_dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "PRISM_JUCE_DIR=${PRISM_JUCE_DIR} does not point at a JUCE source tree "
            "(looked in ${_prism_juce_dir}).")
    endif()
    message(STATUS "JUCE: using ${_prism_juce_dir}")
    add_subdirectory("${_prism_juce_dir}" "${CMAKE_BINARY_DIR}/juce-build")
elseif(PRISM_FETCH_JUCE)
    message(STATUS "JUCE: fetching ${PRISM_JUCE_TAG}")
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${PRISM_JUCE_TAG}
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE)
    FetchContent_MakeAvailable(JUCE)
    set(_prism_juce_dir "${juce_SOURCE_DIR}")
else()
    message(FATAL_ERROR
        "JUCE not available. Point PRISM_JUCE_DIR at a JUCE source tree, "
        "or enable PRISM_FETCH_JUCE to download it automatically.")
endif()
find_package(kissfft CONFIG QUIET COMPONENTS float STATIC)
if(NOT kissfft_FOUND)
    FetchContent_Declare(kissfft
        GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
        GIT_TAG        131.1.0)
    set(KISSFFT_DATATYPE float CACHE STRING "" FORCE)
    set(KISSFFT_STATIC   ON  CACHE BOOL "" FORCE)
    set(KISSFFT_TEST     OFF CACHE BOOL "" FORCE)
    set(KISSFFT_TOOLS    OFF CACHE BOOL "" FORCE)
    set(KISSFFT_OPENMP   OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(kissfft)
endif()

# Lua + sol2 (optional — Script node; Text source works without Lua)
option(PRISM_WITH_LUA "Enable Lua script nodes" ON)
if(PRISM_WITH_LUA)
    if(WIN32)
        # vcpkg ships Lua 5.5; sol2 v3.5.0 supports through 5.4 — bundle 5.4 on Windows.
        FetchContent_Declare(lua54
            GIT_REPOSITORY https://github.com/lua/lua.git
            GIT_TAG        v5.4.7)
        FetchContent_MakeAvailable(lua54)
        set(_lua_src_dir ${lua54_SOURCE_DIR})
        file(GLOB _lua_sources "${_lua_src_dir}/*.c")
        list(REMOVE_ITEM _lua_sources
            "${_lua_src_dir}/lua.c" "${_lua_src_dir}/luac.c"
            "${_lua_src_dir}/onelua.c" "${_lua_src_dir}/ltests.c")
        add_library(prism_lua STATIC ${_lua_sources})
        target_include_directories(prism_lua PUBLIC ${_lua_src_dir})
        if(MSVC)
            target_compile_definitions(prism_lua PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
        set(PRISM_LUA_TARGET prism_lua)
    else()
        pkg_check_modules(LUA REQUIRED lua5.4)
        set(PRISM_LUA_TARGET ${LUA_LIBRARIES})
    endif()

    FetchContent_Declare(sol2
        GIT_REPOSITORY https://github.com/ThePhD/sol2.git
        GIT_TAG        v3.5.0)
    FetchContent_MakeAvailable(sol2)

    add_library(prism_sol2 INTERFACE)
    target_include_directories(prism_sol2 INTERFACE
        ${sol2_SOURCE_DIR}/include)
    if(WIN32)
        target_link_libraries(prism_sol2 INTERFACE ${PRISM_LUA_TARGET})
    else()
        target_include_directories(prism_sol2 INTERFACE ${LUA_INCLUDE_DIRS})
        # Homebrew keeps Lua under a Cellar prefix that isn't on Apple clang's
        # default link path, so the bare -llua needs pkg-config's -L dir.
        target_link_directories(prism_sol2 INTERFACE ${LUA_LIBRARY_DIRS})
        target_link_libraries(prism_sol2 INTERFACE ${PRISM_LUA_TARGET})
    endif()
    set(PRISM_HAVE_LUA TRUE)
    message(STATUS "Lua scripting enabled (sol2 + Lua 5.4)")
else()
    message(STATUS "Lua scripting disabled")
endif()

# NDI (optional — program output to OBS etc.)
option(PRISM_WITH_NDI "Enable NDI program output" ON)
if(PRISM_WITH_NDI)
    find_package(NDI)
    if(NDI_FOUND)
        message(STATUS "NDI found: ${NDI_LIBRARIES}")
        set(PRISM_HAVE_NDI TRUE)
    else()
        message(STATUS "NDI SDK not found — NDI output disabled (set NDI_ROOT or install NDI SDK)")
    endif()
endif()

# WebRTC phone camera (optional — libdatachannel + Qt WebSockets)
option(PRISM_WITH_WEBRTC "Enable WebRTC phone camera source" ON)
if(PRISM_WITH_WEBRTC)
    find_package(Qt6 REQUIRED COMPONENTS WebSockets)
    set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG        v0.24.5)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(libdatachannel)
    find_package(OpenSSL REQUIRED)
    message(STATUS "WebRTC phone camera source enabled (libdatachannel)")
    set(PRISM_HAVE_WEBRTC TRUE)
endif()

# ML background removal (optional — ONNX Runtime + selfie-segmentation model)
option(PRISM_WITH_SEGMENTATION "Enable ML background removal" ON)
option(PRISM_FETCH_ONNXRUNTIME
    "Download prebuilt ONNX Runtime when not found (see CMakeModules/FetchOnnxRuntime.cmake)"
    ON)
if(PRISM_WITH_SEGMENTATION)
    find_package(OnnxRuntime QUIET)
    if(NOT OnnxRuntime_FOUND AND PRISM_FETCH_ONNXRUNTIME)
        include(FetchOnnxRuntime)
        find_package(OnnxRuntime REQUIRED)
    endif()
    if(OnnxRuntime_FOUND)
        message(STATUS "ONNX Runtime found: ${OnnxRuntime_LIBRARIES} — background removal enabled")
        set(PRISM_HAVE_SEGMENTATION TRUE)
    else()
        message(STATUS "ONNX Runtime not found — background removal disabled "
            "(set ONNXRUNTIME_ROOT or enable PRISM_FETCH_ONNXRUNTIME)")
    endif()
endif()


set(SOURCES
    src/core/media/MediaFormats.cpp
    src/core/media/MediaProbe.cpp
    src/core/media/StillImage.cpp
    src/core/media/HwAccel.cpp
    src/core/media/GpuDevice.cpp
    src/core/media/GpuVideoUploader.cpp
    src/core/media/VideoDecoder.cpp
    src/core/sources/VideoFileSource.cpp
    src/core/media/AudioDecoder.cpp
    src/core/media/AudioEffectChain.cpp
    src/core/audio/AudioRack.cpp
    src/core/media/AudioAnalyzer.cpp
    src/core/media/AudioLevelMeter.cpp
    src/core/media/AudioPlayer.cpp
    src/core/media/AudioInputCapture.cpp
    src/core/media/SharedAudioCapture.cpp
    src/core/media/AudioLoopbackCapture.cpp
    src/core/media/AudioInputMixRegistry.cpp
    src/core/media/PcmResampler.cpp
    src/core/sources/ImageSource.cpp
    src/core/sources/SlideshowSource.cpp
    src/core/sources/CanvasSource.cpp
    src/core/sources/CameraSource.cpp
    src/core/sources/SharedSourcePool.cpp
    src/core/sources/WindowCaptureSource.cpp
    src/core/sources/ScreenCapturePersist.cpp
    src/core/project/ClipManager.cpp
    src/core/media/ThumbnailExtractor.cpp
    src/core/project/ClipSettings.cpp
    src/core/project/AssetPathResolver.cpp
    src/core/project/ProjectPackager.cpp
    src/core/sources/ShaderSource.cpp
    src/core/sources/EffectSources.cpp
    src/core/sources/LayerCompositorSource.cpp
    src/core/sources/SceneSource.cpp
    src/core/effects/EffectPassRunner.cpp
    src/core/effects/ShaderEffects.cpp
    src/core/effects/LutLoader.cpp
    src/core/effects/ShaderEffectSource.cpp
    src/core/sources/SvgTemplates.cpp
    src/core/sources/SvgTemplateSource.cpp
    src/core/sources/TextSource.cpp
    src/core/sources/ShapeSource.cpp
    src/core/text/BlendMode.cpp
    src/core/text/Keyframe.cpp
    src/core/text/ShapePath.cpp
    src/core/text/ShapeStyle.cpp
    src/core/text/TextAnimationPreset.cpp
    src/core/text/TextAnimator.cpp
    src/core/text/TextLook.cpp
    src/core/text/TextParamSpec.cpp
    src/core/text/TextPresetStore.cpp
    src/core/text/TextShading.cpp
    src/core/text/TextStyle.cpp
    src/core/text/VectorSlotValue.cpp
    src/core/render/SkiaFonts.cpp
    src/core/render/SkiaPath.cpp
    src/core/render/SkiaRender.cpp
    src/core/render/Compositor.cpp
    src/core/render/SkiaRuntime.cpp
    src/core/render/SkiaShading.cpp
    src/core/render/SkiaShapePainter.cpp
    src/core/render/SkiaSvg.cpp
    src/core/render/SkiaTextEffects.cpp
    src/core/render/SkiaTextPainter.cpp
    src/core/render/SkiaVectorResources.cpp
    src/core/render/TextLayout.cpp
    src/core/scripting/ScriptRuntime.cpp
    src/core/sources/NdiLibrary.cpp
    src/core/sources/NdiSource.cpp
    src/core/webrtc/NetworkUtils.cpp
    src/core/webrtc/WebRtcPairing.cpp
    src/core/webrtc/WebRtcCamPage.cpp
    src/core/webrtc/WebRtcTlsStore.cpp
    src/core/webrtc/PrismVp9RtpDepacketizer.cpp
    src/core/webrtc/FirewallUtils.cpp
    src/core/webrtc/WebRtcManager.cpp
    src/core/sources/WebRtcSource.cpp
    src/app/transitions/Transition.cpp
    src/core/graph/ClipGraph.cpp
    src/core/graph/ClipGraphModels.cpp
    src/app/graph/GraphRuntime.cpp
    src/app/show/Scene.cpp
    src/app/show/SceneListModel.cpp
    src/app/show/SceneThumbnails.cpp
    src/app/show/Preflight.cpp
    src/app/show/ShowManager.cpp
    src/app/show/VenueProfile.cpp
    src/core/graph/ProcessEffects.cpp
    src/core/graph/AudioEffects.cpp
    src/app/output/OutputHub.cpp
    src/app/output/NdiProgramSink.cpp
    src/app/output/VirtualCameraProgramSink.cpp
    src/app/output/FrameCaptureHelper.cpp
    src/app/obs/ObsWebSocketClient.cpp
    src/app/obs/ObsIntegration.cpp
    src/app/recording/ProgramRecorder.cpp
    src/app/recording/ProgramAudioRecorder.cpp
    src/app/recording/RecordingOptions.cpp
    # Extracted helper / controller classes
    src/app/common/ThumbHelper.cpp
    src/app/common/CodeHighlighter.cpp
    src/core/media/CameraEnumerator.cpp
    src/core/media/AudioLoopbackEnumerator.cpp
    src/app/hotkeys/HotkeyManager.cpp
    src/app/session/SessionManager.cpp
    src/app/transitions/TransitionController.cpp
    src/app/mainwindow/DeckController.cpp
    src/app/mainwindow/SourceFactory.cpp
    src/app/remote/OscProtocol.cpp
    src/app/remote/RemoteProtocol.cpp
    src/app/common/MainWindowUtils.cpp
    src/mcp/McpSession.cpp
    src/mcp/McpHttp.cpp
    src/mcp/McpProtocol.cpp
    src/mcp/McpCatalog.cpp
    src/mcp/McpStdio.cpp
    src/app/common/QrCodeHelper.cpp
    src/core/platform/MacPermissions.cpp
    src/core/platform/ErrorReporter.cpp
    third_party/qrcodegen/qrcodegen.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SOURCES
        src/core/sources/ScreenSource_linux.cpp
        src/core/media/AudioLoopbackCapture_gst_linux.cpp
        src/app/output/VirtualCameraProgramSink_linux.cpp)
elseif(WIN32)
    list(APPEND SOURCES
        src/core/media/D3d11GlInterop.cpp
        src/core/sources/ScreenSource_qt.cpp
        src/app/output/VirtualCameraProgramSink_win.cpp
        src/core/media/AudioLoopbackCapture_wasapi_win.cpp)
elseif(APPLE)
    # macOS: Qt QScreenCapture/QWindowCapture for screen+window capture; no
    # built-in virtual-camera loopback (would need a signed Camera Extension).
    list(APPEND SOURCES
        src/core/sources/ScreenSource_qt.cpp
        src/app/output/VirtualCameraProgramSink_mac.cpp)
else()
    list(APPEND SOURCES
        src/core/sources/ScreenSource_qt.cpp
        src/app/output/VirtualCameraProgramSink_win.cpp)
endif()

if(PRISM_HAVE_SEGMENTATION)
    list(APPEND SOURCES
        src/core/segmentation/SelfieSegmenter.cpp
        src/core/sources/SegmentationSource.cpp)
endif()

set(HEADERS
    include/core/Deck.h
    include/core/sources/MediaSource.h
    include/core/sources/SourceDescriptor.h
    include/core/media/MediaFormats.h
    include/core/media/MediaProbe.h
    include/core/media/StillImage.h
    include/core/media/HwAccel.h
    include/core/media/GpuDevice.h
    include/core/media/GpuVideoUploader.h
    include/core/media/VideoDecoder.h
    include/core/sources/VideoFileSource.h
    include/core/media/AudioDecoder.h
    include/core/media/AudioEffectChain.h
    include/core/audio/AudioRack.h
    include/core/media/AudioAnalyzer.h
    include/core/media/AudioLevelMeter.h
    include/core/media/AudioPlayer.h
    include/core/media/AudioInputCapture.h
    include/core/media/SharedAudioCapture.h
    include/core/media/AudioLoopbackCapture.h
    include/core/media/AudioLoopbackCaptureGStreamer.h
    include/core/media/AudioInputMixRegistry.h
    include/core/sources/ImageSource.h
    include/core/sources/SlideshowSource.h
    include/core/sources/CanvasSource.h
    include/core/sources/CameraSource.h
    include/core/sources/SharedSourcePool.h
    include/core/sources/ScreenSource.h
    include/core/sources/ScreenCapturePersist.h
    include/core/sources/WindowCaptureSource.h
    include/core/project/ClipManager.h
    include/core/media/ThumbnailExtractor.h
    include/core/project/ClipSettings.h
    include/core/project/AssetPathResolver.h
    include/core/project/ProjectPackager.h
    include/core/sources/ShaderSource.h
    include/core/sources/EffectSources.h
    include/core/sources/LayerCompositorSource.h
    include/core/sources/SceneSource.h
    include/core/effects/EffectPassRunner.h
    include/core/effects/ShaderEffects.h
    include/core/effects/LutLoader.h
    include/core/effects/ShaderEffectSource.h
    include/core/sources/SvgTemplates.h
    include/core/sources/SvgTemplateSource.h
    include/core/sources/TextSource.h
    include/core/sources/ShapeSource.h
    include/core/scripting/ScriptOutput.h
    include/core/scripting/ScriptRuntime.h
    include/core/sources/NdiLibrary.h
    include/core/sources/NdiSource.h
    include/core/webrtc/NetworkUtils.h
    include/core/webrtc/WebRtcPairing.h
    include/core/webrtc/WebRtcCamPage.h
    include/core/webrtc/WebRtcTlsStore.h
    include/core/webrtc/PrismVp9RtpDepacketizer.h
    include/core/webrtc/FirewallUtils.h
    include/core/webrtc/WebRtcManager.h
    include/core/sources/WebRtcSource.h
    include/core/render/Compositor.h
    include/app/transitions/Transition.h
    include/core/graph/ClipGraph.h
    include/core/graph/ClipGraphModels.h
    include/app/graph/GraphRuntime.h
    include/app/show/Scene.h
    include/app/show/SceneListModel.h
    include/app/show/SceneThumbnails.h
    include/app/show/Preflight.h
    include/app/show/ShowManager.h
    include/app/show/VenueProfile.h
    include/core/graph/ProcessEffects.h
    include/core/graph/AudioEffects.h
    include/app/output/OutputHub.h
    include/app/output/ProgramFrameSource.h
    include/app/output/NdiProgramSink.h
    include/app/output/VirtualCameraProgramSink.h
    include/app/output/FrameCaptureHelper.h
    include/app/obs/ObsWebSocketClient.h
    include/app/obs/ObsIntegration.h
    include/app/recording/ProgramRecorder.h
    include/app/recording/ProgramAudioRecorder.h
    include/app/recording/RecordingOptions.h
    include/app/output/ProgramOutputSink.h
    # Extracted helper / controller headers
    include/app/common/ThumbHelper.h
    include/core/media/CameraEnumerator.h
    include/core/media/AudioLoopbackEnumerator.h
    include/app/hotkeys/HotkeyManager.h
    include/app/session/SessionManager.h
    include/app/transitions/TransitionController.h
    include/app/mainwindow/DeckController.h
    include/app/mainwindow/SourceFactory.h
    include/app/remote/OscProtocol.h
    include/app/remote/RemoteProtocol.h
    include/app/common/MainWindowUtils.h
    include/mcp/McpJson.h
    include/mcp/McpSession.h
    include/mcp/McpHttp.h
    include/mcp/McpProtocol.h
    include/mcp/McpCatalog.h
    include/mcp/McpStdio.h
    include/app/common/QrCodeHelper.h
    include/core/platform/MacPermissions.h
    include/core/platform/ErrorReporter.h
    include/core/text/Bezier.h
    include/core/text/BlendMode.h
    include/core/text/Keyframe.h
    include/core/text/ShapePath.h
    include/core/text/ShapeStyle.h
    include/core/text/TextAnimationPreset.h
    include/core/text/TextAnimator.h
    include/core/text/TextLook.h
    include/core/text/TextParamSpec.h
    include/core/text/TextPresetStore.h
    include/core/text/TextShading.h
    include/core/text/TextStyle.h
    include/core/text/Time.h
    include/core/text/VectorSlotValue.h
    include/core/render/SkiaFonts.h
    include/core/render/SkiaPath.h
    include/core/render/SkiaRender.h
    include/core/render/SkiaRuntime.h
    include/core/render/SkiaShading.h
    include/core/render/SkiaShapePainter.h
    include/core/render/SkiaTextEffects.h
    include/core/render/SkiaTextPainter.h
    include/core/render/SkiaVectorResources.h
    include/core/render/TextLayout.h
    include/core/render/VectorPainter.h
)

if(PRISM_HAVE_SEGMENTATION)
    list(APPEND HEADERS
        include/core/segmentation/SelfieSegmenter.h
        include/core/sources/SegmentationSource.h)
endif()

# Audio effect DSP. JUCE modules compile into whatever target links them, so they live in a library
# of their own to keep JUCE's compile definitions and build time out of prism_core. prism_core only
# gets JUCE's headers, through prism_juce_headers.
add_library(prismaudiodsp STATIC
    include/core/audio/dsp/AudioStage.h
    include/core/audio/dsp/AudioEffectProcessor.h
    src/core/audio/dsp/AudioEffectProcessor.cpp
    include/core/audio/dsp/FilterProcessors.h
    src/core/audio/dsp/FilterProcessors.cpp
    include/core/audio/dsp/DynamicsProcessors.h
    src/core/audio/dsp/DynamicsProcessors.cpp
    include/core/audio/dsp/ModulationProcessors.h
    src/core/audio/dsp/ModulationProcessors.cpp
    include/core/audio/dsp/SoundTouchPitchProcessor.h
    src/core/audio/dsp/SoundTouchPitchProcessor.cpp
    include/core/audio/dsp/AudioEffectFactory.h
    src/core/audio/dsp/AudioEffectFactory.cpp
    include/core/audio/dsp/PedalCatalog.h
    src/core/audio/dsp/PedalCatalog.cpp
    include/core/audio/dsp/WavReader.h
    src/core/audio/dsp/WavReader.cpp
    include/core/audio/dsp/AudioGraph.h
    src/core/audio/dsp/AudioGraph.cpp
    include/core/audio/dsp/GraphPedals.h
    src/core/audio/dsp/GraphPedals.cpp
    include/core/audio/dsp/GraphProcessor.h
    src/core/audio/dsp/GraphProcessor.cpp
)
# Nothing here is a QObject, and AUTOMOC would otherwise scan JUCE's amalgamated sources.
set_target_properties(prismaudiodsp PROPERTIES AUTOMOC OFF AUTORCC OFF)
target_include_directories(prismaudiodsp PUBLIC include)
target_link_libraries(prismaudiodsp PRIVATE juce::juce_dsp juce::juce_recommended_config_flags
    ${PRISM_SOUNDTOUCH_TARGET})

# JUCE_USE_CURL and JUCE_WEB_BROWSER matter: without them juce_core links libcurl on Linux.
set(PRISM_JUCE_DEFINITIONS
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1
)
target_compile_definitions(prismaudiodsp PRIVATE ${PRISM_JUCE_DEFINITIONS})

# Headers and definitions only, no JUCE sources: lets prism_core translation units include
# core/audio/dsp/GraphProcessor.h without compiling a second copy of the JUCE modules.
add_library(prism_juce_headers INTERFACE)
target_include_directories(prism_juce_headers SYSTEM INTERFACE "${_prism_juce_dir}/modules")
target_compile_definitions(prism_juce_headers INTERFACE ${PRISM_JUCE_DEFINITIONS})
# JUCE derives JUCE_DEBUG from _DEBUG and checks it at link time, so header-only consumers need the
# same debug defines its module targets get, or Debug builds fail to link.
target_compile_definitions(prism_juce_headers INTERFACE $<$<CONFIG:Debug>:DEBUG=1 _DEBUG=1>)

# Core library (linked by the app and unit tests)
add_library(prism_core STATIC ${SOURCES} ${HEADERS} resources/resources.qrc)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/release.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/release.h"
    @ONLY
)

target_include_directories(prism_core PUBLIC
    include
    "${CMAKE_CURRENT_BINARY_DIR}/include"
    third_party/qrcodegen
    ${FFMPEG_INCLUDE_DIR}
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_include_directories(prism_core PUBLIC
        ${GSTREAMER_INCLUDE_DIRS}
        ${GSTREAMER_APP_INCLUDE_DIRS}
        ${GSTREAMER_VIDEO_INCLUDE_DIRS}
        ${LIBZIP_INCLUDE_DIRS}
    )
elseif(NOT WIN32)
    target_include_directories(prism_core PUBLIC ${LIBZIP_INCLUDE_DIRS})
endif()

# PRIVATE keeps Skia's include root and defines away from consumers: only the Skia*.cpp files
# include Skia headers. build-skia.sh compiles Skia with RTTI, so Prism classes may subclass Skia
# interfaces from any translation unit.
# Qt's `foreach` macro collides with SkTHash::foreach(); nothing here writes foreach.
target_link_libraries(prism_core PRIVATE Skia::Skia)
target_compile_definitions(prism_core PRIVATE QT_NO_FOREACH)

target_link_libraries(prism_core PUBLIC
    Qt6::Core
    Qt6::Svg
    Qt6::OpenGL
    Qt6::Gui
    Qt6::Multimedia
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    re2::re2
    ${FFMPEG_LIBRARIES}
    kissfft::kissfft
    prismaudiodsp
    prism_juce_headers
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(prism_core PUBLIC
        ${CMAKE_DL_LIBS}
        Qt6::DBus
        ${GSTREAMER_LIBRARIES}
        ${GSTREAMER_APP_LIBRARIES}
        ${GSTREAMER_VIDEO_LIBRARIES}
        ${LIBZIP_LIBRARIES}
    )
elseif(WIN32)
    FetchContent_Declare(softcam
        GIT_REPOSITORY https://github.com/tshino/softcam.git
        GIT_TAG        v1.8.1
        SOURCE_SUBDIR    .no-cmake)
    FetchContent_GetProperties(softcam)
    if(NOT softcam_POPULATED)
        FetchContent_Populate(softcam)
    endif()
    set(SOFTCAM_SOURCE_DIR ${softcam_SOURCE_DIR} CACHE INTERNAL "")
    add_subdirectory(third_party/softcam)

    target_link_libraries(prism_core PUBLIC libzip::zip opengl32 mf mfplat mfuuid ole32 softcam
        d3d11 dxgi d3dcompiler)
else()
    # macOS: Homebrew libzip lives under a Cellar prefix off the default link
    # path, so pass pkg-config's -L dir alongside the bare -lzip.
    target_link_directories(prism_core PUBLIC ${LIBZIP_LIBRARY_DIRS})
    target_link_libraries(prism_core PUBLIC ${LIBZIP_LIBRARIES})
    if(APPLE)
        # CoreGraphics: Screen Recording permission preflight/request.
        target_link_libraries(prism_core PUBLIC "-framework CoreGraphics")
    endif()
endif()

target_link_libraries(prism_core PRIVATE ${PRISM_SOUNDTOUCH_TARGET})

if(PRISM_HAVE_LUA)
    target_compile_definitions(prism_core PUBLIC PRISM_HAVE_LUA)
    target_link_libraries(prism_core PUBLIC prism_sol2)
endif()

if(PRISM_HAVE_NDI)
    target_compile_definitions(prism_core PUBLIC PRISM_HAVE_NDI)
    target_include_directories(prism_core PUBLIC ${NDI_INCLUDE_DIRS})
    target_link_libraries(prism_core PUBLIC ${NDI_LIBRARIES})
endif()

if(PRISM_HAVE_OBS_WS)
    target_compile_definitions(prism_core PUBLIC PRISM_HAVE_OBS_WS)
    target_link_libraries(prism_core PUBLIC Qt6::WebSockets)
endif()

if(PRISM_HAVE_WEBRTC)
    target_compile_definitions(prism_core PUBLIC PRISM_HAVE_WEBRTC)
    target_link_libraries(prism_core PUBLIC LibDataChannel::LibDataChannelStatic Qt6::WebSockets OpenSSL::SSL OpenSSL::Crypto)
endif()

if(PRISM_HAVE_SEGMENTATION)
    target_compile_definitions(prism_core PUBLIC PRISM_HAVE_SEGMENTATION)
    target_link_libraries(prism_core PUBLIC onnxruntime::onnxruntime)
endif()

if(APPLE)
    set(_prism_icns "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/Prism.icns")
    qt_add_executable(prism MACOSX_BUNDLE src/main.cpp ${_prism_icns})

    # A nightly is "Prism Nightly.app" with its own bundle id, so it installs beside a stable copy.
    set(MACOSX_BUNDLE_BUNDLE_NAME "${PRISM_APP_NAME}")
    set(MACOSX_BUNDLE_EXECUTABLE_NAME "${PRISM_APP_NAME}")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "${PRISM_APP_ID}")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_ICON_FILE "Prism.icns")

    # Copy the icon into the bundle's Resources so CFBundleIconFile resolves.
    set_source_files_properties(${_prism_icns} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(prism PROPERTIES
        OUTPUT_NAME "${PRISM_APP_NAME}"
        MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/Info.plist.in")
else()
    if(WIN32)
        set(_prism_win_rc "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/Prism.rc")
        qt_add_executable(prism WIN32 src/main.cpp ${_prism_win_rc})
    else()
        qt_add_executable(prism src/main.cpp)
    endif()
endif()

target_link_libraries(prism PRIVATE prism_core)
include(PrismQml)

# Translations. English is the source language; i18n/prism.ts is the catalog lupdate maintains.
# prism_core's own sources supply the C++ tr() strings and the QML files supply qsTr(). Locale
# catalogs go in i18n/prism_<code>.ts; see docs/i18n.md.
option(PRISM_AUTO_UPDATE_TRANSLATIONS "Run lupdate as part of the default build" OFF)
find_package(Qt6 QUIET COMPONENTS LinguistTools)
if(Qt6LinguistTools_FOUND)
    file(GLOB PRISM_TS_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/i18n/prism_*.ts")
    qt_add_translations(prism_core
        TS_FILES
            "${CMAKE_SOURCE_DIR}/i18n/prism.ts"
            ${PRISM_TS_FILES}
        RESOURCE_PREFIX "/i18n"
        SOURCES ${PRISM_QML_FILES}
        # -locations none: source line numbers in .ts files churn on every edit and make
        # i18n/*.ts conflict across PRs. Translators keep the context and source text.
        LUPDATE_OPTIONS -no-obsolete -locations none
    )
    if(NOT TARGET update_translations AND TARGET prism_core_lupdate)
        add_custom_target(update_translations DEPENDS prism_core_lupdate)
    endif()

    # An lupdate without qtdeclarative's QML parser warns, skips every .qml, and -no-obsolete
    # then deletes every QML string from i18n/*.ts. Probe once and keep auto-update off there.
    if(NOT DEFINED PRISM_LUPDATE_HAS_QML AND TARGET Qt6::lupdate)
        get_target_property(_prism_lupdate_exe Qt6::lupdate IMPORTED_LOCATION)
        if(NOT _prism_lupdate_exe)
            get_target_property(_prism_lupdate_cfgs Qt6::lupdate IMPORTED_CONFIGURATIONS)
            foreach(_prism_cfg IN LISTS _prism_lupdate_cfgs)
                get_target_property(_prism_lupdate_exe Qt6::lupdate IMPORTED_LOCATION_${_prism_cfg})
                if(_prism_lupdate_exe)
                    break()
                endif()
            endforeach()
        endif()
        set(_prism_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/lupdate-qml-probe")
        file(REMOVE_RECURSE "${_prism_probe_dir}")
        file(WRITE "${_prism_probe_dir}/probe.qml"
            "import QtQuick\nItem { property string s: qsTr(\"prism_lupdate_qml_probe\") }\n")
        if(_prism_lupdate_exe)
            execute_process(
                COMMAND "${_prism_lupdate_exe}" probe.qml -ts probe.ts
                WORKING_DIRECTORY "${_prism_probe_dir}"
                OUTPUT_QUIET ERROR_QUIET)
        endif()
        set(_prism_probe_ts "")
        if(EXISTS "${_prism_probe_dir}/probe.ts")
            file(READ "${_prism_probe_dir}/probe.ts" _prism_probe_ts)
        endif()
        string(FIND "${_prism_probe_ts}" "prism_lupdate_qml_probe" _prism_probe_hit)
        if(_prism_probe_hit GREATER -1)
            set(PRISM_LUPDATE_HAS_QML ON CACHE INTERNAL "lupdate can parse QML")
        else()
            set(PRISM_LUPDATE_HAS_QML OFF CACHE INTERNAL "lupdate can parse QML")
        endif()
    endif()

    if(PRISM_AUTO_UPDATE_TRANSLATIONS AND NOT PRISM_LUPDATE_HAS_QML)
        message(WARNING
            "lupdate has no QML/JavaScript support; running it would delete every QML string from "
            "i18n/*.ts. Not running lupdate as part of this build.")
        set(PRISM_AUTO_UPDATE_TRANSLATIONS OFF)
    endif()

    # Order lupdate ahead of lrelease so the .qm files come from the refreshed catalogs. DEFER
    # because qt_add_translations only creates the lrelease target at the end of this scope.
    if(PRISM_AUTO_UPDATE_TRANSLATIONS)
        cmake_language(DEFER CALL add_dependencies prism_core_lrelease update_translations)
    endif()

    if(PRISM_LUPDATE_HAS_QML)
        add_test(NAME Translations
            COMMAND ${CMAKE_COMMAND}
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
                -DBINARY_DIR=${CMAKE_BINARY_DIR}
                -DCONFIG=$<CONFIG>
                -P ${CMAKE_SOURCE_DIR}/CMakeModules/CheckTranslations.cmake
        )
    else()
        message(STATUS "lupdate has no QML support; skipping the Translations test.")
    endif()
else()
    message(STATUS "Qt6 LinguistTools not found; translations will not be built.")
endif()

if(WIN32)
    add_custom_command(TARGET prism POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:softcam>
            $<TARGET_FILE_DIR:prism>
        COMMENT "Copy softcam.dll next to prism.exe")

    if(PRISM_HAVE_SEGMENTATION AND ONNXRUNTIME_RUNTIME_LIBS)
        add_custom_command(TARGET prism POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${ONNXRUNTIME_RUNTIME_LIBS}
                $<TARGET_FILE_DIR:prism>
            COMMENT "Copy onnxruntime.dll next to prism.exe")
    endif()
endif()

# Enable testing
enable_testing()

# Add test subdirectory
add_subdirectory(tests)

install(TARGETS prism prism_core
    RUNTIME DESTINATION bin
    BUNDLE  DESTINATION .
    ARCHIVE DESTINATION lib)

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)

    # Installs one freedesktop metadata file under the channel's app id. On stable that is the
    # source file byte for byte. A nightly gets a generated copy with every org.cutwire.Prism
    # occurrence rewritten (AppStream <id> and <launchable>, the desktop file's Icon=) and the
    # human-facing name suffixed, so the launcher shows "Prism Nightly" beside a stable entry.
    function(prism_install_metadata source destination name)
        if(PRISM_CHANNEL STREQUAL "stable")
            install(FILES "${source}" DESTINATION "${destination}" RENAME "${name}")
            return()
        endif()
        file(READ "${CMAKE_SOURCE_DIR}/${source}" _content)
        string(REPLACE "org.cutwire.Prism" "${PRISM_APP_ID}" _content "${_content}")
        string(REPLACE "Name=Prism\n" "Name=${PRISM_APP_NAME}\n" _content "${_content}")
        string(REPLACE "<name>Prism</name>" "<name>${PRISM_APP_NAME}</name>" _content "${_content}")
        set(_generated "${CMAKE_CURRENT_BINARY_DIR}/metadata/${name}")
        file(WRITE "${_generated}" "${_content}")
        install(FILES "${_generated}" DESTINATION "${destination}")
    endfunction()

    # Every one of these is installed under the channel's app id: flatpak build-export silently
    # drops anything in /app/share whose name does not start with the app id.
    prism_install_metadata(flatpak/org.cutwire.Prism.desktop
        "${CMAKE_INSTALL_DATADIR}/applications" "${PRISM_APP_ID}.desktop")
    prism_install_metadata(flatpak/org.cutwire.Prism.metainfo.xml
        "${CMAKE_INSTALL_DATADIR}/metainfo" "${PRISM_APP_ID}.metainfo.xml")

    # 1024px source is intentionally skipped: flatpak build-export rejects icons
    # larger than 512x512.
    foreach(_size 64 128 256 512)
        install(FILES
            flatpak/share/icons/hicolor/${_size}x${_size}/apps/org.cutwire.Prism.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps
            RENAME ${PRISM_APP_ID}.png)
    endforeach()
endif()
