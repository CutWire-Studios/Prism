cmake_minimum_required(VERSION 3.16)
project(Prism VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

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

# FFmpeg
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")
find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swscale swresample)

include(FetchContent)
FetchContent_Declare(kissfft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG        131.1.0)
set(KISSFFT_DATATYPE float CACHE STRING "" FORCE)
set(KISSFFT_STATIC   ON  CACHE BOOL "" FORCE)
set(KISSFFT_TEST     OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS    OFF CACHE BOOL "" FORCE)
set(KISSFFT_OPENMP   OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(kissfft)

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


set(SOURCES
    src/main.cpp
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
    src/core/ShaderSource.cpp
    src/core/HtmlSource.cpp
    src/core/NdiLibrary.cpp
    src/core/NdiSource.cpp
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
    src/ui/ShaderEditDialog.cpp
    src/ui/HtmlEditDialog.cpp
    # Extracted helper / controller classes
    src/ui/ThumbHelper.cpp
    src/ui/HotkeyManager.cpp
    src/ui/HotkeyEditorDialog.cpp
    src/ui/SessionManager.cpp
    src/ui/SessionRecoveryDialog.cpp
    src/ui/TransitionController.cpp
    src/ui/DeckController.cpp
    src/ui/SourceFactory.cpp
    src/ui/RemoteControlServer.cpp
    src/ui/RemoteServerDialog.cpp
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
    include/core/ShaderSource.h
    include/core/HtmlSource.h
    include/core/NdiLibrary.h
    include/core/NdiSource.h
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
    include/ui/NdiProgramSink.h
    include/ui/VirtualCameraProgramSink.h
    include/ui/FrameCaptureHelper.h
    include/ui/ObsWebSocketClient.h
    include/ui/ObsIntegration.h
    include/ui/ProgramRecorder.h
    include/ui/ProgramOutputSink.h
    include/ui/ShaderEditDialog.h
    include/ui/HtmlEditDialog.h
    # Extracted helper / controller headers
    include/ui/ThumbHelper.h
    include/ui/HotkeyManager.h
    include/ui/HotkeyEditorDialog.h
    include/ui/SessionManager.h
    include/ui/SessionRecoveryDialog.h
    include/ui/TransitionController.h
    include/ui/DeckController.h
    include/ui/SourceFactory.h
    include/ui/RemoteControlServer.h
    include/ui/RemoteServerDialog.h
)

set(UI_FILES
    forms/MainWindow.ui
    forms/ClipCard.ui
    forms/ClipEditDialog.ui
    forms/OutputWindow.ui
    forms/ShaderEditDialog.ui
    forms/HtmlEditDialog.ui
    # New dialog UI files extracted from inline widget construction
    forms/TransformNodeDialog.ui
    forms/ContextNodeDialog.ui
    forms/AudioNodeDialog.ui
)

# Create executable
qt_add_executable(Prism ${SOURCES} ${HEADERS} ${UI_FILES} resources/resources.qrc)

# Tell AUTOUIC where to find .ui files
set_target_properties(Prism PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/forms"
)

# Include directories
target_include_directories(Prism PRIVATE
    include
    ${FFMPEG_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_APP_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(Prism PRIVATE
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
    kissfft
)

if(SWITCHX_HAVE_NDI)
    target_compile_definitions(Prism PRIVATE SWITCHX_HAVE_NDI)
    target_include_directories(Prism PRIVATE ${NDI_INCLUDE_DIRS})
    target_link_libraries(Prism PRIVATE ${NDI_LIBRARIES})
endif()

if(SWITCHX_HAVE_OBS_WS)
    target_compile_definitions(Prism PRIVATE SWITCHX_HAVE_OBS_WS)
    target_link_libraries(Prism PRIVATE Qt6::WebSockets)
endif()

# Enable testing
enable_testing()

# Add test subdirectory
add_subdirectory(tests)
