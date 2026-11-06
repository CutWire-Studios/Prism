# QML UI module (URI Prism). Owned by the QML rewrite; sources are added here, not in the
# top-level SOURCES list, so the core library stays widget-free once the Widgets UI is removed.

option(PRISM_QML_CACHEGEN "Compile QML to bytecode/C++ (slow); OFF for fast dev builds" ON)
if(PRISM_QML_CACHEGEN)
    set(_prism_qml_cachegen_args DISCARD_QML_CONTENTS)
else()
    set(_prism_qml_cachegen_args NO_CACHEGEN)
endif()

# Singletons that QML reaches without an import of their C++ type. The C++ ones (ThemeManager,
# PreferencesController, Workspaces, LayoutMemory) are declared in their headers with QML_SINGLETON.
foreach(_prism_singleton Theme Toasts Haptics WindowBackdrop)
    set_source_files_properties(src/qml/${_prism_singleton}.qml PROPERTIES QT_QML_SINGLETON_TYPE true)
endforeach()

file(GLOB_RECURSE PRISM_QML_FILES CONFIGURE_DEPENDS
     RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "src/qml/*.qml" "src/qml/*.js")

file(GLOB PRISM_LUCIDE_ICONS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
     "resources/icons/*.svg")

# The QML module directory is <build>/qml/Prism, not <build>/Prism: the executable is written to
# <build>/Prism and the two would collide.
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)
qt_add_qml_module(prism_qml
    URI Prism
    VERSION 1.0
    STATIC
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/Prism"
    ${_prism_qml_cachegen_args}
    SOURCES
        include/app/theme/ThemeManager.h
        src/app/theme/ThemeManager.cpp
        include/app/editor/PreferencesController.h
        src/app/editor/PreferencesController.cpp
        include/app/editor/WorkspaceStore.h
        src/app/editor/WorkspaceStore.cpp
        include/app/editor/LayoutStore.h
        src/app/editor/LayoutStore.cpp
        include/app/GraphicsApiChoice.h
        src/app/GraphicsApiChoice.cpp
    QML_FILES
        ${PRISM_QML_FILES}
    RESOURCES
        resources/themes/dark.json
        resources/themes/light.json
        resources/themes/graphite.json
        resources/themes/frost.json
        resources/themes/theme.schema.json
        resources/workspaces/live.json
        resources/fonts/InterUI-Regular.ttf
        resources/fonts/InterUI-Medium.ttf
        resources/fonts/InterUI-SemiBold.ttf
        resources/fonts/InterUI-Bold.ttf
        resources/fonts/InterUI-Italic.ttf
        ${PRISM_LUCIDE_ICONS}
)

# qmltyperegistrar includes the module headers by bare basename.
target_include_directories(prism_qml PRIVATE
    include/app/theme
    include/app/editor
    include/app
)

target_link_libraries(prism_qml PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Qml
    prism_core
)

target_link_libraries(Prism PRIVATE prism_qmlplugin)
