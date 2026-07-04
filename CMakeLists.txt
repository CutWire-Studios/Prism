cmake_minimum_required(VERSION 3.16)
project(Prism VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")

# Qt
set(_prism_qt_components Core Widgets OpenGL OpenGLWidgets Gui Multimedia WebEngineWidgets WebChannel Network)
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
if(PRISM_WITH_SEGMENTATION)
    find_package(OnnxRuntime)
    if(OnnxRuntime_FOUND)
        message(STATUS "ONNX Runtime found: ${OnnxRuntime_LIBRARIES} — background removal enabled")
        set(PRISM_HAVE_SEGMENTATION TRUE)
    else()
        message(STATUS "ONNX Runtime not found — background removal disabled (set ONNXRUNTIME_ROOT)")
    endif()
endif()


set(SOURCES
    src/core/media/VideoPlayer.cpp
    src/core/sources/VideoFileSource.cpp
    src/core/media/AudioDecoder.cpp
    src/core/media/AudioAnalyzer.cpp
    src/core/media/AudioPlayer.cpp
    src/core/media/AudioInputCapture.cpp
    src/core/media/AudioInputMixRegistry.cpp
    src/core/sources/ImageSource.cpp
    src/core/sources/SlideshowSource.cpp
    src/core/sources/CanvasSource.cpp
    src/core/sources/CameraSource.cpp
    src/core/sources/WindowCaptureSource.cpp
    src/core/sources/ScreenCapturePersist.cpp
    src/core/project/ClipManager.cpp
    src/core/media/ThumbnailExtractor.cpp
    src/core/project/OverlayItem.cpp
    src/core/project/AssetPathResolver.cpp
    src/core/project/ProjectPackager.cpp
    src/core/sources/ShaderSource.cpp
    src/core/sources/HtmlSource.cpp
    src/core/sources/HtmlWorkspace.cpp
    src/core/sources/TextSource.cpp
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
    src/ui/mainwindow/MainWindow.cpp
    src/ui/canvas/VideoWidget.cpp
    src/ui/transitions/Transition.cpp
    src/ui/nodes/ClipCard.cpp
    src/ui/nodes/ClipNodeModel.cpp
    src/ui/nodes/ClipNodeEditor.cpp
    src/ui/common/AssetLibrary.cpp
    src/ui/nodes/ClipEditDialog.cpp
    src/ui/canvas/CropSelectorWidget.cpp
    src/ui/canvas/TransformCanvasWidget.cpp
    src/ui/canvas/TransformEditorDialog.cpp
    src/ui/output/OutputWindow.cpp
    src/ui/output/ProgramMirrorWidget.cpp
    src/ui/output/MirrorOutputWindow.cpp
    src/ui/output/OutputHub.cpp
    src/ui/output/NdiProgramSink.cpp
    src/ui/output/VirtualCameraProgramSink.cpp
    src/ui/output/FrameCaptureHelper.cpp
    src/ui/obs/ObsWebSocketClient.cpp
    src/ui/obs/ObsIntegration.cpp
    src/ui/recording/ProgramRecorder.cpp
    src/ui/recording/ProgramAudioRecorder.cpp
    src/ui/recording/RecordingOptions.cpp
    src/ui/recording/RecordingSettingsDialog.cpp
    src/ui/editors/ShaderEditDialog.cpp
    src/ui/editors/HtmlEditDialog.cpp
    src/ui/canvas/HtmlPreviewWidget.cpp
    src/ui/canvas/HtmlWorkspaceCanvasWidget.cpp
    src/ui/canvas/HtmlVisualEditorWidget.cpp
    src/ui/editors/ScriptEditDialog.cpp
    src/ui/editors/TextEditDialog.cpp
    # Extracted helper / controller classes
    src/ui/common/ThumbHelper.cpp
    src/ui/common/CameraEnumerator.cpp
    src/ui/common/CapturePicker.cpp
    src/ui/common/MaterialSymbols.cpp
    src/ui/hotkeys/HotkeyManager.cpp
    src/ui/hotkeys/HotkeyEditorDialog.cpp
    src/ui/session/SessionManager.cpp
    src/ui/session/SessionRecoveryDialog.cpp
    src/ui/transitions/TransitionController.cpp
    src/ui/mainwindow/DeckController.cpp
    src/ui/mainwindow/SourceFactory.cpp
    src/ui/mainwindow/SourcePrompt.cpp
    src/ui/mainwindow/PrismSplashScreen.cpp
    src/ui/remote/RemoteControlServer.cpp
    src/ui/remote/RemoteProtocol.cpp
    src/ui/mainwindow/MainWindowUtils.cpp
    src/ui/remote/RemoteServerDialog.cpp
    src/ui/common/QrCodeHelper.cpp
    third_party/qrcodegen/qrcodegen.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SOURCES
        src/core/sources/ScreenSource_linux.cpp
        src/ui/output/VirtualCameraProgramSink_linux.cpp)
else()
    list(APPEND SOURCES
        src/core/sources/ScreenSource_win.cpp
        src/ui/output/VirtualCameraProgramSink_win.cpp)
endif()

if(PRISM_HAVE_SEGMENTATION)
    list(APPEND SOURCES
        src/core/segmentation/SelfieSegmenter.cpp
        src/core/sources/SegmentationSource.cpp)
endif()

set(HEADERS
    include/core/sources/MediaSource.h
    include/core/sources/SourceDescriptor.h
    include/core/media/VideoPlayer.h
    include/core/sources/VideoFileSource.h
    include/core/media/AudioDecoder.h
    include/core/media/AudioAnalyzer.h
    include/core/media/AudioPlayer.h
    include/core/media/AudioInputCapture.h
    include/core/media/AudioInputMixRegistry.h
    include/core/sources/ImageSource.h
    include/core/sources/SlideshowSource.h
    include/core/sources/CanvasSource.h
    include/core/sources/CameraSource.h
    include/core/sources/ScreenSource.h
    include/core/sources/ScreenCapturePersist.h
    include/core/sources/WindowCaptureSource.h
    include/core/project/ClipManager.h
    include/core/media/ThumbnailExtractor.h
    include/core/project/OverlayItem.h
    include/core/project/AssetPathResolver.h
    include/core/project/ProjectPackager.h
    include/core/sources/ShaderSource.h
    include/core/sources/HtmlSource.h
    include/core/sources/HtmlWorkspace.h
    include/core/sources/TextSource.h
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
    include/ui/mainwindow/MainWindow.h
    include/ui/canvas/VideoWidget.h
    include/ui/transitions/Transition.h
    include/ui/nodes/ClipCard.h
    include/ui/nodes/ClipNodeModel.h
    include/ui/nodes/ClipNodeEditor.h
    include/ui/common/AssetLibrary.h
    include/ui/nodes/ClipEditDialog.h
    include/ui/canvas/CanvasGeometry.h
    include/ui/canvas/CropSelectorWidget.h
    include/ui/canvas/TransformCanvasWidget.h
    include/ui/canvas/TransformEditorDialog.h
    include/ui/output/OutputWindow.h
    include/ui/output/ProgramMirrorWidget.h
    include/ui/output/MirrorOutputWindow.h
    include/ui/output/OutputHub.h
    include/ui/output/ProgramFrameSource.h
    include/ui/output/NdiProgramSink.h
    include/ui/output/VirtualCameraProgramSink.h
    include/ui/output/FrameCaptureHelper.h
    include/ui/obs/ObsWebSocketClient.h
    include/ui/obs/ObsIntegration.h
    include/ui/recording/ProgramRecorder.h
    include/ui/recording/ProgramAudioRecorder.h
    include/ui/recording/RecordingOptions.h
    include/ui/recording/RecordingSettingsDialog.h
    include/ui/output/ProgramOutputSink.h
    include/ui/editors/ShaderEditDialog.h
    include/ui/editors/HtmlEditDialog.h
    include/ui/canvas/HtmlPreviewWidget.h
    include/ui/canvas/HtmlWorkspaceCanvasWidget.h
    include/ui/canvas/HtmlVisualEditorWidget.h
    include/ui/editors/ScriptEditDialog.h
    include/ui/editors/TextEditDialog.h
    # Extracted helper / controller headers
    include/ui/common/ThumbHelper.h
    include/ui/common/CameraEnumerator.h
    include/ui/common/CapturePicker.h
    include/ui/common/MaterialSymbols.h
    include/ui/hotkeys/HotkeyManager.h
    include/ui/hotkeys/HotkeyEditorDialog.h
    include/ui/session/SessionManager.h
    include/ui/session/SessionRecoveryDialog.h
    include/ui/transitions/TransitionController.h
    include/ui/mainwindow/DeckController.h
    include/ui/mainwindow/SourceFactory.h
    include/ui/mainwindow/SourcePrompt.h
    include/ui/mainwindow/PrismSplashScreen.h
    include/ui/remote/RemoteControlServer.h
    include/ui/remote/RemoteProtocol.h
    include/ui/mainwindow/MainWindowUtils.h
    include/ui/remote/RemoteServerDialog.h
    include/ui/common/QrCodeHelper.h
)

if(PRISM_HAVE_SEGMENTATION)
    list(APPEND HEADERS
        include/core/segmentation/SelfieSegmenter.h
        include/core/sources/SegmentationSource.h)
endif()

set(UI_FILES
    forms/MainWindow.ui
    forms/ClipCard.ui
    forms/ClipEditDialog.ui
    forms/OutputWindow.ui
    forms/ShaderEditDialog.ui
    forms/HtmlEditDialog.ui
    forms/ScriptEditDialog.ui
    forms/TextEditDialog.ui
    # New dialog UI files extracted from inline widget construction
    forms/TransformNodeDialog.ui
    forms/ContextNodeDialog.ui
    forms/AudioNodeDialog.ui
)

# Core library (linked by the app and unit tests)
add_library(prism_core STATIC ${SOURCES} ${HEADERS} ${UI_FILES} resources/resources.qrc)

set_target_properties(prism_core PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/forms"
)

target_include_directories(prism_core PUBLIC
    include
    third_party/qrcodegen
    ${FFMPEG_INCLUDE_DIRS}
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

target_link_libraries(prism_core PUBLIC
    Qt6::Core
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Gui
    Qt6::Multimedia
    Qt6::WebEngineWidgets
    Qt6::WebChannel
    Qt6::Network
    re2::re2
    ${FFMPEG_LIBRARIES}
    kissfft::kissfft
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(prism_core PUBLIC
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

    target_link_libraries(prism_core PUBLIC libzip::zip opengl32 mf mfplat mfuuid ole32 softcam)
else()
    target_link_libraries(prism_core PUBLIC ${LIBZIP_LIBRARIES})
endif()

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

qt_add_executable(Prism src/main.cpp)
target_link_libraries(Prism PRIVATE prism_core)

if(WIN32)
    add_custom_command(TARGET Prism POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:softcam>
            $<TARGET_FILE_DIR:Prism>
        COMMENT "Copy softcam.dll next to Prism.exe")
endif()

# Enable testing
enable_testing()

# Add test subdirectory
add_subdirectory(tests)

install(TARGETS Prism prism_core RUNTIME DESTINATION bin ARCHIVE DESTINATION lib)
