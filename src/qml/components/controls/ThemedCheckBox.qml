pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Prism

// Themed checkbox. Replaces the raw `CheckBox` used in the settings tab, which
// ignored the theme entirely.
CheckBox {
    id: root

    property string tooltip: ""

    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeXs
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    spacing: Theme.spacingLg

    Accessible.role: Accessible.CheckBox
    Accessible.name: text.length > 0 ? text : tooltip

    indicator: ThemeSlot {
        implicitWidth: 18
        implicitHeight: 18
        x: root.leftPadding
        y: parent.height / 2 - height / 2
        slot: "ThemedCheckBoxIndicator"
        control: root
        fallback: Component {
            Rectangle {
                radius: Theme.c.themedCheckBox.radius
                color: root.checked ? Theme.c.themedCheckBox.backgroundChecked
                                    : (root.hovered ? Theme.c.themedCheckBox.backgroundHover
                                                    : Theme.c.themedCheckBox.background)
                border.width: Theme.c.themedCheckBox.borderWidth
                border.color: root.checked ? Theme.c.themedCheckBox.borderChecked
                                           : Theme.c.themedCheckBox.border
                opacity: root.enabled ? 1 : 0.5

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                IconGlyph {
                    anchors.centerIn: parent
                    glyph: Theme.icons.check
                    iconSize: Theme.iconSizeSm
                    iconColor: Theme.c.themedCheckBox.icon
                    visible: root.checked
                    opacity: root.checked ? 1 : 0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -Theme.c.themedCheckBox.focusBorderWidth
                    radius: Theme.c.themedCheckBox.focusRadius
                    color: "transparent"
                    border.width: Theme.c.themedCheckBox.focusBorderWidth
                    border.color: Theme.c.themedCheckBox.focusBorder
                    visible: root.visualFocus
                }
            }
        }
    }

    contentItem: Text {
        text: root.text
        font: root.font
        color: Theme.c.themedCheckBox.foreground
        opacity: root.enabled ? 1 : 0.5
        verticalAlignment: Text.AlignVCenter
        leftPadding: root.indicator.width + root.spacing
        elide: Text.ElideRight
    }

    ThemedToolTip {
        text: root.tooltip
        visible: root.tooltip.length > 0 && (root.hovered || root.visualFocus)
    }

    onClicked: Haptics.toggle(root.checked)

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onWheel: (wheel) => { wheel.accepted = false }
    }
}
