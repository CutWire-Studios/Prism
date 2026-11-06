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

set_source_files_properties(src/qml/components/nodes/NodePalette.qml PROPERTIES QT_QML_SINGLETON_TYPE true)

file(GLOB_RECURSE PRISM_QML_FILES CONFIGURE_DEPENDS
     RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "src/qml/*.qml" "src/qml/*.js")

file(GLOB PRISM_LUCIDE_ICONS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
     "resources/icons/*.svg")

# The QML module directory is <build>/qml/Prism, not <build>/Prism: the executable is written to
# <build>/prism and the two (case-insensitively, on macOS and Windows) would collide.
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
        include/app/gpu/GpuStatus.h
        src/app/gpu/GpuStatus.cpp
        include/app/gpu/GpuPreference.h
        src/app/gpu/GpuPreference.cpp
        include/app/preview/AppCompositor.h
        src/app/preview/AppCompositor.cpp
        include/app/preview/PreviewItem.h
        src/app/preview/PreviewItem.cpp
        include/app/online/updates/UpdateAsset.h
        src/app/online/updates/UpdateAsset.cpp
        include/app/online/updates/UpdateChecker.h
        src/app/online/updates/UpdateChecker.cpp
        include/app/AppController.h
        src/app/AppController.cpp
        include/app/dialogs/HotkeyEditor.h
        src/app/dialogs/HotkeyEditor.cpp
        include/app/dialogs/RecordingPanel.h
        src/app/dialogs/RecordingPanel.cpp
        include/app/dialogs/RemoteServerSettings.h
        src/app/dialogs/RemoteServerSettings.cpp
        include/app/dialogs/ServiceSettings.h
        src/app/dialogs/ServiceSettings.cpp
        include/app/dialogs/SourcePrompts.h
        src/app/dialogs/SourcePrompts.cpp
        include/app/dialogs/PlaybackSettings.h
        src/app/dialogs/PlaybackSettings.cpp
        include/mcp/McpDispatcher.h
        src/mcp/McpDispatcher.cpp
        include/mcp/McpServer.h
        src/mcp/McpServer.cpp
        include/app/remote/OscServer.h
        src/app/remote/OscServer.cpp
        include/app/remote/RemoteControlServer.h
        src/app/remote/RemoteControlServer.cpp
        include/app/nodes/NodeCatalog.h
        src/app/nodes/NodeCatalog.cpp
        include/app/editors/ClipFrameView.h
        src/app/editors/ClipFrameView.cpp
        include/app/editors/ClipEditTools.h
        src/app/editors/ClipEditTools.cpp
        include/app/editors/TransformTools.h
        src/app/editors/TransformTools.cpp
        include/app/editors/PedalboardModel.h
        src/app/editors/PedalboardModel.cpp
        include/app/editors/AudioDeviceCatalog.h
        src/app/editors/AudioDeviceCatalog.cpp
        include/app/editors/AudioGraphParams.h
        src/app/editors/AudioGraphParams.cpp
        include/app/editors/CodeHighlighterAttacher.h
        src/app/editors/CodeHighlighterAttacher.cpp
        include/app/editors/MixerChannels.h
        src/app/editors/MixerChannels.cpp
        include/app/editors/ScriptPreview.h
        src/app/editors/ScriptPreview.cpp
        include/app/editors/ShaderPreview.h
        src/app/editors/ShaderPreview.cpp
        include/app/nodes/NodeGridItem.h
        src/app/nodes/NodeGridItem.cpp
        include/app/editors/StyleLayers.h
        src/app/editors/StyleLayers.cpp
        include/app/editors/EditorImageItem.h
        src/app/editors/EditorImageItem.cpp
        include/app/editors/TextSourceBackend.h
        src/app/editors/TextSourceBackend.cpp
        include/app/editors/ShapeSourceBackend.h
        src/app/editors/ShapeSourceBackend.cpp
        include/app/editors/SvgTemplateBackend.h
        src/app/editors/SvgTemplateBackend.cpp
        include/app/bin/MediaMetaCache.h
        src/app/bin/MediaMetaCache.cpp
        include/app/bin/MediaThumbnailProvider.h
        src/app/bin/MediaThumbnailProvider.cpp
        include/app/bin/MediaBinModel.h
        src/app/bin/MediaBinModel.cpp
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
    include/app/preview
    include/app/editors
    include/app/online/updates
    include/app/gpu
    include/app/dialogs
    include/app/nodes
    include/app/bin
    include/app
)

target_link_libraries(prism_qml PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Qml
    prism_core
)

target_link_libraries(prism PRIVATE prism_qmlplugin)
