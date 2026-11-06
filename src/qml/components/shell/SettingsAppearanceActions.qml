import QtQuick
import Prism

// Theme folder shortcut, removal and the hot-reload switch. Import and export need a native file
// picker, which is not ported yet.
Column {
    id: root

    width: parent ? parent.width : 0
    spacing: Theme.spacingMd

    readonly property bool userThemeSelected: {
        if (ThemeManager.selection === "system")
            return false
        const themes = ThemeManager.themes
        for (let i = 0; i < themes.length; ++i) {
            if (themes[i].id === ThemeManager.selection)
                return !themes[i].builtIn
        }
        return true
    }

    Flow {
        width: parent.width
        spacing: Theme.spacingMd

        ThemedButton {
            text: qsTr("Open themes folder")
            variant: "secondary"
            onClicked: Qt.openUrlExternally(ThemeManager.themesFolderUrl())
        }

        ThemedButton {
            enabled: root.userThemeSelected
            text: qsTr("Remove")
            variant: "destructive"
            onClicked: confirmRemove.open()
        }
    }

    ThemedSwitch {
        checked: ThemeManager.hotReload
        text: qsTr("Reload themes when files change")
        tooltip: qsTr("Watches the selected user theme and applies edits when its files change. Built-in themes are not watched.")
        onToggled: ThemeManager.hotReload = checked
    }

    Text {
        width: parent.width
        visible: root.userThemeSelected === false && ThemeManager.hotReload
        text: qsTr("Hot reload applies the next time a user theme is selected.")
        color: Theme.c.settingsAppearanceActions.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        wrapMode: Text.WordWrap
    }

    Item {
        visible: false
        ThemedDialog {
            id: confirmRemove
            title: qsTr("Remove theme?")
            acceptText: qsTr("Remove")
            acceptVariant: "destructive"
            rejectText: qsTr("Cancel")
            preferredWidth: Theme.dialogWidthSm
            onAccepted: {
                const name = ThemeManager.selection
                if (!ThemeManager.removeTheme(name))
                    Toasts.error(ThemeManager.lastError || qsTr("Could not remove that theme."))
                else
                    Toasts.success(qsTr("Removed “%1”.").arg(name))
            }

            contentItem: ThemedLabel {
                width: parent ? parent.width : Theme.dialogWidthSm
                wrapMode: Text.WordWrap
                size: "sm"
                text: qsTr("Remove “%1” from this device? Its files are deleted.")
                      .arg(ThemeManager.selection)
            }
        }
    }
}
