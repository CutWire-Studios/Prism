pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Prism

// Multi-line text field matching ThemedTextField chrome.
TextArea {
    id: root

    property string errorText: ""
    readonly property bool hasError: errorText.length > 0

    color: Theme.c.themedTextArea.foreground
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeSm
    wrapMode: TextArea.Wrap
    selectByMouse: true
    selectedTextColor: Theme.c.themedTextArea.selectionForeground
    selectionColor: Theme.c.themedTextArea.selectionBackground
    leftPadding: Theme.spacingLg
    rightPadding: Theme.spacingLg
    topPadding: Theme.spacingMd
    bottomPadding: Theme.spacingMd
    hoverEnabled: true
    opacity: enabled ? 1 : 0.5

    Accessible.role: Accessible.EditableText
    Accessible.description: errorText

    background: ThemeSlot {
        slot: "ThemedTextAreaBackground"
        control: root
        fallback: Component {
            Rectangle {
                radius: Theme.c.themedTextArea.radius
                color: Theme.c.themedTextArea.background
                border.width: root.hasError || root.activeFocus ? Theme.c.themedTextArea.borderWidthFocus
                                                                : (root.hovered ? Theme.c.themedTextArea.borderWidth : 0)
                border.color: root.hasError ? Theme.c.themedTextArea.borderError
                                            : (root.activeFocus ? Theme.c.themedTextArea.borderFocus
                                                                : Theme.c.themedTextArea.border)

                Behavior on border.width {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
                Behavior on border.color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
            }
        }
    }
}
