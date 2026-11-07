import QtQuick
import QtQuick.Controls
import Prism

ThemedDialog {
    id: root

    property string autosavePath: ""
    property var backupPaths: []

    property var _autosave: ({})
    property var _backups: []
    readonly property bool _hasAutosave: _autosave.exists === true
    readonly property bool _hasBackups: _backups.length > 0

    title: qsTr("Recover session")
    acceptText: choice.currentValue === "fresh" ? qsTr("Start fresh") : qsTr("Recover")
    showReject: false
    preferredWidth: Theme.dialogWidthMd
    closePolicy: Popup.NoAutoClose

    function show(autosave, backups) {
        autosavePath = autosave
        backupPaths = backups
        _autosave = AppController.describeSessionFile(autosave)
        _backups = backups.map(function (path) {
            const info = AppController.describeSessionFile(path)
            return { path: path, label: info.name + "  -  " + info.savedAt }
        })
        open()
    }

    onAccepted: {
        if (choice.currentValue === "autosave")
            AppController.recoverFrom(autosavePath)
        else if (choice.currentValue === "backup" && backupCombo.currentIndex >= 0)
            AppController.recoverFrom(_backups[backupCombo.currentIndex].path)
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedLabel {
            width: parent.width
            text: qsTr("Prism did not shut down cleanly last time. You can recover your last autosaved session or choose a backup.")
        }

        ThemedComboBox {
            id: choice
            width: parent.width
            textRole: "label"
            valueRole: "id"
            model: {
                const items = []
                if (root._hasAutosave)
                    items.push({ id: "autosave", label: qsTr("Recover autosave") })
                if (root._hasBackups)
                    items.push({ id: "backup", label: qsTr("Recover from backup") })
                items.push({ id: "fresh", label: qsTr("Start with an empty session") })
                return items
            }
        }

        ThemedLabel {
            width: parent.width
            visible: choice.currentValue === "autosave"
            text: qsTr("Last saved: %1").arg(root._autosave.savedAt || "")
        }

        ThemedLabel {
            width: parent.width
            visible: !root._hasAutosave
            text: qsTr("No autosave file found.")
        }

        ThemedComboBox {
            id: backupCombo
            width: parent.width
            visible: choice.currentValue === "backup"
            textRole: "label"
            model: root._backups
        }
    }
}
