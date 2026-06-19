cmake_minimum_required(VERSION 3.16)
project(Prism VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")

# Qt
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGL OpenGLWidgets Gui Multimedia WebEngineWidgets DBus Network)
find_package(Qt6 QUIET COMPONENTS WebSockets)
if(TARGET Qt6::WebSockets)
    set(SWITCHX_HAVE_OBS_WS TRUE)
    message(STATUS "Qt WebSockets found — OBS integration enabled")
else()
    message(STATUS "Qt6 WebSockets not found — OBS integration disabled (install qt6-websockets)")
endif()
find_package(re2 REQUIRED)

# GStreamer (for ScreenSource PipeWire frame capture)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_APP REQUIRED gstreamer-app-1.0)
pkg_check_modules(LIBZIP REQUIRED libzip)

# FFmpeg
find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swscale swresample)

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
option(SWITCHX_WITH_LUA "Enable Lua script nodes" ON)
if(SWITCHX_WITH_LUA)
    pkg_check_modules(LUA REQUIRED lua54)

    FetchContent_Declare(sol2
        GIT_REPOSITORY https://github.com/ThePhD/sol2.git
        GIT_TAG        v3.5.0)
    FetchContent_MakeAvailable(sol2)

    add_library(switchx_sol2 INTERFACE)
    target_include_directories(switchx_sol2 INTERFACE
        ${sol2_SOURCE_DIR}/include
        ${LUA_INCLUDE_DIRS})
    target_link_libraries(switchx_sol2 INTERFACE ${LUA_LIBRARIES})
    set(SWITCHX_HAVE_LUA TRUE)
    message(STATUS "Lua scripting enabled (sol2 + system Lua 5.4)")
else()
    message(STATUS "Lua scripting disabled")
endif()

# NDI (optional — program output to OBS etc.)
option(SWITCHX_WITH_NDI "Enable NDI program output" ON)
if(SWITCHX_WITH_NDI)
    find_package(NDI)
    if(NDI_FOUND)
        message(STATUS "NDI found: ${NDI_LIBRARIES}")
        set(SWITCHX_HAVE_NDI TRUE)
    else()
        message(STATUS "NDI SDK not found — NDI output disabled (set NDI_ROOT or install NDI SDK)")
    endif()
endif()

# WebRTC phone camera (optional — libdatachannel + Qt WebSockets)
option(SWITCHX_WITH_WEBRTC "Enable WebRTC phone camera source" ON)
if(SWITCHX_WITH_WEBRTC)
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
    set(SWITCHX_HAVE_WEBRTC TRUE)
endif()


set(SOURCES
    src/core/VideoPlayer.cpp
    src/core/VideoFileSource.cpp
    src/core/AudioDecoder.cpp
    src/core/AudioAnalyzer.cpp
    src/core/AudioPlayer.cpp
    src/core/ImageSource.cpp
    src/core/SlideshowSource.cpp
    src/core/CanvasSource.cpp
    src/core/CameraSource.cpp
    src/core/ScreenSource.cpp
    src/core/WindowCaptureSource.cpp
    src/core/ClipManager.cpp
    src/core/ThumbnailExtractor.cpp
    src/core/OverlayItem.cpp
    src/core/AssetPathResolver.cpp
    src/core/ProjectPackager.cpp
    src/core/ShaderSource.cpp
    src/core/HtmlSource.cpp
    src/core/TextSource.cpp
    src/core/ScriptRuntime.cpp
    src/core/NdiLibrary.cpp
    src/core/NdiSource.cpp
    src/core/NetworkUtils.cpp
    src/core/WebRtcPairing.cpp
    src/core/WebRtcCamPage.cpp
    src/core/WebRtcTlsStore.cpp
    src/core/WebRtcH264Sdp.cpp
    src/core/SwitchXH264RtpDepacketizer.cpp
    src/core/FirewallUtils.cpp
    src/core/WebRtcManager.cpp
    src/core/WebRtcSource.cpp
    src/ui/MainWindow.cpp
    src/ui/VideoWidget.cpp
    src/ui/Transition.cpp
    src/ui/ClipCard.cpp
    src/ui/ClipNodeModel.cpp
    src/ui/ClipNodeEditor.cpp
    src/ui/ClipEditDialog.cpp
    src/ui/CropSelectorWidget.cpp
    src/ui/TransformCanvasWidget.cpp
    src/ui/TransformEditorDialog.cpp
    src/ui/GroupEditorDialog.cpp
    src/ui/OutputWindow.cpp
    src/ui/ProgramMirrorWidget.cpp
    src/ui/MirrorOutputWindow.cpp
    src/ui/OutputHub.cpp
    src/ui/NdiProgramSink.cpp
    src/ui/VirtualCameraProgramSink.cpp
    src/ui/FrameCaptureHelper.cpp
    src/ui/ObsWebSocketClient.cpp
    src/ui/ObsIntegration.cpp
    src/ui/ProgramRecorder.cpp
    src/ui/RecordingOptions.cpp
    src/ui/RecordingSettingsDialog.cpp
    src/ui/ShaderEditDialog.cpp
    src/ui/HtmlEditDialog.cpp
    src/ui/ScriptEditDialog.cpp
    src/ui/TextEditDialog.cpp
    # Extracted helper / controller classes
    src/ui/ThumbHelper.cpp
    src/ui/HotkeyManager.cpp
    src/ui/HotkeyEditorDialog.cpp
    src/ui/SessionManager.cpp
    src/ui/SessionRecoveryDialog.cpp
    src/ui/TransitionController.cpp
    src/ui/DeckController.cpp
    src/ui/SourceFactory.cpp
    src/ui/SourcePrompt.cpp
    src/ui/RemoteControlServer.cpp
    src/ui/RemoteProtocol.cpp
    src/ui/MainWindowUtils.cpp
    src/ui/RemoteServerDialog.cpp
    src/ui/QrCodeHelper.cpp
    third_party/qrcodegen/qrcodegen.cpp
)

set(HEADERS
    include/core/MediaSource.h
    include/core/SourceDescriptor.h
    include/core/VideoPlayer.h
    include/core/VideoFileSource.h
    include/core/AudioDecoder.h
    include/core/AudioAnalyzer.h
    include/core/AudioPlayer.h
    include/core/ImageSource.h
    include/core/SlideshowSource.h
    include/core/CanvasSource.h
    include/core/CameraSource.h
    include/core/ScreenSource.h
    include/core/WindowCaptureSource.h
    include/core/ClipManager.h
    include/core/ThumbnailExtractor.h
    include/core/OverlayItem.h
    include/core/AssetPathResolver.h
    include/core/ProjectPackager.h
    include/core/ShaderSource.h
    include/core/HtmlSource.h
    include/core/TextSource.h
    include/core/ScriptOutput.h
    include/core/ScriptRuntime.h
    include/core/NdiLibrary.h
    include/core/NdiSource.h
    include/core/NetworkUtils.h
    include/core/WebRtcPairing.h
    include/core/WebRtcCamPage.h
    include/core/WebRtcTlsStore.h
    include/core/WebRtcH264Sdp.h
    include/core/SwitchXH264RtpDepacketizer.h
    include/core/FirewallUtils.h
    include/core/WebRtcManager.h
    include/core/WebRtcSource.h
    include/ui/MainWindow.h
    include/ui/VideoWidget.h
    include/ui/Transition.h
    include/ui/ClipCard.h
    include/ui/ClipNodeModel.h
    include/ui/ClipNodeEditor.h
    include/ui/ClipEditDialog.h
    include/ui/CropSelectorWidget.h
    include/ui/TransformCanvasWidget.h
    include/ui/TransformEditorDialog.h
    include/ui/GroupEditorDialog.h
    include/ui/OutputWindow.h
    include/ui/ProgramMirrorWidget.h
    include/ui/MirrorOutputWindow.h
    include/ui/OutputHub.h
    include/ui/ProgramFrameSource.h
    include/ui/NdiProgramSink.h
    include/ui/VirtualCameraProgramSink.h
    include/ui/FrameCaptureHelper.h
    include/ui/ObsWebSocketClient.h
    include/ui/ObsIntegration.h
    include/ui/ProgramRecorder.h
    include/ui/RecordingOptions.h
    include/ui/RecordingSettingsDialog.h
    include/ui/ProgramOutputSink.h
    include/ui/ShaderEditDialog.h
    include/ui/HtmlEditDialog.h
    include/ui/ScriptEditDialog.h
    include/ui/TextEditDialog.h
    # Extracted helper / controller headers
    include/ui/ThumbHelper.h
    include/ui/HotkeyManager.h
    include/ui/HotkeyEditorDialog.h
    include/ui/SessionManager.h
    include/ui/SessionRecoveryDialog.h
    include/ui/TransitionController.h
    include/ui/DeckController.h
    include/ui/SourceFactory.h
    include/ui/SourcePrompt.h
    include/ui/RemoteControlServer.h
    include/ui/RemoteProtocol.h
    include/ui/MainWindowUtils.h
    include/ui/RemoteServerDialog.h
    include/ui/QrCodeHelper.h
)

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
add_library(switchx_core STATIC ${SOURCES} ${HEADERS} ${UI_FILES} resources/resources.qrc)

set_target_properties(switchx_core PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/forms"
)

target_include_directories(switchx_core PUBLIC
    include
    third_party/qrcodegen
    ${FFMPEG_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_APP_INCLUDE_DIRS}
    ${LIBZIP_INCLUDE_DIRS}
)

target_link_libraries(switchx_core PUBLIC
    Qt6::Core
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Gui
    Qt6::Multimedia
    Qt6::WebEngineWidgets
    Qt6::DBus
    Qt6::Network
    re2::re2
    ${FFMPEG_LIBRARIES}
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_APP_LIBRARIES}
    ${LIBZIP_LIBRARIES}
    kissfft::kissfft
)

if(SWITCHX_HAVE_LUA)
    target_compile_definitions(switchx_core PUBLIC SWITCHX_HAVE_LUA)
    target_link_libraries(switchx_core PUBLIC switchx_sol2)
endif()

if(SWITCHX_HAVE_NDI)
    target_compile_definitions(switchx_core PUBLIC SWITCHX_HAVE_NDI)
    target_include_directories(switchx_core PUBLIC ${NDI_INCLUDE_DIRS})
    target_link_libraries(switchx_core PUBLIC ${NDI_LIBRARIES})
endif()

if(SWITCHX_HAVE_OBS_WS)
    target_compile_definitions(switchx_core PUBLIC SWITCHX_HAVE_OBS_WS)
    target_link_libraries(switchx_core PUBLIC Qt6::WebSockets)
endif()

if(SWITCHX_HAVE_WEBRTC)
    target_compile_definitions(switchx_core PUBLIC SWITCHX_HAVE_WEBRTC)
    target_link_libraries(switchx_core PUBLIC LibDataChannel::LibDataChannelStatic Qt6::WebSockets OpenSSL::SSL OpenSSL::Crypto)
endif()

qt_add_executable(Prism src/main.cpp)
target_link_libraries(Prism PRIVATE switchx_core)

# Enable testing
enable_testing()

# Add test subdirectory
add_subdirectory(tests)

install(TARGETS Prism switchx_core RUNTIME DESTINATION bin ARCHIVE DESTINATION lib)
