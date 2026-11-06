import QtQuick
import QtQuick.Dialogs
import Prism

Row {
    id: root

    spacing: Theme.spacingMd

    signal message(string text)

    function _path(url) {
        return AppController.localPath(url)
    }

    ThemedButton {
        text: qsTr("Import profile…")
        onClicked: importDialog.open()
    }

    ThemedButton {
        text: qsTr("Export profile…")
        onClicked: exportDialog.open()
    }

    ThemedButton {
        text: qsTr("Clear all")
        onClicked: {
            HotkeyEditor.clearAll()
            root.message("")
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import hotkey profile")
        nameFilters: [qsTr("Hotkey profiles (*.json)"), qsTr("All files (*)")]
        onAccepted: root.message(HotkeyEditor.importProfile(root._path(selectedFile)).message)
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export hotkey profile")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Hotkey profiles (*.json)"), qsTr("All files (*)")]
        onAccepted: root.message(HotkeyEditor.exportProfile(root._path(selectedFile)).message)
    }
}
