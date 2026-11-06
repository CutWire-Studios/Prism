import QtQuick
import Prism

// Workspace, theme and language were three header buttons of their own. They share one menu
// now, and the preferences themselves live in the Settings dialog behind "Settings…".
Item {
    id: settingsButton
    implicitWidth: settingsBtn.implicitWidth
    implicitHeight: settingsBtn.implicitHeight
    width: implicitWidth
    height: implicitHeight
    anchors.verticalCenter: parent.verticalCenter

    signal multicamRequested()
    signal moreSettingsRequested()
    signal helpRequested()

    IconButton {
        id: settingsBtn
        anchors.fill: parent
        glyph: Theme.icons.settings
        variant: "ghost"
        text: qsTr("Settings")
        active: settingsMenu.opened
        tooltip: qsTr("Settings")
        onClicked: settingsMenu.popup(0, settingsButton.height + Theme.spacingMd)
    }

    AppSettingsMenu {
        id: settingsMenu
        onMulticamRequested: settingsButton.multicamRequested()
        onMoreSettingsRequested: settingsButton.moreSettingsRequested()
        onHelpRequested: settingsButton.helpRequested()
    }
}
