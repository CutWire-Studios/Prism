import QtQuick
import Prism

// One theme in the appearance list: swatch, name, and the badges that say
// whether it follows the toggle, ships QML, or is trusted to run that QML.
Item {
    id: root

    property string themeId: ""
    property string themeName: ""
    property string variantText: ""
    property bool hasOverrides: false
    property bool trusted: false
    property bool hasVariants: false
    property bool builtIn: false
    property bool systemEntry: false

    signal activate()
    signal revokeTrust()

    width: parent ? parent.width : 0
    implicitHeight: Math.max(swatchRow.implicitHeight, textCol.implicitHeight) + Theme.spacingMd * 2
    height: implicitHeight

    readonly property bool selected: ThemeManager.selection === themeId
    readonly property var swatch: {
        const _side = ThemeManager.effectiveVariant
        return ThemeManager.previewColors(systemEntry ? "system" : themeId)
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.c.settingsThemeEntry.radius
        color: root.selected ? Theme.c.settingsThemeEntry.backgroundSelected
                             : Theme.c.settingsThemeEntry.background
        border.width: Theme.c.settingsThemeEntry.borderWidth
        border.color: root.selected ? Theme.c.settingsThemeEntry.borderSelected
                                    : Theme.c.settingsThemeEntry.border
    }

    Row {
        id: swatchRow
        x: Theme.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3

        Repeater {
            model: [
                root.swatch.surface,
                root.swatch.panel,
                root.swatch.accent,
                root.swatch.foreground,
                root.swatch.primary
            ]
            delegate: Rectangle {
                required property var modelData
                width: 14
                height: 14
                radius: Theme.c.settingsThemeEntry.swatchRadius
                color: modelData || "transparent"
                border.width: Theme.c.settingsThemeEntry.borderWidth
                border.color: Theme.c.settingsThemeEntry.border
            }
        }
    }

    Column {
        id: textCol
        anchors.left: swatchRow.right
        anchors.leftMargin: Theme.spacingMd
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingXs

        Text {
            width: parent.width
            text: root.themeName
            color: Theme.c.settingsThemeEntry.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        Flow {
            width: parent.width
            spacing: Theme.spacingXs

            Rectangle {
                visible: root.variantText.length > 0
                implicitWidth: variantLabel.implicitWidth + Theme.spacingSm * 2
                implicitHeight: variantLabel.implicitHeight + Theme.spacingXs
                radius: Theme.c.settingsThemeEntry.swatchRadius
                color: Theme.c.settingsThemeEntry.badgeBackground

                Text {
                    id: variantLabel
                    anchors.centerIn: parent
                    text: root.variantText
                    color: Theme.c.settingsThemeEntry.badgeForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeTiny
                }
            }

            Rectangle {
                visible: root.hasOverrides
                implicitWidth: overridesLabel.implicitWidth + Theme.spacingSm * 2
                implicitHeight: overridesLabel.implicitHeight + Theme.spacingXs
                radius: Theme.c.settingsThemeEntry.swatchRadius
                color: Theme.c.settingsThemeEntry.badgeBackground

                Text {
                    id: overridesLabel
                    anchors.centerIn: parent
                    text: qsTr("Overrides")
                    color: Theme.c.settingsThemeEntry.badgeForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeTiny
                }
            }

            Rectangle {
                visible: root.hasOverrides
                implicitWidth: trustLabel.implicitWidth + Theme.spacingSm * 2
                implicitHeight: trustLabel.implicitHeight + Theme.spacingXs
                radius: Theme.c.settingsThemeEntry.swatchRadius
                color: Theme.c.settingsThemeEntry.badgeBackground

                Text {
                    id: trustLabel
                    anchors.centerIn: parent
                    text: root.trusted ? qsTr("Trusted") : qsTr("Not trusted")
                    color: Theme.c.settingsThemeEntry.badgeForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeTiny
                }
            }
        }

        ThemedButton {
            visible: root.hasOverrides && root.trusted && !root.builtIn
            text: qsTr("Revoke trust")
            variant: "ghost"
            onClicked: root.revokeTrust()
        }
    }

    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: root.activate()
    }
}
