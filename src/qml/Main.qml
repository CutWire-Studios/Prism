import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import Prism

ApplicationWindow {
    id: window

    width: 1280
    height: 800
    minimumWidth: Theme.windowMinimumWidth
    minimumHeight: Theme.windowMinimumHeight
    visible: true
    title: {
        const path = AppController.sessionPath
        const name = path.length > 0 ? path.split("/").pop() : qsTr("Untitled")
        return qsTr("Prism") + " - " + name + (AppController.sessionDirty ? " *" : "")
    }
    color: Theme.c.appWindow.background

    palette.window: Theme.c.appWindow.paletteWindow
    palette.windowText: Theme.c.appWindow.paletteWindowText
    palette.base: Theme.c.appWindow.paletteBase
    palette.text: Theme.c.appWindow.paletteText
    palette.button: Theme.c.appWindow.paletteButton
    palette.buttonText: Theme.c.appWindow.paletteButtonText
    palette.placeholderText: Theme.c.appWindow.palettePlaceholder
    palette.dark: Theme.c.appWindow.paletteDark
    palette.mid: Theme.c.appWindow.paletteMid
    palette.midlight: Theme.c.appWindow.paletteMidlight
    palette.light: Theme.c.appWindow.paletteLight
    palette.shadow: Theme.c.appWindow.paletteShadow
    palette.highlight: Theme.c.appWindow.paletteHighlight
    palette.highlightedText: Theme.c.appWindow.paletteHighlightedText
    palette.toolTipBase: Theme.c.appWindow.paletteToolTipBase
    palette.toolTipText: Theme.c.appWindow.paletteToolTipText

    onWidthChanged: Theme.windowWidth = width
    Component.onCompleted: {
        Theme.windowWidth = width
        Updates.startAutoCheck()
    }

    property string pendingConfirmId: ""
    property string pendingPickId: ""
    property var pendingPickFilters: []
    property int modalDepth: 0

    readonly property var focused: window.activeFocusItem
    readonly property bool typing: !!focused && focused.hasOwnProperty("cursorPosition")
    readonly property bool inCanvas: !!focused && focused.hasOwnProperty("spaceDown")
    readonly property bool inSceneList: !!focused && focused.hasOwnProperty("sceneListFocus")
    readonly property bool shortcutsLive: modalDepth === 0 && !typing

    function pushOverlayModal() { modalDepth++ }
    function popOverlayModal() { modalDepth = Math.max(0, modalDepth - 1) }

    function localPath(url) {
        return AppController.localPath(url)
    }

    function importDropped(urls) {
        const files = []
        for (let i = 0; i < urls.length; ++i) {
            const path = window.localPath(urls[i])
            const name = path.split("/").pop()
            if (name.indexOf(".") < 0)
                AppController.importFolder(path)
            else if (/\.psm$/i.test(name))
                AppController.openSession(path)
            else
                files.push(path)
        }
        if (files.length > 0)
            AppController.addToLibrary(files)
    }

    PanelPool {
        id: panelPool
        editors: editorRouter
    }

    EditorRouter {
        id: editorRouter
        host: window.contentItem
    }

    // Catches drops that land outside the node canvas, which adds nodes itself; these only fill the media bin.
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => {
            if (!drop.hasUrls)
                return
            window.importDropped(drop.urls)
            drop.accept()
        }
    }

    Shortcut { sequences: [StandardKey.New]; onActivated: AppController.newSession() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: AppController.requestOpenSession() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: AppController.saveSession() }
    Shortcut { sequences: [StandardKey.SaveAs]; onActivated: AppController.requestSaveSessionAs() }
    Shortcut { sequence: "Ctrl+Shift+O"; onActivated: AppController.requestAddFiles() }
    Shortcut {
        sequence: "Ctrl+M"
        enabled: AppController.recording
        onActivated: AppController.dropMarker("")
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: window.shortcutsLive && !window.inCanvas
        onActivated: window.inSceneList ? AppController.undoShow() : AppController.graph.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo]
        enabled: window.shortcutsLive && !window.inCanvas
        onActivated: window.inSceneList ? AppController.redoShow() : AppController.graph.redo()
    }
    Shortcut {
        sequence: "Ctrl+Tab"
        enabled: window.modalDepth === 0
        onActivated: AppController.enterSceneRelative(1)
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Tab", "Ctrl+Backtab"]
        enabled: window.modalDepth === 0
        onActivated: AppController.enterSceneRelative(-1)
    }
    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: window.shortcutsLive && AppController.inScene
        onActivated: AppController.stageAndTake(AppController.editedSceneId)
    }
    Shortcut {
        sequence: "B"
        enabled: window.shortcutsLive
        onActivated: AppController.togglePanic("blackout")
    }
    Shortcut {
        sequence: "P"
        enabled: window.shortcutsLive
        onActivated: AppController.togglePanic("freeze")
    }
    Shortcut {
        sequence: "F9"
        enabled: window.modalDepth === 0
        onActivated: AppController.togglePanic("stay_tuned")
    }
    Shortcut {
        sequence: "Space"
        enabled: window.shortcutsLive && !window.inCanvas && !window.inSceneList
        onActivated: AppController.cut()
    }
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: window.shortcutsLive && !window.inCanvas && !window.inSceneList
        onActivated: AppController.take()
    }

    ThemedContextMenu {
        id: fileMenu

        ThemedMenuItem { text: qsTr("New Session"); onTriggered: AppController.newSession() }
        ThemedMenuItem { text: qsTr("Open Session…"); onTriggered: AppController.requestOpenSession() }
        ThemedMenuItem { text: qsTr("Save Session"); onTriggered: AppController.saveSession() }
        ThemedMenuItem { text: qsTr("Save Session As…"); onTriggered: AppController.requestSaveSessionAs() }
        ThemedMenuSeparator { }
        ThemedMenuItem { text: qsTr("Add Media Files…"); onTriggered: AppController.requestAddFiles() }
        ThemedMenuItem { text: qsTr("Add Media Folder…"); onTriggered: AppController.requestAddFolder() }
        ThemedMenuItem {
            text: qsTr("Add Source…")
            onTriggered: Qt.callLater(() => sourceMenu.openSourcesBelow(fileButton))
        }
        ThemedMenuSeparator { }
        ThemedMenuItem { text: qsTr("Export Project…"); onTriggered: AppController.requestExportProject() }
        ThemedMenuItem { text: qsTr("Import Project…"); onTriggered: AppController.requestImportProject() }
        ThemedMenuSeparator { }
        ThemedMenuItem {
            text: qsTr("Drop Marker")
            enabled: AppController.recording
            onTriggered: AppController.dropMarker("")
        }
        ThemedMenuItem { text: qsTr("Clear Graph…"); onTriggered: AppController.clearGraph() }
    }

    AddNodeMenu {
        id: sourceMenu
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.controlHeight + Theme.spacingMd * 2
            color: Theme.c.appWindow.background

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingLg
                anchors.rightMargin: Theme.spacingLg
                spacing: Theme.spacingSm

                Repeater {
                    model: Workspaces.workspaces
                    delegate: ThemedChip {
                        required property var modelData
                        text: modelData.name
                        selected: Workspaces.activeId === modelData.id
                        onClicked: Workspaces.activeId = modelData.id
                    }
                }

                Item { Layout.fillWidth: true }

                IconButton {
                    id: fileButton
                    glyph: "folder-open"
                    variant: "ghost"
                    text: qsTr("File")
                    tooltip: qsTr("Session and media")
                    onClicked: fileMenu.popup(fileButton, 0, fileButton.height)
                }
                IconButton {
                    glyph: "undo"
                    variant: "ghost"
                    tooltip: qsTr("Undo")
                    enabled: AppController.graph.canUndo
                    onClicked: AppController.graph.undo()
                }
                IconButton {
                    glyph: "redo"
                    variant: "ghost"
                    tooltip: qsTr("Redo")
                    enabled: AppController.graph.canRedo
                    onClicked: AppController.graph.redo()
                }

                IconButton {
                    glyph: Theme.icons.settings
                    variant: "ghost"
                    text: qsTr("Settings")
                    tooltip: qsTr("Settings")
                    onClicked: settingsLoader.ensure().open()
                }
            }
        }

        EditorWorkspace {
            Layout.fillWidth: true
            Layout.fillHeight: true
            pool: panelPool
        }
    }

    LazyLoader {
        id: settingsLoader
        sourceComponent: ThemedDialog {
            id: settingsDialog
            title: qsTr("Settings")
            preferredWidth: 960
            acceptText: qsTr("Done")
            showReject: false
            padding: 0
            contentItem: SettingsPane {
                implicitWidth: 960
                implicitHeight: Math.min(680, settingsDialog.availableContentHeight)
            }
        }
    }

    LazyLoader {
        id: accelDialogLoader
        sourceComponent: AccelerationFailedDialog {}
    }

    Connections {
        target: GpuStatus
        function onAccelerationFailed(stage, backend, detail) {
            accelDialogLoader.ensure().openFor(stage, backend, detail)
        }
    }

    ThemedDialog {
        id: confirmDialog
        property string text: ""
        acceptOnReturn: false
        preferredWidth: Theme.dialogWidthSm
        contentItem: ThemedLabel {
            width: parent ? parent.width : 320
            wrapMode: Text.Wrap
            text: confirmDialog.text
        }
        onAccepted: AppController.confirmAnswered(window.pendingConfirmId, true)
        onRejected: AppController.confirmAnswered(window.pendingConfirmId, false)
    }

    ThemedDialog {
        id: messageDialog
        property string text: ""
        showReject: false
        preferredWidth: Theme.dialogWidthSm
        contentItem: ThemedLabel {
            width: parent ? parent.width : 320
            wrapMode: Text.Wrap
            text: messageDialog.text
        }
    }

    AppDialogs { anchors.fill: parent }

    FileDialog {
        id: openDialog
        onAccepted: {
            const urls = selectedFiles.length > 0 ? selectedFiles : [selectedFile]
            AppController.filePicked(window.pendingPickId, urls.map(u => AppController.localPath(u)))
        }
        onRejected: AppController.filePicked(window.pendingPickId, [])
    }

    FolderDialog {
        id: folderDialog
        onAccepted: AppController.filePicked(window.pendingPickId,
                                             [AppController.localPath(selectedFolder)])
        onRejected: AppController.filePicked(window.pendingPickId, [])
    }

    Connections {
        target: AppController

        function onConfirmRequested(id, title, text) {
            window.pendingConfirmId = id
            confirmDialog.title = title
            confirmDialog.text = text
            confirmDialog.open()
        }

        function onErrorOccurred(text) {
            messageDialog.title = qsTr("Error")
            messageDialog.text = text
            messageDialog.open()
        }

        function onInfoMessage(title, text) {
            messageDialog.title = title
            messageDialog.text = text
            messageDialog.open()
        }

        function onStatusMessage(text, timeoutMs) {
            Toasts.info(text, timeoutMs)
        }

        function onFilePickRequested(id, title, mode, nameFilters, defaultPath) {
            window.pendingPickId = id
            if (mode === "folder") {
                folderDialog.title = title
                folderDialog.open()
                return
            }
            openDialog.title = title
            openDialog.nameFilters = nameFilters
            openDialog.fileMode = mode === "save" ? FileDialog.SaveFile
                                : mode === "openMany" ? FileDialog.OpenFiles : FileDialog.OpenFile
            openDialog.open()
        }
    }
}
