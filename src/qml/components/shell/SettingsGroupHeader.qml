import QtQuick
import Prism

// Small heading over a group of SettingsRows. Hidden while searching, since the
// rows that still match are shown without their groups. `available` hides it with
// a group whose rows are all platform-gated.
Column {
    id: root

    property string text: ""
    property string query: ""
    property bool available: true

    width: parent.width
    visible: available && query.length === 0
    topPadding: Theme.spacingXl
    spacing: Theme.spacingMd

    Text {
        width: parent.width
        text: root.text
        color: Theme.c.settingsGroupHeader.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        font.weight: Font.DemiBold
    }

    Rectangle {
        width: parent.width
        height: Theme.c.settingsGroupHeader.borderWidth
        color: Theme.c.settingsGroupHeader.divider
    }
}
