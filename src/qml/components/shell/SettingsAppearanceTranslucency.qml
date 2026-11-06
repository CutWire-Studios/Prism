import QtQuick
import Prism

// WindowBackdrop owns the persisted translucent-windows switch. A theme can
// ask for it via window.translucent; the switch still needs a restart.
Column {
    id: root

    width: parent ? parent.width : 0
    spacing: Theme.spacingMd

    readonly property bool themeWantsTranslucency: ThemeManager.window.translucent === true

    ThemedSwitch {
        checked: WindowBackdrop.translucentWindows
        onToggled: WindowBackdrop.translucentWindows = checked
    }

    ThemedLabel {
        width: parent.width
        visible: WindowBackdrop.restartRequired
        wrapMode: Text.WordWrap
        color: Theme.c.settingsAppearanceTranslucency.foreground
        text: qsTr("Restart Prism to apply window transparency.")
    }

    ThemedLabel {
        width: parent.width
        wrapMode: Text.WordWrap
        color: Theme.c.settingsAppearanceTranslucency.foregroundMuted
        text: WindowBackdrop.supportedBackdrops.length > 0
              ? qsTr("Backdrops on this system: %1. Effective backdrop: %2.")
                    .arg(WindowBackdrop.supportedBackdrops.join(", "))
                    .arg(WindowBackdrop.backdrop || qsTr("none"))
              : qsTr("This system has no translucent backdrop.")
    }

    ThemedLabel {
        width: parent.width
        visible: root.themeWantsTranslucency && !WindowBackdrop.translucencyActive
        wrapMode: Text.WordWrap
        color: Theme.c.settingsAppearanceTranslucency.foreground
        text: qsTr("This theme uses a translucent window. Turn on translucent windows to show its backdrop.")
    }
}
