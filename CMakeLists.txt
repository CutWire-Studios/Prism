cmake_minimum_required(VERSION 3.16)
project(Prism VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGL OpenGLWidgets Gui Multimedia)

# FFmpeg
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")
find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swscale)

# Source files
set(SOURCES
    src/main.cpp
    src/core/VideoPlayer.cpp
    src/core/VideoFileSource.cpp
    src/core/ImageSource.cpp
    src/core/SlideshowSource.cpp
    src/core/ColorSource.cpp
    src/core/CameraSource.cpp
    src/core/ScreenSource.cpp
    src/core/WindowCaptureSource.cpp
    src/core/ClipManager.cpp
    src/core/ThumbnailExtractor.cpp
    src/core/OverlayItem.cpp
    src/ui/MainWindow.cpp
    src/ui/VideoWidget.cpp
    src/ui/ClipCard.cpp
    src/ui/ClipEditDialog.cpp
    src/ui/CropSelectorWidget.cpp
    src/ui/OverlayCanvasWidget.cpp
    src/ui/OutputWindow.cpp
)

set(HEADERS
    include/core/MediaSource.h
    include/core/SourceDescriptor.h
    include/core/VideoPlayer.h
    include/core/VideoFileSource.h
    include/core/ImageSource.h
    include/core/SlideshowSource.h
    include/core/ColorSource.h
    include/core/CameraSource.h
    include/core/ScreenSource.h
    include/core/WindowCaptureSource.h
    include/core/ClipManager.h
    include/core/ThumbnailExtractor.h
    include/core/OverlayItem.h
    include/ui/MainWindow.h
    include/ui/VideoWidget.h
    include/ui/ClipCard.h
    include/ui/ClipEditDialog.h
    include/ui/CropSelectorWidget.h
    include/ui/OverlayCanvasWidget.h
    include/ui/OutputWindow.h
)

set(UI_FILES
    forms/MainWindow.ui
    forms/ClipCard.ui
    forms/ClipEditDialog.ui
    forms/OutputWindow.ui
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
)

# Link libraries
target_link_libraries(Prism PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Gui
    Qt6::Multimedia
    ${FFMPEG_LIBRARIES}
)

# Enable testing
enable_testing()

# Add test subdirectory
add_subdirectory(tests)
