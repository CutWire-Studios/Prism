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

    PanelPool { id: pool }

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
                    glyph: Theme.icons.settings
                    variant: "ghost"
                    text: qsTr("Settings")
                    tooltip: qsTr("Settings")
                    onClicked: settingsLoader.ensure().open()
                }
            }
        }

        WorkspaceHost {
            Layout.fillWidth: true
            Layout.fillHeight: true
            pool: pool
            workspaceId: Workspaces.activeId
        }
    }

    LazyLoader {
        id: settingsLoader
        sourceComponent: ThemedDialog {
            title: qsTr("Settings")
            preferredWidth: 960
            acceptText: qsTr("Done")
            showReject: false
            padding: 0
            contentItem: SettingsPane {
                implicitWidth: 960
                implicitHeight: Math.min(680, parent ? parent.height : 680)
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

    ThemedDialog {
        id: recoveryDialog
        property string autosavePath: ""
        property var backups: []
        title: qsTr("Recover session")
        acceptText: qsTr("Recover")
        rejectText: qsTr("Start fresh")
        preferredWidth: Theme.dialogWidthSm
        contentItem: ThemedLabel {
            width: parent ? parent.width : 320
            wrapMode: Text.Wrap
            text: qsTr("Prism did not shut down cleanly. Recover the last autosave?")
        }
        onAccepted: AppController.recoverFrom(autosavePath)
    }

    FileDialog {
        id: openDialog
        onAccepted: {
            const urls = selectedFiles.length > 0 ? selectedFiles : [selectedFile]
            AppController.filePicked(window.pendingPickId, urls.map(u => u.toString().replace(/^file:\/\//, "")))
        }
        onRejected: AppController.filePicked(window.pendingPickId, [])
    }

    FolderDialog {
        id: folderDialog
        onAccepted: AppController.filePicked(window.pendingPickId,
                                             [selectedFolder.toString().replace(/^file:\/\//, "")])
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

        function onRecoveryRequested(autosavePath, backupPaths) {
            recoveryDialog.autosavePath = autosavePath
            recoveryDialog.backups = backupPaths
            recoveryDialog.open()
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
