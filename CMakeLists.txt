cmake_minimum_required(VERSION 3.16)
project(Prism VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGL OpenGLWidgets Gui)

# FFmpeg
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")
find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swscale)

# Source files
set(SOURCES
    src/main.cpp
    src/core/VideoPlayer.cpp
    src/core/ClipManager.cpp
    src/ui/MainWindow.cpp
    src/ui/VideoWidget.cpp
)

set(HEADERS
    src/core/VideoPlayer.h
    src/core/ClipManager.h
    src/ui/MainWindow.h
    src/ui/VideoWidget.h
)

# Create executable
qt_add_executable(Prism ${SOURCES} ${HEADERS} resources/resources.qrc)

# Include directories
target_include_directories(Prism PRIVATE
    src
    ${FFMPEG_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(Prism PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Gui
    ${FFMPEG_LIBRARIES}
)

# Enable testing
enable_testing()

# Add test subdirectory
add_subdirectory(tests)
